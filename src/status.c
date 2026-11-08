/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
/* nanosleep() — -std=c11 alone hides the POSIX declarations. */
#define _POSIX_C_SOURCE 200809L

#include "status.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <sys/ioctl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* linux/lp.h: LPIOC_GET_DEVICE_ID(len) = _IOC(_IOC_READ, 'P', 1, len).
 * Spelled out so this file needs no kernel headers. */
#define IOC_READ 2u
#define LP_GET_DEVICE_ID(len) \
  ((IOC_READ << 30) | ((unsigned)(len) << 16) | ('P' << 8) | 1)

/* The status request: enter remote mode, ask for status, leave. Read-only as
 * far as the printer is concerned — no media, no ink.
 *
 * Deliberately WITHOUT the leading ESC @ that WIC sends. A reset in the middle
 * of a job the printer is still working through could disturb it, and this is
 * polled in the background; the printer answers fine without it (verified). */
static const unsigned char status_request[] = {
    0x1b, 0x28, 0x52, 0x08, 0x00,                   /* ESC (R len=8      */
    0x00, 'R', 'E', 'M', 'O', 'T', 'E', '1',        /* REMOTE1           */
    'S',  'T', 0x02, 0x00, 0x00, 0x01,              /* ST 02 00 00 01    */
    0x1b, 0x00, 0x00, 0x00                          /* end REMOTE        */
};

const char *status_name(int code) {
  switch (code) {
  case PRN_STATUS_ERROR:    return "error";
  case PRN_STATUS_PRINTING: return "printing";
  case PRN_STATUS_READY:    return "ready";
  case PRN_STATUS_CLEANING: return "cleaning";
  case PRN_STATUS_CHARGING: return "charging";
  default:                  return "unknown";
  }
}

/* Read the IEEE-1284 device ID; returns length written to `out`, or -1. */
static int read_device_id(int fd, char *out, size_t out_size) {
  unsigned char buf[1024];
  if (ioctl(fd, LP_GET_DEVICE_ID(sizeof(buf)), buf) < 0)
    return -1;
  size_t n = (size_t)((buf[0] << 8) | buf[1]);   /* 2-byte BE length prefix */
  if (n < 2 || n > sizeof(buf))
    return -1;
  n -= 2;
  if (n >= out_size)
    n = out_size - 1;
  memcpy(out, buf + 2, n);
  out[n] = '\0';
  return (int)n;
}

/* USB serial of an lp node, from sysfs (the 1284 device ID has none). */
static int node_serial(const char *node, char *out, size_t out_size) {
  char path[512];
  snprintf(path, sizeof(path), "/sys/class/usbmisc/%s/device/../serial", node);
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;
  int ok = fgets(out, (int)out_size, f) != NULL;
  fclose(f);
  if (!ok)
    return -1;
  out[strcspn(out, "\r\n")] = '\0';
  return 0;
}

int status_find_device_by_serial(const char *serial, char *out, size_t out_size) {
  if (!serial || !*serial)
    return -1;
  DIR *d = opendir("/dev/usb");
  if (!d)
    return -1;
  int found = -1;
  struct dirent *e;
  while (found < 0 && (e = readdir(d))) {
    if (strncmp(e->d_name, "lp", 2) != 0)
      continue;
    char sn[128];
    /* Read from sysfs, not the device — so this still resolves while CUPS
     * holds the node open for a job. */
    if (node_serial(e->d_name, sn, sizeof(sn)) == 0 &&
        strcasecmp(sn, serial) == 0) {
      snprintf(out, out_size, "/dev/usb/%s", e->d_name);
      found = 0;
    }
  }
  closedir(d);
  return found;
}

int status_find_device(char *out, size_t out_size) {
  DIR *d = opendir("/dev/usb");
  if (!d)
    return -1;
  int found = -1;
  /* Identifying a node means opening it, and usblp allows a single opener — so
   * while CUPS is spooling, the printer we are looking for is exactly the one
   * we cannot inspect. Remember it instead of reporting "no printer": the
   * caller's own open() will then fail with EBUSY, which is the honest answer
   * ("a job is going out") rather than "nothing is connected". */
  char busy[sizeof("/dev/usb/") + sizeof(((struct dirent *)0)->d_name)] = "";
  struct dirent *e;
  while (found < 0 && (e = readdir(d))) {
    if (strncmp(e->d_name, "lp", 2) != 0)
      continue;
    char path[sizeof("/dev/usb/") + sizeof(e->d_name)], id[1024];
    snprintf(path, sizeof(path), "/dev/usb/%s", e->d_name);
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
      if (errno == EBUSY && !*busy)
        snprintf(busy, sizeof(busy), "%s", path);
      continue;
    }
    if (read_device_id(fd, id, sizeof(id)) > 0 && strstr(id, "MFG:EPSON")) {
      snprintf(out, out_size, "%s", path);
      found = 0;
    }
    close(fd);
  }
  closedir(d);
  if (found < 0 && *busy) {
    snprintf(out, out_size, "%s", busy);
    found = 0;
  }
  return found;
}

