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

/*
 * Description: Serialize CBW/data/CSW at the caller; reject incomplete disk reads.
 * Notes: Protocol/transport failure quarantines this session until replug.
 *        A valid SCSI CHECK CONDITION allows REQUEST SENSE and retry.
 */
#include "ls2k_usb_bot.h"
#include "usb_errno.h"
#include <string.h>

static uint32_t get_le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i)
    {
        p[i] = value >> (i * 8);
    }
}

int ls2k_usb_bot_command(struct ls2k_usb_bot *bot, const uint8_t *cdb,
                         uint8_t cdb_length, bool input, void *buffer, uint32_t length)
{
    uint8_t cbw[31] = {0};
    uint8_t csw[13] = {0};
    int ret;

    if (!bot || !bot->transfer || !cdb || !cdb_length || cdb_length > 16 ||
        (length && !buffer) || length > 0x7fffffff)
    {
        return -USB_ERR_INVAL;
    }

    if (bot->fault)
    {
        return -USB_ERR_IO;
    }

    put_le32(cbw, 0x43425355);
    put_le32(cbw + 4, ++bot->tag);
    put_le32(cbw + 8, length);
    cbw[12] = input ? 0x80 : 0;
    cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);
    ret = bot->transfer(bot->context, false, cbw, sizeof(cbw));
    if (ret != sizeof(cbw))
    {
        goto failed;
    }

    if (length)
    {
        ret = bot->transfer(bot->context, input, buffer, length);
        if (ret < 0 || (uint32_t)ret != length)
        {
            goto failed;
        }
    }

    ret = bot->transfer(bot->context, true, csw, sizeof(csw));
    if (ret != sizeof(csw) || get_le32(csw) != 0x53425355 ||
        get_le32(csw + 4) != bot->tag || get_le32(csw + 8) != 0 || csw[12] > 1)
    {
        goto failed;
    }

    return csw[12] ? -USB_ERR_STALL : 0;

failed:
    bot->fault = true;
    return ret < 0 ? ret : -USB_ERR_IO;
}
