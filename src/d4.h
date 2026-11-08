/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#ifndef D4_H
#define D4_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal IEEE-1284.4 ("D4") client — only what reading the printer's EEPROM
 * needs, nothing more.
 *
 * Why this exists: the EEPROM read command (`||`) is served ONLY on the D4
 * control channel EPSON-CTRL. Plain ESC/P — the path src/status.c uses — is
 * ignored for it; verified in three framings against the printer.
 *
 * Protocol layout follows Gutenprint's d4lib.c (the reference implementation
 * for this transport). Control commands carry a 7-byte header:
 *     psid | ssid | lengthH | lengthL | credit | control | command
 * data-channel frames carry the same minus the command byte.
 * Lengths are big-endian and count the header itself.
 *
 * ⚠️ Entering D4 changes a GLOBAL printer mode that outlives this process:
 * afterwards the printer ignores plain ESC/P until it leaves D4 again (WIC
 * Reset Utility leaves printers this way — see docs/analisis_cleaning_wic.md
 * §d4mode). d4_close() sends the proper Exit for that reason; always pair it
 * with d4_open(), including on error paths.
 */

typedef struct {
  int fd;
  unsigned char socket;   /* EPSON-CTRL socket id from GetSocketID */
  int send_size;          /* negotiated packet sizes */
  int recv_size;
  int credit;             /* our remaining allowance to send */
} d4_session_t;

/* Open the device and bring up a D4 session on EPSON-CTRL.
 * Returns 0 on success; on failure nothing is left open. */
int d4_open(const char *device, d4_session_t *s);

/* Close the channel, leave D4, close the device. Safe to call once after a
 * successful d4_open(). */
void d4_close(d4_session_t *s);

/* Send one service command (e.g. the `||` EEPROM read) and collect the reply
 * payload. Returns the number of payload bytes, or -1. */
int d4_command(d4_session_t *s, const uint8_t *cmd, size_t len,
               uint8_t *reply, size_t reply_cap);

/* Ask for printer status over D4 (the "st" command). Fills `reply` with the
 * raw answer — normally the binary "@BDC ST2" form. Returns bytes read or -1.
 * Used as the fallback when plain ESC/P gets no answer because the printer has
 * been left in D4 mode. */
int d4_status(d4_session_t *s, uint8_t *reply, size_t cap);

/* Read one EEPROM byte. Returns 0 and stores the value, or -1.
 * Sends `|| 51 05 41 BE A0 <addr> 00` and parses "@BDC PS\r\nEE:00<aa><vv>;". */
int d4_read_eeprom(d4_session_t *s, uint8_t addr, uint8_t *value);

/*
 * Write one EEPROM byte. Returns 0 if the printer accepted the command, -1 if
 * it refused it.
 *
 * ⚠️ On the L1800 this currently always refuses: the printer parses the command
 * (it echoes the command code) and answers "||:42:NA;" — write protection. The
 * bytes below are correct as far as they go; what is missing is whatever
 * unlocks writes, which WIC must send before its reset. See
 * docs/analisis_cleaning_wic.md §waste-reset.
 *
 * ⚠️ IRREVERSIBLE. The EEPROM also holds head calibration, the head ID and the
 * serial number; a write to the wrong address can ruin the printer for good.
 * Nothing in DWRip exposes this as a general "write any address" operation,
 * and nothing should: the only caller is the waste-counter reset, which writes
 * two addresses that have been confirmed twice against WIC's own readings.
 *
 * Command layout (derived from ReInk's formula, and it reproduces our observed
 * READ command byte for byte):
 *     7C 7C 08 00 | 51 05 | 42 BD 21 | addr_lo addr_hi | value
 *     class  len    model   cmd 0x42   address           data
 * where cmd1 = ~cmd and cmd2 = ((cmd>>1)&0x7F)|((cmd<<7)&0x80). The model code
 * is what authenticates the request — there is no password or checksum.
 */
int d4_write_eeprom(d4_session_t *s, uint8_t addr, uint8_t value);

#endif /* D4_H */
