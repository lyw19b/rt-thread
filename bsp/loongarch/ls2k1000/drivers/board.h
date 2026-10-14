#ifndef __LS2K_BOARD_H__
#define __LS2K_BOARD_H__

#include <rtconfig.h>

extern unsigned int __bss_start;
extern unsigned int __bss_end;
extern unsigned char _end[];

#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0x0
#endif

#define RT_HW_HEAP_SIZE  64 * 1024 * 1024
#define RT_HW_HEAP_BEGIN ((void *)_end)
#define RT_HW_HEAP_END   ((void *)((char *)RT_HW_HEAP_BEGIN + RT_HW_HEAP_SIZE))

void rt_hw_board_init(void);

#endif // __LS2K_BOARD_H__
