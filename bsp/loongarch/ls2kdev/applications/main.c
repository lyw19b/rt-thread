#include <rtthread.h>
#include <rthw.h>

void test_delay()
{
    rt_uint64_t count = 0;
    while (1)
    {
        ++ count;
        rt_thread_mdelay(1000);
        rt_kprintf("[test_mdelay] %u s\n", count);
    }
}

int main(int argc, char **argv)
{
    rt_kprintf("Hi, this is RT-Thread!!\n");
    rt_kprintf("\033[1m\033[32mHello LoongArch64!\033[0m\n");

    return 0;
}
