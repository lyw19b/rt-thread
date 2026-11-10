/*
 * Copyright (C) 2026 OpenLANPU Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-11-03     OpenLANPU Contributors    isolate network buffers from the large UI heap
 */

/* The Smart ELF/page cache fragments the system heap into many small blocks.
 * Searching those blocks for each TCP pbuf dominates the actual wire time.
 * Redirect only lwIP's allocator through BSP-local linker wrappers. Other
 * kernel/application allocations retain their existing allocator and ABI.
 * The reserved heap is bounded and lives for the network device lifetime.
 */
#include <rtthread.h>
#include <lwip/mem.h>
#include <stdint.h>

static rt_uint8_t storage[BSP_VNC_NET_HEAP_KIB * 1024U] rt_align(RT_ALIGN_SIZE);
static struct rt_mutex heap_lock;
static rt_smem_t heap;
static uint32_t failed_allocations;

extern void *__real_mem_malloc(mem_size_t size);
extern void *__real_mem_calloc(mem_size_t count, mem_size_t size);
extern void __real_mem_free(void *memory);

void *__wrap_mem_malloc(mem_size_t size)
{
    void *memory;

    if (!heap)
        return __real_mem_malloc(size);
    rt_mutex_take(&heap_lock, RT_WAITING_FOREVER);
    memory = rt_smem_alloc(heap, size);
    if (!memory && size)
        failed_allocations++;
    rt_mutex_release(&heap_lock);
    return memory;
}

void *__wrap_mem_calloc(mem_size_t count, mem_size_t size)
{
    size_t bytes = (size_t)count * size;
    void *memory;

    if (!heap)
        return __real_mem_calloc(count, size);
    if (bytes > (mem_size_t)-1)
        return RT_NULL;
    memory = __wrap_mem_malloc((mem_size_t)bytes);
    if (memory)
        rt_memset(memory, 0, bytes);
    return memory;
}

void __wrap_mem_free(void *memory)
{
    uintptr_t address = (uintptr_t)memory;

    if (!memory)
        return;
    /* Pre-initialization allocations remain owned by the system allocator. */
    if (address < (uintptr_t)storage || address >= (uintptr_t)storage + sizeof(storage))
    {
        __real_mem_free(memory);
        return;
    }
    rt_mutex_take(&heap_lock, RT_WAITING_FOREVER);
    rt_smem_free(memory);
    rt_mutex_release(&heap_lock);
}

void vnc_net_heap_report(void)
{
    if (!heap)
        return;
    rt_mutex_take(&heap_lock, RT_WAITING_FOREVER);
    rt_kprintf("VNC net_heap total=%lu used=%lu peak=%lu failures=%u\n",
        (unsigned long)heap->total, (unsigned long)heap->used,
        (unsigned long)heap->max, failed_allocations);
    rt_mutex_release(&heap_lock);
}

static int vnc_net_heap_init(void)
{
    int result = rt_mutex_init(&heap_lock, "vnc-net-heap", RT_IPC_FLAG_PRIO);

    if (result)
        return result;
    heap = rt_smem_init("vnc-network", storage, sizeof(storage));
    if (!heap)
    {
        rt_mutex_detach(&heap_lock);
        return -RT_ENOMEM;
    }
    return RT_EOK;
}

/* The system heap is already installed; lwIP's INIT_PREV runs afterwards. */
INIT_BOARD_EXPORT(vnc_net_heap_init);
