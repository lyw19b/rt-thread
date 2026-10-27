/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: hid_mouse.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Validate descriptor items before reading fields; preserve independent report buttons.
 */

#include "hid_mouse.h"
#include <string.h>

#define FIELD_X 0x30U
#define FIELD_Y 0x31U
#define FIELD_WHEEL 0x38U
#define LOCAL_USAGES 32U
#define COLLECTION_DEPTH 16U
#define GLOBAL_DEPTH 4U

struct hid_global
{
    uint32_t page;
    uint32_t size;
    uint32_t count;
    int32_t minimum;
    uint8_t id;
};

struct hid_local
{
    uint32_t usage[LOCAL_USAGES];
    uint32_t minimum;
    uint32_t maximum;
    unsigned int count;
    unsigned int range;
};

static uint32_t integer(const uint8_t *data, unsigned int bytes)
{
    uint32_t value = 0;

    for (unsigned int index = 0; index < bytes; ++index)
    {
        value |= (uint32_t)data[index] << (index * 8);
    }

    return value;
}

static int32_t signed_value(uint32_t value, unsigned int bits)
{
    if (bits < 32 && (value & (1U << (bits - 1))))
    {
        return (int32_t)((int64_t)value - (INT64_C(1) << bits));
    }

    return (int32_t)value;
}

static uint32_t usage_at(const struct hid_local *local, unsigned int index)
{
    if (index < local->count)
    {
        return local->usage[index];
    }

    if (local->range == 3 && local->maximum >= local->minimum &&
        index <= local->maximum - local->minimum)
    {
        return local->minimum + index;
    }

    return UINT32_MAX;
}

static int input_item(struct ls_mouse_layout *layout, const struct hid_global *global,
                       const struct hid_local *local, uint32_t flags, int mouse)
{
    unsigned int report;
    unsigned int offset;

    if (!global->size || global->size > 32 || !global->count || global->count > 256 ||
        global->size * global->count > LS_MOUSE_REPORT_BYTES * 8)
    {
        return -1;
    }

    for (report = 0; report < layout->reports; ++report)
    {
        if (layout->id[report] == global->id)
        {
            break;
        }
    }

    if (report == layout->reports)
    {
        if (report == LS_MOUSE_REPORTS)
        {
            return -1;
        }

        layout->id[report] = global->id;
        layout->reports++;
    }

    offset = layout->bits[report];

    if (global->size * global->count > LS_MOUSE_REPORT_BYTES * 8 - offset)
    {
        return -1;
    }

    layout->bits[report] += global->size * global->count;

    if (!mouse || (flags & 1U) || !(flags & 2U))
    {
        return 0;
    }

    for (unsigned int index = 0; index < global->count; ++index)
    {
        uint32_t usage = usage_at(local, index);
        uint32_t page = global->page;
        struct ls_mouse_field *field;

        if (usage == UINT32_MAX)
        {
            continue;
        }

        if (usage >> 16)
        {
            page = usage >> 16;
            usage &= 0xffff;
        }

        if (page == 1 && (usage == FIELD_X || usage == FIELD_Y || usage == FIELD_WHEEL))
        {
            if (!(flags & 4U) || global->size > 16 || global->minimum >= 0)
            {
                return -1;
            }
        }
        else if (page == 9 && usage >= 1 && usage <= 8 && global->size == 1)
        {
            /* Button fields use their usage number as the internal kind. */
        }
        else
        {
            continue;
        }

        if (layout->fields == LS_MOUSE_FIELDS)
        {
            return -1;
        }

        field = &layout->field[layout->fields++];
        field->offset = offset + index * global->size;
        field->bits = global->size;
        field->kind = usage;
        field->report = report;
        field->is_signed = global->minimum < 0;
    }

    return 0;
}

