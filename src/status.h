/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef STATUS_H
#define STATUS_H

#include <stddef.h>

/*
 * Printer status read-back over the USB printer device.
 *
 * DWRip normally never touches the device — it writes a PRN and lets CUPS
 * spool it. Status is the one thing that cannot work that way: a spooled job
 * is "done" the moment the bytes are handed over, which says nothing about
 * whether the printer has finished acting on them. Head cleaning in particular
 * runs for a minute or more after the command is delivered.
 *
 * Protocol (verified on an L1800, see docs/analisis_cleaning_wic.md):
 *   write  ESC @  ESC ( R 08 00 00 REMOTE1 ST 02 00 00 01  ESC 00 00 00
 *   read   "@BDC ST\r\nST:04;\f"          (text form, what the L1800 sends)
 *      or  "@BDC ST2\r\n" + LE16 len + TLV items, item 0x01 = status
 * Both forms are parsed; the status codes are the same in either.
 */

/* Returned instead of a status code when the device node is held by another
 * program (CUPS spooling a job). Not an error — it means data is going out. */
#define PRN_STATUS_DEVICE_BUSY (-2)

/* Status codes, mapped from WIC's own labels for the same replies. */
typedef enum {
  PRN_STATUS_ERROR    = 0x00,
  PRN_STATUS_PRINTING = 0x03,
  PRN_STATUS_READY    = 0x04,
  PRN_STATUS_CLEANING = 0x07,
  PRN_STATUS_CHARGING = 0x11
} printer_status_t;

/* Human name for a status code ("ready", "cleaning", … "unknown" otherwise). */
const char *status_name(int code);

/*
 * Query the printer. `device` may be NULL to auto-detect the first Epson
 * /dev/usb/lp* node. Returns the status code (>= 0), PRN_STATUS_DEVICE_BUSY
 * when something else holds the device, or -1 on failure (message on stderr).
 */
int status_query(const char *device, int quiet);

/*
 * Auto-detect: first /dev/usb/lp* whose IEEE-1284 device ID says MFG:EPSON.
 * Writes the path into `out` and returns 0, or returns -1 when none is found.
 * NOTE: with two USB Epson printers attached this picks whichever enumerates
 * first — pass an explicit device in that case.
 */
int status_find_device(char *out, size_t out_size);

/*
 * Pick the device node belonging to ONE specific printer, by its USB serial.
 *
 * This is what makes the status follow the printer the user actually selected.
 * CUPS addresses printers by backend URI (usb://EPSON/L1800 Series?serial=…)
 * while this code addresses them by /dev/usb/lpN, and the two have nothing in
 * common: node numbers are assigned in plug order. The IEEE-1284 device ID
 * would be the obvious bridge but carries no serial on this model — sysfs does,
 * and its value is byte-identical to the one in the CUPS URI.
 *
 * Returns 0 and fills `out`, or -1 when no attached printer has that serial.
 */
int status_find_device_by_serial(const char *serial, char *out, size_t out_size);

/* Parse a status reply buffer — both the text "@BDC ST\r\nST:04;" form and the
 * binary "@BDC ST2" one, always taking the LAST reply present. Exposed so the
 * D4 fallback can reuse it. Returns the status code or -1. */
int status_parse_buffer(const unsigned char *buf, size_t n);

#endif /* STATUS_H */
