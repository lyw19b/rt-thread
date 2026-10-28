/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               add bounded native xHCI mouse transport
 */

/*
 * Description:
 *   Adapt native polling xHCI mice to the existing RT-Thread pointer interface.
 */

#include <rtdevice.h>
#include <rthw.h>
#include <drivers/dev_touch.h>
#include "board.h"
#include "ls2k3000_pci.h"
#include "xhci_mouse.h"
#include "mouse_pointer.h"

#define MOUSE_HOSTS 2U

struct mouse_host
{
    struct ls_xhci controller;
    const struct ls2k3000_pci_device *pci;
    rt_ubase_t registers;
    rt_tick_t previous_tick;
    uint64_t elapsed_ticks;
    uint32_t buttons;
    unsigned int announced;
    int last_result;
};

static struct mouse_host hosts[MOUSE_HOSTS];
static struct rt_touch_device mouse;
static struct rt_spinlock point_lock;
static struct rt_mutex host_lock;
static struct ls_mouse_pointer pointer;
static unsigned int read_pressed;
static int64_t wheel_total;
static uint32_t buttons;
static unsigned int host_count;
static unsigned int registered;

static uint32_t hardware_read(void *context, uint32_t offset)
{
    struct mouse_host *host = context;

    uint32_t value;

    rt_hw_dmb();
    value = *(volatile uint32_t *)(host->registers + offset);
    rt_hw_dmb();
    return value;
}

static void hardware_write(void *context, uint32_t offset, uint32_t value)
{
    struct mouse_host *host = context;

    *(volatile uint32_t *)(host->registers + offset) = value;
    rt_hw_dmb();
}

static void hardware_barrier(void *context)
{
    RT_UNUSED(context);
    rt_hw_dmb();
}

static uint64_t hardware_time(void *context)
{
    struct mouse_host *host = context;
    rt_tick_t now = rt_tick_get();

    host->elapsed_ticks += (rt_tick_t)(now - host->previous_tick);
    host->previous_tick = now;
    return host->elapsed_ticks * 1000 / RT_TICK_PER_SECOND;
}

static void hardware_sleep(void *context, unsigned int milliseconds)
{
    RT_UNUSED(context);
    rt_thread_mdelay(milliseconds);
}

static void report_motion(void *context, const struct ls_mouse_motion *motion, int connected)
{
    struct mouse_host *host = context;
    rt_base_t level = rt_spin_lock_irqsave(&point_lock);

    host->buttons = connected ? (host->buttons & ~motion->button_mask) | motion->buttons : 0;
    buttons = 0;

    for (unsigned int index = 0; index < host_count; ++index)
    {
        buttons |= hosts[index].buttons;
    }

    ls_mouse_pointer_update(&pointer, motion->x, motion->y, buttons & 1);

    if (!connected && !(buttons & 1))
    {
        ls_mouse_pointer_release(&pointer);
    }

    wheel_total += motion->wheel;
    rt_spin_unlock_irqrestore(&point_lock, level);
}

static const struct ls_xhci_ops hardware_ops = {
    .read32 = hardware_read,
    .write32 = hardware_write,
    .barrier = hardware_barrier,
    .now_ms = hardware_time,
    .sleep_ms = hardware_sleep,
    .motion = report_motion,
};

static rt_size_t read_point(struct rt_touch_device *device, void *buffer, rt_size_t count)
{
    rt_base_t level;

    RT_UNUSED(device);

    if (!buffer || !count)
    {
        return 0;
    }

    level = rt_spin_lock_irqsave(&point_lock);
    struct ls_mouse_point current = ls_mouse_pointer_read(&pointer);
    struct rt_touch_data *sample = buffer;

    rt_memset(sample, 0, sizeof(*sample));
    sample->x_coordinate = current.x;
    sample->y_coordinate = current.y;
    sample->timestamp = rt_tick_get();
    sample->event = !current.pressed ? RT_TOUCH_EVENT_UP :
                    read_pressed ? RT_TOUCH_EVENT_MOVE : RT_TOUCH_EVENT_DOWN;
    read_pressed = current.pressed;

    rt_spin_unlock_irqrestore(&point_lock, level);
    return 1;
}

