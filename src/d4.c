/* SPDX-License-Identifier: GPL-3.0-only AND AGPL-3.0-or-later
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
/* poll(), nanosleep() — -std=c11 alone hides the POSIX declarations. */
#define _POSIX_C_SOURCE 200809L

#include "d4.h"
#include "l1800_reset_data.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static int d4_debug(void) {
  static int on = -1;
  if (on < 0)
    on = getenv("DWRIP_D4_DEBUG") != NULL;
  return on;
}

static void dbg(const char *dir, const uint8_t *b, int n) {
  if (!d4_debug())
    return;
  fprintf(stderr, "  %s [%d]", dir, n);
  for (int i = 0; i < n && i < 64; i++)
    fprintf(stderr, " %02X", b[i]);
  fprintf(stderr, "\n");
}

#define D4_HDR 7            /* control-command header length */
#define D4_DATA_HDR 6       /* data-frame header length */

/* The sequence that puts the printer into 1284.4 mode (d4lib.c EnterIEEE). */
static const uint8_t enter_ieee[] = {
    0x00, 0x00, 0x00, 0x1b, 0x01, '@', 'E', 'J', 'L', ' ',
    '1',  '2',  '8',  '4',  '.',  '4', 0x0a, '@', 'E', 'J',
    'L',  0x0a, '@',  'E',  'J',  'L', 0x0a};

static void msleep(int ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/* Read and discard until the device has been quiet for `quiet_ms`.
 *
 * A single empty read means nothing here — the printer sends in bursts, and
 * bailing on the first one leaves the leftovers of an earlier session in the
 * queue, which then get read as the answers to the next session's handshake. */
static void drain(int fd, int quiet_ms) {
  uint8_t junk[1024];
  for (;;) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, quiet_ms) <= 0)
      return;
    if (read(fd, junk, sizeof(junk)) <= 0)
      return;
  }
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t w = write(fd, buf + off, len - off);
    if (w <= 0)
      return -1;
    off += (size_t)w;
  }
  return 0;
}

/* Read up to `want` bytes, giving up after `timeout_ms` of total silence. */
static int read_reply(int fd, uint8_t *buf, size_t want, int timeout_ms) {
  size_t got = 0;
  while (got < want) {
    struct pollfd p = {fd, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0)
      break;                       /* timeout or error — return what we have */
    ssize_t n = read(fd, buf + got, want - got);
    if (n <= 0)
      break;
    got += (size_t)n;
  }
  return (int)got;
}

/* One control-channel transaction: send `cmd`, expect `expect` reply bytes. */
static int txn(int fd, const uint8_t *cmd, size_t len, uint8_t *reply,
               size_t expect) {
  dbg("->", cmd, (int)len);
  if (write_all(fd, cmd, len) < 0)
    return -1;
  int rd = read_reply(fd, reply, expect, 2000);
  dbg("<-", reply, rd);
  return rd;
}

static int d4_init(int fd) {
  const uint8_t cmd[8] = {0, 0, 0, 8, 1, 0, 0x00, 0x10};  /* + revision 0x10 */
  uint8_t rep[16];
  return txn(fd, cmd, sizeof(cmd), rep, 9) == 9 ? 0 : -1;
}

static int d4_get_socket(int fd, const char *service) {
  uint8_t cmd[64], rep[64];
  const size_t nlen = strlen(service);
  const size_t len = D4_HDR + nlen;
  if (len > sizeof(cmd))
    return -1;
  cmd[0] = 0; cmd[1] = 0;
  cmd[2] = (uint8_t)(len >> 8); cmd[3] = (uint8_t)(len & 0xff);
  cmd[4] = 1; cmd[5] = 0; cmd[6] = 0x09;
  memcpy(cmd + D4_HDR, service, nlen);
  int rd = txn(fd, cmd, len, rep, len + 2);
  return rd > 8 ? rep[8] : -1;
}

static int d4_close_channel(int fd, uint8_t sock) {
  const uint8_t cmd[10] = {0, 0, 0, 10, 1, 0, 0x02, sock, sock, 0};
  uint8_t rep[16];
  return txn(fd, cmd, sizeof(cmd), rep, 10) == 10 ? 0 : -1;
}

