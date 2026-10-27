/*
 * Copyright (C) 2026 OpenLANPU Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-25     OpenLANPU Contributors    add bounded high-address USB host support
 *
 */

/* Description: Exact-length USB mass-storage BOT transactions with CSW validation. */
#ifndef LS2K_USB_BOT_H
#define LS2K_USB_BOT_H

#include <stdbool.h>
#include <stdint.h>

struct ls2k_usb_bot
{
    void *context;
    int (*transfer)(void *context, bool input, uint8_t *buffer, uint32_t length);
    uint32_t tag;
    bool fault;
};

int ls2k_usb_bot_command(struct ls2k_usb_bot *bot, const uint8_t *cdb,
                         uint8_t cdb_length, bool input, void *buffer, uint32_t length);

#endif