/*
 * Pull the status code out of either reply form.
 *   text:   "@BDC ST\r\nST:04;\f"
 *   binary: "@BDC ST2\r\n" + LE16 length + TLV items, item 0x01 = status
 * Returns the code, or -1 when the buffer holds neither.
 *
 * Takes the LAST reply in the buffer, not the first. `ST 02 00 00 01` turns on
 * automatic status reporting, so the printer keeps emitting messages and
 * several queue up between polls — a single request has been observed coming
 * back with two replies at once. Reading the first one means reading whatever
 * the printer was doing seconds ago, which is how a cleaning can look finished
 * while it is still running.
 */
int status_parse_buffer(const unsigned char *buf, size_t n) {
  int latest = -1;

  /* Text form — that is what the L1800 answers with. */
  for (size_t i = 0; i + 4 < n; i++) {
    if (memcmp(buf + i, "ST:", 3) != 0)
      continue;
    int v = 0;
    for (int k = 0; k < 2; k++) {
      int c = buf[i + 3 + k];
      if (c >= '0' && c <= '9')       v = v * 16 + (c - '0');
      else if (c >= 'A' && c <= 'F')  v = v * 16 + (c - 'A' + 10);
      else if (c >= 'a' && c <= 'f')  v = v * 16 + (c - 'a' + 10);
      else                            { v = -1; break; }
    }
    if (v >= 0)
      latest = v;   /* keep going — the last reply is the current one */
  }

  /* Binary ST2: walk the TLV list for item 0x01, again keeping the last. */
  for (size_t i = 0; i + 12 < n; i++) {
    if (memcmp(buf + i, "@BDC ST2\r\n", 10) != 0)
      continue;
    size_t p = i + 12;   /* skip the LE16 payload length as well */
    while (p + 2 <= n) {
      unsigned id = buf[p], len = buf[p + 1];
      if (id == 0x01 && len >= 1 && p + 2 < n) {
        latest = buf[p + 2];
        break;
      }
      p += 2 + len;
    }
  }
  return latest;
}

int status_query(const char *device, int quiet) {
  char auto_path[256];
  if (!device || !*device) {
    if (status_find_device(auto_path, sizeof(auto_path)) < 0) {
      if (!quiet)
        fprintf(stderr, "No Epson printer device found under /dev/usb/lp*.\n"
                        "Pass one explicitly with --device.\n");
      return -1;
    }
    device = auto_path;
  }

  /* usblp allows a single opener, so two status queries collide: the GUI polls
   * every few seconds and a `--status --watch` in a terminal would trip over
   * it. Those collisions last milliseconds, so retry briefly before calling it
   * busy — a real job holds the device far longer than this. */
  int fd = -1;
  for (int attempt = 0; attempt < 4; attempt++) {
    fd = open(device, O_RDWR);
    if (fd >= 0 || errno != EBUSY)
      break;
    struct timespec ts = {0, 250 * 1000 * 1000};   /* 250 ms */
    nanosleep(&ts, NULL);
  }
  if (fd < 0) {
    if (errno == EBUSY)
      return PRN_STATUS_DEVICE_BUSY;   /* genuinely in use — a job is going out */
    if (!quiet)
      fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
    return -1;
  }

  int code = -1;
  if (write(fd, status_request, sizeof(status_request)) !=
      (ssize_t)sizeof(status_request)) {
    if (!quiet)
      fprintf(stderr, "Failed to send the status request to %s\n", device);
  } else {
    /* The reply is a handful of bytes and arrives almost at once, but a busy
     * printer can take a moment; poll briefly rather than blocking forever.
     * The write above wants blocking semantics, the reads do not. */
    fcntl(fd, F_SETFL, O_NONBLOCK);
    unsigned char buf[4096];
    size_t got = 0;
    int idle_rounds = 0;   /* NOT `quiet` — that is the caller's verbosity flag */
    /* Read until the device has been silent for a moment, so the queue is
     * drained and parse_status() sees the newest reply.
     *
     * Keep this SHORT. usblp allows a single opener, so every millisecond here
     * is a millisecond in which CUPS cannot start a job and another status
     * query gets EBUSY. An earlier 3 s budget made two pollers starve each
     * other: the longer a query waited, the more likely the next one was to
     * find the device busy and wait too. ~1 s is plenty for a printer that
     * normally answers in well under 100 ms. */
    for (int tries = 0; tries < 25; tries++) {
      struct timespec ts = {0, 40 * 1000 * 1000};   /* 40 ms */
      nanosleep(&ts, NULL);
      ssize_t r = (got < sizeof(buf)) ? read(fd, buf + got, sizeof(buf) - got)
                                      : 0;
      if (r > 0) {
        got += (size_t)r;
        idle_rounds = 0;
      } else if (got > 0 && ++idle_rounds >= 3) {
        break;                      /* queue drained */
      }
    }
    code = status_parse_buffer(buf, got);
    if (code < 0 && !quiet) {
      /* Writes are accepted but nothing ever comes back: the classic sign that
       * the printer is sitting in IEEE-1284.4 mode, where plain ESC/P is
       * ignored. WIC Reset Utility leaves it that way — its session ends the
       * transport but does not put the printer back into ESC/P mode, and no
       * preamble we can send from here pulls it out again. */
      fprintf(stderr,
              "No status reply from %s.\n"
              "The printer accepts data but does not answer — it may be left in\n"
              "IEEE-1284.4 mode by a service tool such as WIC Reset Utility.\n"
              "Power-cycle the printer to restore normal ESC/P communication.\n",
              device);
    }
  }
  close(fd);
  return code;
}
