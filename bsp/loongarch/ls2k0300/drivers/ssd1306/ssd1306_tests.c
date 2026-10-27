/*
* Copyright (c) 2020, RudyLo <luhuadong@163.com>
*
* SPDX-License-Identifier: MIT License
*
* Change Logs:
* Date           Author       Notes
* 2020-11-15     luhuadong    the first version
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: ssd1306 tests.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <stdio.h>
#include "ssd1306.h"
#include "ssd1306_tests.h"

void ssd1306_TestFonts()
{
    ssd1306_Fill(Black);
    ssd1306_SetCursor(2, 0);
    ssd1306_WriteString("Font 16x26", Font_16x26, White);
    ssd1306_SetCursor(2, 26);
    ssd1306_WriteString("Font 11x18", Font_11x18, White);
    ssd1306_SetCursor(2, 26 + 18);
    ssd1306_WriteString("Font 7x10", Font_7x10, White);
    ssd1306_SetCursor(2, 26 + 18 + 10);
    ssd1306_WriteString("Font 6x8", Font_6x8, White);
    ssd1306_UpdateScreen();
}

void ssd1306_TestFPS()
{
    char buff[64];
    ssd1306_Fill(White);

    uint32_t start = rt_tick_get();
    uint32_t end = start;
    int fps = 0;
    int test_time;
    int tick_count;
    char message[] = "ABCDEFGHIJK";

    ssd1306_SetCursor(2, 0);
    ssd1306_WriteString("Testing...", Font_11x18, Black);

    test_time = 5;
    tick_count = RT_TICK_PER_SECOND * test_time;
    do
    {
        ssd1306_SetCursor(2, 18);
        ssd1306_WriteString(message, Font_11x18, Black);
        ssd1306_UpdateScreen();

        char ch = message[0];
        memmove(message, message + 1, sizeof(message) - 2);
        message[sizeof(message) - 2] = ch;

        fps++;
        end = rt_tick_get();
    } while ((end - start) < tick_count);

    fps = fps / test_time;
    rt_kprintf("fps: %d\n", fps);
    rt_sprintf(buff, "%d frame", fps);

    ssd1306_Fill(White);
    ssd1306_SetCursor(2, 18);
    ssd1306_WriteString(buff, Font_11x18, Black);
    ssd1306_UpdateScreen();
}

void ssd1306_TestLine()
{
    ssd1306_Line(1, 1, SSD1306_WIDTH - 1, SSD1306_HEIGHT - 1, White);
    ssd1306_Line(SSD1306_WIDTH - 1, 1, 1, SSD1306_HEIGHT - 1, White);
    ssd1306_UpdateScreen();
    return;
}

void ssd1306_TestRectangle()
{
    uint32_t delta;

    for (delta = 0; delta < 5; delta++)
    {
        ssd1306_DrawRectangle(1 + (5 * delta), 1 + (5 * delta), SSD1306_WIDTH - 1 - (5 * delta),
                              SSD1306_HEIGHT - 1 - (5 * delta), White);
    }
    ssd1306_UpdateScreen();
    return;
}

void ssd1306_TestCircle()
{
    uint32_t delta;

    for (delta = 0; delta < 5; delta++)
    {
        ssd1306_DrawCircle(20 * delta + 30, 30, 10, White);
    }
    ssd1306_UpdateScreen();
    return;
}

void ssd1306_TestAll()
{
    ssd1306_Init();

    ssd1306_TestFPS();
    rt_thread_mdelay(2000);

    ssd1306_TestFonts();
    rt_thread_mdelay(2000);

    ssd1306_Fill(Black);
    ssd1306_TestRectangle();
    ssd1306_TestLine();
    rt_thread_mdelay(2000);

    ssd1306_Fill(Black);
    ssd1306_TestCircle();
}

#ifdef FINSH_USING_MSH
MSH_CMD_EXPORT(ssd1306_TestAll, test ssd1306 oled driver);
#endif
