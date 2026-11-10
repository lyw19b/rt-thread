/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    add memory LCD and queued remote pointer
 */

/* Match the existing checked NovaUIOS display service without exposing a new
 * userspace ABI. Its only writer stages into the back buffer, then calls PAN.
 * PAN and snapshot share a mutex, so network snapshots never see partial frames.
 * Buffer storage lives for the device lifetime; each client owns its own copy.
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/dev_touch.h>
#include "vnc_display.h"

#define VNC_POINTER_QUEUE_SIZE 32U

static struct rt_device lcd_device, mouse_device;
static struct rt_mutex frame_lock, pointer_lock;
static uint32_t *framebuffers, *front_frame;
static uint64_t frame_sequence;
static int ready;
struct queued_pointer
{
    struct rt_touch_data data;
    rt_bool_t transition;
};
static struct queued_pointer pointer_queue[VNC_POINTER_QUEUE_SIZE];
static unsigned pointer_head, pointer_count;
static uint16_t pointer_x, pointer_y;
static int pointer_pressed;
static int pointer_blocked;

static rt_err_t lcd_control(rt_device_t device, int command, void *argument)
{
    struct rt_device_graphic_info *info = argument;

    (void)device;
    switch (command)
    {
    case RTGRAPHIC_CTRL_GET_INFO:
        if (!info)
            return -RT_EINVAL;
        rt_memset(info, 0, sizeof(*info));
        info->pixel_format = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
        info->bits_per_pixel = 32;
        info->width = LCD_WIDTH;
        info->height = LCD_HEIGHT;
        info->pitch = LCD_WIDTH * 4;
        info->smem_len = VNC_FRAME_BYTES * 2;
        info->framebuffer = (rt_uint8_t *)framebuffers;
        return RT_EOK;

    case RTGRAPHIC_CTRL_PAN_DISPLAY:
        if (argument != framebuffers && argument != (uint8_t *)framebuffers + VNC_FRAME_BYTES)
            return -RT_EINVAL;
        rt_mutex_take(&frame_lock, RT_WAITING_FOREVER);
        front_frame = argument;
        frame_sequence++;
        rt_mutex_release(&frame_lock);
        return RT_EOK;

    case VNC_DISPLAY_COMMIT_REGION:
    {
        const struct vnc_display_region *region = argument;
        uint32_t row;

        if (!region || !region->pixels || !region->width || !region->height ||
            region->x >= LCD_WIDTH || region->y >= LCD_HEIGHT ||
            region->width > LCD_WIDTH - region->x || region->height > LCD_HEIGHT - region->y)
            return -RT_EINVAL;
        /* Commit only after the service has staged every user row. Snapshot
         * uses this same mutex, so partial updates cannot tear on the wire.
         */
        rt_mutex_take(&frame_lock, RT_WAITING_FOREVER);
        for (row = 0; row < region->height; row++)
        {
            rt_memcpy(front_frame + (size_t)(region->y + row) * LCD_WIDTH + region->x,
                       region->pixels + (size_t)row * region->width, region->width * 4U);
        }
        frame_sequence++;
        rt_mutex_release(&frame_lock);
        return RT_EOK;
    }

    default:
        return -RT_ENOSYS;
    }
}

static rt_err_t mouse_control(rt_device_t device, int command, void *argument)
{
    struct rt_touch_info *info = argument;

    (void)device;
    if (command != RT_TOUCH_CTRL_GET_INFO || !info)
        return -RT_EINVAL;
    rt_memset(info, 0, sizeof(*info));
    info->point_num = 1;
    info->range_x = LCD_WIDTH;
    info->range_y = LCD_HEIGHT;
    return RT_EOK;
}

static rt_ssize_t mouse_read(rt_device_t device, rt_off_t position, void *buffer, rt_size_t count)
{
    rt_ssize_t copied = 0;

    (void)device;
    (void)position;
    if (!buffer || !count)
        return 0;

    rt_mutex_take(&pointer_lock, RT_WAITING_FOREVER);
    if (pointer_count)
    {
        *(struct rt_touch_data *)buffer = pointer_queue[pointer_head].data;
        pointer_head = (pointer_head + 1) % VNC_POINTER_QUEUE_SIZE;
        pointer_count--;
        copied = 1;
    }
    rt_mutex_release(&pointer_lock);
    return copied;
}

static void enqueue_pointer(uint8_t event, uint16_t x, uint16_t y, rt_bool_t transition)
{
    unsigned tail = (pointer_head + pointer_count) % VNC_POINTER_QUEUE_SIZE;

    pointer_queue[tail].data = (struct rt_touch_data){
        .event = event, .x_coordinate = x, .y_coordinate = y,
        .timestamp = rt_tick_get(), .track_id = 0,
    };
    pointer_queue[tail].transition = transition;
    pointer_count++;
}