static rt_err_t mouse_control(struct rt_touch_device *device, int command, void *argument)
{
    if (command == RT_TOUCH_CTRL_GET_INFO)
    {
        if (!argument)
        {
            return -RT_EINVAL;
        }

        rt_memcpy(argument, &device->info, sizeof(device->info));
        return RT_EOK;
    }

    return -RT_ENOSYS;
}

static const struct rt_touch_ops mouse_ops = {
    .touch_readpoint = read_point,
    .touch_control = mouse_control,
};

static void mouse_thread(void *argument)
{
    RT_UNUSED(argument);

    for (;;)
    {
        for (unsigned int index = 0; index < host_count; ++index)
        {
            struct mouse_host *host = &hosts[index];
            struct ls_xhci *controller = &host->controller;

            rt_mutex_take(&host_lock, RT_WAITING_FOREVER);

            if (controller->running)
            {
                int result = ls_xhci_poll(controller);

                if (result)
                {
                    host->last_result = result;
                    rt_kprintf("xHCI%u: port=%u result=%d command_code=%u control_code=%u\n",
                               index, controller->port, result,
                               controller->command_code, controller->control_code);
                }
            }

            if (host->announced != controller->connected)
            {
                host->announced = controller->connected;
                rt_kprintf("xHCI%u: mouse %s port=%u id=%04x:%04x endpoint=%u packet=%u\n",
                           index, controller->connected ? "connected" : "released",
                           controller->port, controller->vendor, controller->product,
                           controller->endpoint, controller->packet_bytes);
            }

            rt_mutex_release(&host_lock);
            rt_thread_mdelay(1);
        }

        rt_thread_mdelay(1);
    }
}

static int mouse_init(void)
{
    const struct ls2k3000_framebuffer *framebuffer = &ls2k3000_boot->framebuffer;
    rt_thread_t thread;
    int result;
    unsigned int running_count = 0;

    if (!framebuffer->width || !framebuffer->height || framebuffer->width > UINT16_MAX ||
        framebuffer->height > UINT16_MAX)
    {
        return -RT_EINVAL;
    }

    rt_spin_lock_init(&point_lock);
    result = rt_mutex_init(&host_lock, "xhci", RT_IPC_FLAG_PRIO);

    if (result)
    {
        return result;
    }

    mouse.info.type = RT_TOUCH_TYPE_NONE;
    mouse.info.vendor = RT_TOUCH_VENDOR_UNKNOWN;
    mouse.info.point_num = 1;
    mouse.info.range_x = framebuffer->width;
    mouse.info.range_y = framebuffer->height;
    mouse.ops = &mouse_ops;
    ls_mouse_pointer_init(&pointer, framebuffer->width, framebuffer->height);

    for (unsigned int index = 0; index < MOUSE_HOSTS; ++index)
    {
        const struct ls2k3000_pci_device *pci = ls2k3000_pci_find(0x0c0330, index);
        struct mouse_host *host;
        struct ls_xhci *controller;
        const char *stage = "PCI command";

        if (!pci || (pci->id != 0x7a440014 && pci->id != 0x7a340014))
        {
            continue;
        }

        host = &hosts[host_count];
        controller = &host->controller;
        host->pci = pci;
        result = ls2k3000_pci_map(pci, 0, 0x1000, &host->registers);

        if (result)
        {
            continue;
        }

        controller->dma = ls2k3000_dma_alloc(sizeof(*controller->dma), 4096,
                                             &controller->dma_address);

        if (!controller->dma)
        {
            rt_kprintf("xHCI: permanent DMA pool exhausted\n");
            break;
        }

        controller->ops = &hardware_ops;
        controller->context = host;
        controller->bar_bytes = pci->bar[0].bytes;
        /* Integrated functions need not implement PCI INTX_DISABLE. Polling
         * disables xHCI USBCMD.INTE and IMAN.IE instead; require only MEM/BME. */
        result = ls2k3000_pci_command(pci, 6, 0);

        if (!result)
        {
            stage = "HCD";
            result = ls_xhci_start(controller);
        }

        if (result)
        {
            rt_kprintf("xHCI: %02x:%02x.%u init stage=%s failed=%d quarantine=%u; DMA retained\n",
                       pci->bus, pci->devfn >> 3, pci->devfn & 7,
                       stage, result, controller->quarantined);
            ls2k3000_pci_command(pci, 0, 4);
        }
        else
        {
            running_count++;
            rt_kprintf("xHCI%u: %02x:%02x.%u ports=%u context=%u DMA=%lx\n",
                       host_count, pci->bus, pci->devfn >> 3, pci->devfn & 7,
                       controller->ports, controller->context_bytes,
                       (unsigned long)controller->dma_address);
        }

        /* Keep failed hosts separate too: no quarantined arena or state is reused. */
        host_count++;
    }

    if (!running_count)
    {
        rt_mutex_detach(&host_lock);
        return -RT_ENOSYS;
    }

    thread = rt_thread_create("usb_mouse", mouse_thread, RT_NULL, 8192, 24, 5);

    if (!thread)
    {
        result = -RT_ENOMEM;
        goto failure;
    }

    result = rt_hw_touch_register(&mouse, "mouse", RT_DEVICE_FLAG_RDONLY, RT_NULL);

    if (result)
    {
        rt_thread_delete(thread);
        goto failure;
    }

    registered = 1;
    rt_thread_startup(thread);
    return RT_EOK;

failure:
    for (unsigned int index = 0; index < host_count; ++index)
    {
        if (hosts[index].controller.running)
        {
            ls_xhci_stop(&hosts[index].controller);
        }

        ls2k3000_pci_command(hosts[index].pci, 0, 4);
    }

    rt_mutex_detach(&host_lock);
    return result;
}

