# Copyright (C) 2020-2026 Loongson Technology Corporation Limited
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author                    Notes
# 2026-10-27     LoongsonLab               standardize imported build file

# Description:
#   Build the imported ERESTART user-mode example.

# Makefile for ERESTART test (user-mode ELF for rt-thread-smart/loongarch64)
#
# Usage:
#   make CROSS=loongarch64-linux-musl-
#   # then load test_erestart on the board via the LWP exec command

CROSS   ?= loongarch64-linux-musl-
CC      := $(CROSS)gcc
CFLAGS  := -static -O2 -Wall -Wextra
TARGET  := test_erestart

all: $(TARGET)

$(TARGET): test_erestart.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