static int d4_open_channel(int fd, uint8_t sock, int *snd, int *rcv) {
  for (int attempt = 0; attempt < 3; attempt++) {
    uint8_t cmd[17] = {0, 0, 0, 17, 1, 0, 0x01, sock, sock,
                       (uint8_t)(*snd >> 8), (uint8_t)(*snd & 0xff),
                       (uint8_t)(*rcv >> 8), (uint8_t)(*rcv & 0xff),
                       0, 0, 0, 0};
    uint8_t rep[24];
    int rd = txn(fd, cmd, sizeof(cmd), rep, 16);
    if (rd == 16 && rep[7] == 0) {
      *snd = (rep[10] << 8) + rep[11];
      *rcv = (rep[12] << 8) + rep[13];
      return 0;
    }
    /* Channel already open (a previous session died, or another tool left it
     * behind): close it and try once more rather than failing outright. */
    d4_close_channel(fd, sock);
    msleep(100);
  }
  return -1;
}

/* Ask the printer for credit so WE may send. Without this the printer answers
 * a data packet with error 0x81, "no credit for received packet" — credit runs
 * in both directions and each side must hold the other's. */
static int d4_credit_request(int fd, uint8_t sock) {
  const uint8_t cmd[13] = {0, 0, 0, 13, 1, 0, 0x04, sock, sock,
                           0x00, 0x80, 0xff, 0xff};
  uint8_t rep[16];
  int rd = txn(fd, cmd, sizeof(cmd), rep, 12);
  if (rd != 12)
    return -1;
  return (rep[10] << 8) + rep[11];
}

static int d4_exit(int fd) {
  const uint8_t cmd[7] = {0, 0, 0, 7, 1, 0, 0x08};
  uint8_t rep[16];
  return txn(fd, cmd, sizeof(cmd), rep, 8) > 0 ? 0 : -1;
}

int d4_open(const char *device, d4_session_t *s) {
  memset(s, 0, sizeof(*s));
  s->fd = open(device, O_RDWR);
  if (s->fd < 0) {
    fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
    return -1;
  }

  /* Drain BEFORE the handshake, not after. The printer queues replies across
   * sessions, and leftovers from a previous run desynchronise everything that
   * follows: the Init answer is read with stale bytes in front of it, the
   * socket id then comes out of the wrong offset, and OpenChannel fails on a
   * socket that does not exist. Sessions that appeared to work simply had a
   * clean queue by luck. */
  drain(s->fd, 600);

  /* EnterIEEE: the printer answers with 8 bytes; an all-zero answer means it
   * has not switched yet, so the reference retries. */
  int entered = 0;
  for (int attempt = 0; attempt < 3 && !entered; attempt++) {
    uint8_t rep[8] = {0};
    if (write_all(s->fd, enter_ieee, sizeof(enter_ieee)) < 0)
      break;
    int rd = read_reply(s->fd, rep, sizeof(rep), 2000);
    for (int i = 0; i < rd; i++)
      if (rep[i] != 0) { entered = 1; break; }
    if (!entered)
      msleep(200);
  }
  /* A printer already in D4 (WIC was used) answers nothing here — that is not
   * a failure, the Init below is the real test. */

  if (d4_init(s->fd) < 0) {
    fprintf(stderr, "Printer did not accept the IEEE-1284.4 init.\n");
    close(s->fd);
    s->fd = -1;
    return -1;
  }

  int sock = d4_get_socket(s->fd, "EPSON-CTRL");
  if (sock < 0) {
    fprintf(stderr, "Printer has no EPSON-CTRL service channel.\n");
    d4_exit(s->fd);
    close(s->fd);
    s->fd = -1;
    return -1;
  }
  s->socket = (uint8_t)sock;

  s->send_size = 0x200;
  s->recv_size = 0x200;
  if (d4_open_channel(s->fd, s->socket, &s->send_size, &s->recv_size) < 0) {
    fprintf(stderr, "Could not open the EPSON-CTRL channel.\n");
    d4_exit(s->fd);
    close(s->fd);
    s->fd = -1;
    return -1;
  }

  /* Drop anything the printer still had queued from an earlier session — a
   * stale reply read as the answer to our next command would report the wrong
   * address's value. */
  drain(s->fd, 200);

  s->credit = d4_credit_request(s->fd, s->socket);
  if (s->credit <= 0) {
    fprintf(stderr, "Printer granted no send credit on EPSON-CTRL.\n");
    d4_close_channel(s->fd, s->socket);
    d4_exit(s->fd);
    close(s->fd);
    s->fd = -1;
    return -1;
  }
  return 0;
}

