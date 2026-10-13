#ifndef __LS2K_BOARD_H__
#define __LS2K_BOARD_H__

#include <rtconfig.h>

extern unsigned int __bss_start;
extern unsigned int __bss_end;

#ifndef RT_USING_SMART
#define KERNEL_VADDR_START 0x0
#endif

#define RT_HW_HEAP_SIZE  64 * 1024 * 1024
#define RT_HW_HEAP_BEGIN ((void *)&__bss_end)
#define RT_HW_HEAP_END   ((void *)(RT_HW_HEAP_BEGIN + RT_HW_HEAP_SIZE))

void rt_hw_board_init(void);

#endif // __LS2K_BOARD_H__