void vnc_display_pointer(uint16_t x, uint16_t y, int pressed)
{
    uint8_t event;

    if (!ready || x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return;
    pressed = pressed != 0;
    rt_mutex_take(&pointer_lock, RT_WAITING_FOREVER);
    if (pointer_blocked)
    {
        pointer_blocked = pressed;
        pointer_pressed = pressed;
        rt_mutex_release(&pointer_lock);
        return;
    }
    if (x == pointer_x && y == pointer_y && pressed == pointer_pressed)
    {
        rt_mutex_release(&pointer_lock);
        return;
    }
    event = !pressed ? RT_TOUCH_EVENT_UP :
        (pointer_pressed ? RT_TOUCH_EVENT_MOVE : RT_TOUCH_EVENT_DOWN);

    /* Coalesce both hover and drag motion. Neither may build a backlog ahead
     * of a click. Keep every DOWN/UP transition, including its exact position:
     * replacing a release with later hover coordinates can cancel the click.
     */
    if (pointer_count &&
        !pointer_queue[(pointer_head + pointer_count - 1) % VNC_POINTER_QUEUE_SIZE].transition)
    {
        pointer_count--;
    }
    if (pointer_count == VNC_POINTER_QUEUE_SIZE)
    {
        /* A stalled consumer must not leave a stuck press. Resynchronize with
         * release; suppress a held press until the viewer actually releases.
         */
        pointer_head = pointer_count = 0;
        enqueue_pointer(RT_TOUCH_EVENT_UP, pointer_x, pointer_y, RT_TRUE);
        pointer_x = x;
        pointer_y = y;
        pointer_pressed = pressed;
        pointer_blocked = pressed;
        rt_mutex_release(&pointer_lock);
        return;
    }
    enqueue_pointer(event, x, y, pressed != pointer_pressed);
    pointer_x = x;
    pointer_y = y;
    pointer_pressed = pressed;
    rt_mutex_release(&pointer_lock);
}

void vnc_display_release_pointer(void)
{
    if (!ready)
        return;
    rt_mutex_take(&pointer_lock, RT_WAITING_FOREVER);
    pointer_head = pointer_count = 0;
    pointer_pressed = 0;
    pointer_blocked = 0;
    enqueue_pointer(RT_TOUCH_EVENT_UP, pointer_x, pointer_y, RT_TRUE);
    rt_mutex_release(&pointer_lock);
}

static rt_err_t mouse_open(rt_device_t device, rt_uint16_t flags)
{
    (void)device;
    (void)flags;
    vnc_display_release_pointer();
    return RT_EOK;
}

static rt_err_t mouse_close(rt_device_t device)
{
    (void)device;
    vnc_display_release_pointer();
    return RT_EOK;
}

int vnc_display_ready(void)
{
    return ready;
}

void vnc_display_snapshot(uint32_t *pixels, uint64_t *sequence)
{
    rt_mutex_take(&frame_lock, RT_WAITING_FOREVER);
    rt_memcpy(pixels, front_frame, VNC_FRAME_BYTES);
    *sequence = frame_sequence;
    rt_mutex_release(&frame_lock);
}

uint64_t vnc_display_sequence(void)
{
    uint64_t sequence;

    rt_mutex_take(&frame_lock, RT_WAITING_FOREVER);
    sequence = frame_sequence;
    rt_mutex_release(&frame_lock);
    return sequence;
}

static const struct rt_device_ops lcd_ops = {.control = lcd_control};
static const struct rt_device_ops mouse_ops = {
    .open = mouse_open, .close = mouse_close, .read = mouse_read, .control = mouse_control,
};

static int vnc_display_init(void)
{
    int result;

    if (rt_device_find("lcd") || rt_device_find("mouse") || rt_device_find("touch"))
    {
        rt_kprintf("VNC: headless mode conflicts with an existing display/pointer\n");
        return -RT_EBUSY;
    }
    framebuffers = rt_calloc(2, VNC_FRAME_BYTES);
    if (!framebuffers)
        return -RT_ENOMEM;
    front_frame = framebuffers;
    result = rt_mutex_init(&frame_lock, "vnc-frame", RT_IPC_FLAG_PRIO);
    if (result)
        goto free_buffers;
    result = rt_mutex_init(&pointer_lock, "vnc-pointer", RT_IPC_FLAG_PRIO);
    if (result)
        goto detach_frame;

    lcd_device.type = RT_Device_Class_Graphic;
    lcd_device.ops = &lcd_ops;
    mouse_device.type = RT_Device_Class_Touch;
    mouse_device.ops = &mouse_ops;
    result = rt_device_register(&lcd_device, "lcd", RT_DEVICE_FLAG_RDWR);
    if (result)
        goto detach_pointer;
    /* The RFB viewer draws its own cursor immediately. Register as touch so
     * NovaUIOS does not burn a second, network-delayed cursor into the pixels.
     */
    result = rt_device_register(&mouse_device, "touch", RT_DEVICE_FLAG_RDONLY);
    if (result)
    {
        rt_device_unregister(&lcd_device);
        goto detach_pointer;
    }
    ready = 1;
    rt_kprintf("VNC: memory display %ux%u, no physical LCD required\n", LCD_WIDTH, LCD_HEIGHT);
    return RT_EOK;

detach_pointer:
    rt_mutex_detach(&pointer_lock);
detach_frame:
    rt_mutex_detach(&frame_lock);
free_buffers:
    rt_free(framebuffers);
    framebuffers = front_frame = RT_NULL;
    return result;
}

INIT_DEVICE_EXPORT(vnc_display_init);