void d4_close(d4_session_t *s) {
  if (!s || s->fd < 0)
    return;
  d4_close_channel(s->fd, s->socket);
  d4_exit(s->fd);          /* leave 1284.4 so plain ESC/P works again */
  close(s->fd);
  s->fd = -1;
}

int d4_command(d4_session_t *s, const uint8_t *cmd, size_t len,
               uint8_t *reply, size_t reply_cap) {
  uint8_t frame[512];
  if (len + D4_DATA_HDR > sizeof(frame))
    return -1;

  /* Top up our own send credit when it runs out. */
  if (s->credit <= 0) {
    /* Drop anything the printer still had queued from an earlier session — a
   * stale reply read as the answer to our next command would report the wrong
   * address's value. */
  drain(s->fd, 200);

  s->credit = d4_credit_request(s->fd, s->socket);
    if (s->credit <= 0)
      return -1;
  }

  /* Drain before every command, not just at session start. The printer queues
   * replies, so anything still pending belongs to the PREVIOUS command; left
   * in place it would be read as the answer to this one. */
  drain(s->fd, 150);

  const size_t flen = len + D4_DATA_HDR;
  frame[0] = s->socket; frame[1] = s->socket;
  frame[2] = (uint8_t)(flen >> 8); frame[3] = (uint8_t)(flen & 0xff);
  frame[4] = 0;                     /* credit */
  frame[5] = 1;                     /* end of job — the command is complete */
  memcpy(frame + D4_DATA_HDR, cmd, len);
  dbg("=>", frame, (int)flen);
  if (write_all(s->fd, frame, flen) < 0)
    return -1;
  s->credit--;

  /* Credit AFTER the command. The reference warns this can make the printer
   * deliver the payload as the reply to the credit packet itself — which is
   * exactly why the read below takes whatever arrives and scans it, rather
   * than assuming one tidy data frame. */
  const uint8_t cr[11] = {0, 0, 0, 0x0b, 1, 0, 0x03, s->socket, s->socket,
                          0, 1};
  if (write_all(s->fd, cr, sizeof(cr)) < 0)
    return -1;

  /* Read in credit rounds: each credit lets the printer send one packet, and
   * the payload does not always arrive on the first one. */
  int got = 0;
  for (int round = 0; round < 4 && (size_t)got < reply_cap; round++) {
    int n = read_reply(s->fd, reply + got, reply_cap - (size_t)got, 1500);
    if (n > 0)
      got += n;
    /* Stop once the COMPLETE payload is in: "EE:" plus its hex digits and the
     * closing ';'. Stopping at "EE:" alone truncates the value itself. */
    for (int i = 0; i + 3 <= got; i++) {
      if (memcmp(reply + i, "EE:", 3) != 0)
        continue;
      for (int j = i + 3; j < got; j++)
        if (reply[j] == ';') {
          dbg("<=", reply, got);
          return got;
        }
    }
    if (round < 3) {
      const uint8_t again[11] = {0, 0, 0, 0x0b, 1, 0, 0x03,
                                 s->socket, s->socket, 0, 1};
      if (write_all(s->fd, again, sizeof(again)) < 0)
        break;
    }
  }
  dbg("<=", reply, got);
  return got;
}

int d4_status(d4_session_t *s, uint8_t *reply, size_t cap) {
  /* "st 01 00 01" — the status request WIC sends on EPSON-CTRL. The answer is
   * the binary "@BDC ST2" form, which src/status.c already parses. */
  const uint8_t cmd[5] = {'s', 't', 0x01, 0x00, 0x01};
  return d4_command(s, cmd, sizeof(cmd), reply, cap);
}

int d4_read_eeprom(d4_session_t *s, uint8_t addr, uint8_t *value) {
  /* Same bytes WIC sends; 51 05 41 BE A0 is the READ key. There is a matching
   * WRITE key that resets counters — deliberately not implemented. */
  const uint8_t payload[7] = {0x51, 0x05, 0x41, 0xBE, 0xA0, addr, 0x00};
  uint8_t cmd[16];
  cmd[0] = '|'; cmd[1] = '|';
  cmd[2] = (uint8_t)(sizeof(payload) & 0xff);
  cmd[3] = (uint8_t)(sizeof(payload) >> 8);
  memcpy(cmd + 4, payload, sizeof(payload));

  /* The printer echoes the address in its reply, so a reply for a DIFFERENT
   * address is a leftover from an earlier command. Retry rather than trust it
   * — silently returning another address's value would be far worse than
   * failing. */
  for (int attempt = 0; attempt < 3; attempt++) {
    uint8_t rep[256];
    int rd = d4_command(s, cmd, 4 + sizeof(payload), rep, sizeof(rep));
    if (rd <= 0)
      continue;
    for (int i = 0; i + 8 <= rd; i++) {
      if (memcmp(rep + i, "EE:", 3) != 0)
        continue;
      unsigned a = 0, v = 0;
      if (sscanf((const char *)rep + i + 3, "%4x%2x", &a, &v) == 2 &&
          (a & 0xff) == addr) {
        *value = (uint8_t)v;
        return 0;
      }
    }
  }
  return -1;
}