int ls_mouse_parse(struct ls_mouse_layout *layout, const uint8_t *data, size_t bytes)
{
    struct ls_mouse_layout parsed = {0};
    struct hid_global global = {0};
    struct hid_global stack[GLOBAL_DEPTH];
    struct hid_local local = {0};
    uint8_t collection[COLLECTION_DEPTH];
    unsigned int depth = 0;
    unsigned int globals = 0;
    unsigned int axes = 0;
    size_t offset = 0;

    if (!layout || !data || !bytes || bytes > 4096)
    {
        return -1;
    }

    while (offset < bytes)
    {
        uint8_t prefix = data[offset++];
        unsigned int size = prefix & 3U;
        unsigned int type = (prefix >> 2) & 3U;
        unsigned int tag = prefix >> 4;
        uint32_t value;

        size = size == 3 ? 4 : size;

        if (prefix == 0xfe || size > bytes - offset)
        {
            return -1;
        }

        value = integer(data + offset, size);
        offset += size;

        if (type == 1)
        {
            switch (tag)
            {
            case 0:
                global.page = value;
                break;
            case 1:
                if (!size)
                {
                    return -1;
                }

                global.minimum = signed_value(value, size * 8);
                break;
            case 7:
                global.size = value;
                break;
            case 8:
                if (!value || value > 255)
                {
                    return -1;
                }

                global.id = value;
                parsed.numbered = 1;
                break;
            case 9:
                global.count = value;
                break;
            case 10:
                if (globals == GLOBAL_DEPTH)
                {
                    return -1;
                }

                stack[globals++] = global;
                break;
            case 11:
                if (!globals)
                {
                    return -1;
                }

                global = stack[--globals];
                break;
            default:
                break;
            }
        }
        else if (type == 2)
        {
            if (tag == 0)
            {
                if (local.count == LOCAL_USAGES)
                {
                    return -1;
                }

                local.usage[local.count++] = value;
            }
            else if (tag == 1)
            {
                local.minimum = value;
                local.range |= 1;
            }
            else if (tag == 2)
            {
                local.maximum = value;
                local.range |= 2;
            }
            else
            {
                return -1;
            }
        }
        else if (type == 0)
        {
            if (tag == 10)
            {
                uint32_t usage = usage_at(&local, 0);
                int mouse = depth ? collection[depth - 1] : 0;

                if (depth == COLLECTION_DEPTH)
                {
                    return -1;
                }

                if (value == 1)
                {
                    mouse = (global.page == 1 && usage == 2) || usage == 0x10002;
                }

                collection[depth++] = mouse;
            }
            else if (tag == 12)
            {
                if (!depth)
                {
                    return -1;
                }

                depth--;
            }
            else if (tag == 8)
            {
                if (input_item(&parsed, &global, &local, value,
                                depth && collection[depth - 1]))
                {
                    return -1;
                }
            }
            else if (tag != 9 && tag != 11)
            {
                return -1;
            }

            memset(&local, 0, sizeof(local));
        }
        else
        {
            return -1;
        }
    }

    if (depth || globals)
    {
        return -1;
    }

    for (unsigned int index = 0; index < parsed.reports; ++index)
    {
        if (parsed.numbered && !parsed.id[index])
        {
            return -1;
        }
    }

    for (unsigned int index = 0; index < parsed.fields; ++index)
    {
        axes |= parsed.field[index].kind == FIELD_X ? 1U : 0U;
        axes |= parsed.field[index].kind == FIELD_Y ? 2U : 0U;
    }

    if (axes != 3)
    {
        return -1;
    }

    *layout = parsed;
    return 0;
}

int ls_mouse_decode(const struct ls_mouse_layout *layout, const uint8_t *data,
                     size_t bytes, struct ls_mouse_motion *motion)
{
    struct ls_mouse_motion result = {0};
    unsigned int report;
    unsigned int id;

    if (!layout || !data || !motion || !bytes || bytes > LS_MOUSE_REPORT_BYTES + 1 ||
        !layout->reports || layout->reports > LS_MOUSE_REPORTS || layout->fields > LS_MOUSE_FIELDS)
    {
        return -1;
    }

    id = layout->numbered ? *data++ : 0;
    bytes -= !!layout->numbered;

    for (report = 0; report < layout->reports; ++report)
    {
        if (layout->id[report] == id)
        {
            break;
        }
    }

    if (report == layout->reports)
    {
        return 1;
    }

    if (layout->bits[report] > bytes * 8)
    {
        return -1;
    }

    for (unsigned int index = 0; index < layout->fields; ++index)
    {
        const struct ls_mouse_field *field = &layout->field[index];
        uint32_t raw = 0;
        int32_t value;

        if (field->report != report)
        {
            continue;
        }

        if (!field->bits || field->bits > 16 || field->offset + field->bits > bytes * 8)
        {
            return -1;
        }

        for (unsigned int bit = 0; bit < field->bits; ++bit)
        {
            unsigned int position = field->offset + bit;
            raw |= ((data[position / 8] >> (position % 8)) & 1U) << bit;
        }

        value = field->is_signed ? signed_value(raw, field->bits) : (int32_t)raw;

        if (field->kind == FIELD_X)
        {
            result.x += value;
        }
        else if (field->kind == FIELD_Y)
        {
            result.y += value;
        }
        else if (field->kind == FIELD_WHEEL)
        {
            result.wheel += value;
        }
        else if (field->kind >= 1 && field->kind <= 8)
        {
            uint32_t mask = 1U << (field->kind - 1);
            result.button_mask |= mask;
            result.buttons |= raw ? mask : 0;
        }
    }

    *motion = result;
    return 0;
}

void ls_mouse_boot_layout(struct ls_mouse_layout *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->reports = 1;
    layout->bits[0] = 24;
    layout->fields = 5;

    for (unsigned int index = 0; index < 3; ++index)
    {
        layout->field[index] = (struct ls_mouse_field){index, 1, index + 1, 0, 0};
    }

    layout->field[3] = (struct ls_mouse_field){8, 8, FIELD_X, 0, 1};
    layout->field[4] = (struct ls_mouse_field){16, 8, FIELD_Y, 0, 1};
}
