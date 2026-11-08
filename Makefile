# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Didik DW and contributors.
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -MMD -MP
LDFLAGS = -lpng -lm -llcms2

SRC_DIR = src
BUILD_DIR = build
TARGET  = dwrip

# --- libm.so shim ------------------------------------------------------------
# Some glibc packagings (e.g. Ubuntu after a 2.39 update) ship libm.so.6 but
# drop the linker dev symlink /usr/lib/.../libm.so, so `-lm` falls back to the
# static libm.a and the link fails (undefined _dl_x86_cpu_features). If no
# system libm.so is found, create a local shared shim and link against it.
# No-op on healthy systems → build stays byte-identical there.
LIBM_SO := $(firstword $(wildcard /usr/lib/x86_64-linux-gnu/libm.so /usr/lib64/libm.so /usr/lib/libm.so))
ifeq ($(LIBM_SO),)
  LIBM_REAL := $(shell $(CC) -print-file-name=libm.so.6)
  LIBM_SHIM := $(BUILD_DIR)/libm-shim
  LDFLAGS   := -L$(LIBM_SHIM) $(LDFLAGS)
endif

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/packbits.c \
       $(SRC_DIR)/prn_gen.c \
       $(SRC_DIR)/halftone.c \
       $(SRC_DIR)/channel.c \
       $(SRC_DIR)/resolution.c \
       $(SRC_DIR)/image.c \
       $(SRC_DIR)/status.c \
       $(SRC_DIR)/d4.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS) | libm-shim
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: libm-shim
libm-shim:
ifneq ($(LIBM_SHIM),)
	@mkdir -p $(LIBM_SHIM)
	@[ -e $(LIBM_SHIM)/libm.so ] || ln -sf $(LIBM_REAL) $(LIBM_SHIM)/libm.so
endif

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Auto-generated header dependencies (gcc -MMD)
-include $(OBJS:.o=.d)

# Quick test: generate PRN from test pattern
test: $(TARGET)
	./$(TARGET) -t 568x227 -o test_output.prn -c YKWWMC
	@echo "Test PRN generated: test_output.prn"
	@ls -la test_output.prn