/* ReInkPy-related EEPROM-write framing and parameter use in this function
 * are conveyed under AGPL-3.0-or-later. Original DWRip portions remain under
 * GPL-3.0-only; see LICENSE_SCOPE.md for the section-13 combined work.
 * Modified 2026-11-08: moved the existing model key into an attributed header;
 * command bytes, reply handling, and write behavior are unchanged. */
int d4_write_eeprom(d4_session_t *s, uint8_t addr, uint8_t value) {
  /* Layout (reinkpy, which drives these printers over USB as we do:
   *     struct.pack('<H B', addr, value) + wkey ):
   *     || len | rkey | 42 BD 21 | addr_lo addr_hi | value | write key
   *
   * Note the order — value BEFORE the key. Putting the key first gets the
   * command authenticated ("||:42:OK;") yet writes nothing, which is a
   * confusing way to fail: the acknowledgement says the packet was well formed
   * and the key accepted, not that anything was stored.
   *
   * 0x42 = EEPROM write. The two bytes after it are derived from it, not a
   * secret: cmd1 = ~cmd, cmd2 = ((cmd>>1)&0x7F)|((cmd<<7)&0x80). The same
   * formula applied to 0x41 yields the 41 BE A0 seen in WIC's own read.
   *
   * The write key is the part that cannot be derived: an eight-character word
   * specific to the model, and its absence is why an earlier attempt came back
   * "||:42:NA;". ReInkPy stores separate wkey and wkey1 fields for L1800;
   * this USB path uses wkey verbatim, as recorded in l1800_reset_data.h.
   * That same entry lists rkey 0x0551 and waste addresses 0x1C/0x1D — both of
   * which we had already derived independently from WIC's log, which is what
   * makes the entry trustworthy for this printer.
   *
   * A wrong key is refused and writes nothing, so a mismatch costs nothing. */
  /* The key goes on the wire exactly as reinkpy's database stores it. Its
   * companion field "KotaGede" is the same word shifted by one and is NOT what
   * the printer wants — tried, acknowledged with "OK", and stored nothing.
   *
   * That "OK" is worth distrusting in general: a deliberately wrong key of the
   * right length is also acknowledged, and only the read-back shows that
   * nothing happened. Verify writes by reading, never by the reply. */
  static const char kWriteKey[] = DWRIP_L1800_WRITE_KEY;
  const size_t klen = sizeof(kWriteKey) - 1;

  uint8_t payload[32];
  size_t n = 0;
  payload[n++] = 0x51; payload[n++] = 0x05;          /* read key */
  payload[n++] = 0x42; payload[n++] = 0xBD; payload[n++] = 0x21;
  payload[n++] = addr; payload[n++] = 0x00;          /* address, low byte first */
  payload[n++] = value;
  memcpy(payload + n, kWriteKey, klen); n += klen;

  uint8_t cmd[48];
  cmd[0] = '|'; cmd[1] = '|';
  cmd[2] = (uint8_t)(n & 0xff);
  cmd[3] = (uint8_t)(n >> 8);
  memcpy(cmd + 4, payload, n);

  uint8_t rep[128];
  int rd = d4_command(s, cmd, 4 + n, rep, sizeof(rep));

  /* A refusal comes back as "||:42:NA;" — the command understood, the write
   * declined. Report it as such; the caller also verifies by reading the
   * address back, which is the only check worth fully trusting. */
  for (int i = 0; i + 6 <= rd; i++)
    if (memcmp(rep + i, "||:", 3) == 0) {
      for (int j = i; j + 2 < rd; j++)
        if (memcmp(rep + j, "NA", 2) == 0) {
          fprintf(stderr, "Printer refused the EEPROM write (\"||:%02X:NA\") — "
                          "writes are locked.\n", cmd[6]);
          return -1;
        }
      break;
    }
  return rd >= 0 ? 0 : -1;
}