INIT_DEVICE_EXPORT(mouse_init);

static long ls2k3000_mouse_status(void)
{
    struct mouse_status
    {
        unsigned int running;
        unsigned int connected;
        unsigned int slot;
        unsigned int port;
        unsigned int vendor;
        unsigned int product;
        unsigned int quarantine;
        int last_result;
        uint64_t reports;
        uint64_t errors;
        uint64_t timeouts;
        uint64_t malformed;
    } status[MOUSE_HOSTS];
    struct ls_mouse_point current;
    uint32_t current_buttons;
    int64_t current_wheel;
    uint64_t overflows;
    rt_base_t level;

    if (!registered)
    {
        rt_kprintf("mouse: not registered\n");
        return -RT_ENOSYS;
    }

    level = rt_spin_lock_irqsave(&point_lock);
    current = pointer.current;
    overflows = pointer.overflows;
    current_buttons = buttons;
    current_wheel = wheel_total;
    rt_spin_unlock_irqrestore(&point_lock, level);
    rt_mutex_take(&host_lock, RT_WAITING_FOREVER);

    for (unsigned int index = 0; index < host_count; ++index)
    {
        const struct ls_xhci *host = &hosts[index].controller;

        status[index] = (struct mouse_status){
            host->running, host->connected, host->slot, host->port,
            host->vendor, host->product, host->quarantined, hosts[index].last_result,
            host->reports, host->errors, host->timeouts, host->malformed,
        };
    }

    rt_mutex_release(&host_lock);
    rt_kprintf("mouse: x=%u y=%u buttons=%x wheel=%ld range=%dx%d edge_overflow=%lu\n",
               current.x, current.y, current_buttons, (long)current_wheel,
               mouse.info.range_x, mouse.info.range_y, (unsigned long)overflows);

    for (unsigned int index = 0; index < host_count; ++index)
    {
        const struct mouse_status *host = &status[index];

        rt_kprintf("xHCI%u: running=%u connected=%u slot=%u port=%u id=%04x:%04x "
                   "reports=%lu errors=%lu timeouts=%lu malformed=%lu quarantine=%u last_result=%d\n",
                   index, host->running, host->connected, host->slot, host->port,
                   host->vendor, host->product, (unsigned long)host->reports,
                   (unsigned long)host->errors, (unsigned long)host->timeouts,
                   (unsigned long)host->malformed, host->quarantine, host->last_result);
    }

    return 0;
}

MSH_CMD_EXPORT(ls2k3000_mouse_status, Show native USB mouse and xHCI status);
