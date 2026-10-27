/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * File: ls2k3000_periph_host.c
 * Author: LoongsonLab
 */

/*
 * Description:
 *   Fault-inject the production peripheral engines without accessing real devices.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i2c_poll.h"
#include "spi_poll.h"
#include "rtc_toy.h"
#include "pwm_hw.h"
#include "watchdog_hw.h"
#include "hpet_counter.h"

enum model_type
{
    REGISTER_MODEL,
    I2C_MODEL,
    SPI_MODEL
};

struct model
{
    uint32_t registers[1024];
    unsigned int command[256];
    unsigned int commands;
    unsigned int writes;
    unsigned int mode;
    unsigned int fault;
    unsigned int fault_after;
    unsigned int transfers;
    unsigned int rolls;
    uint64_t clock;
};

static unsigned int checks;
#define CHECK(expression) do { assert(expression); ++checks; } while (0)

static uint32_t model_read(void *context, unsigned int reg, unsigned int width)
{
    struct model *model = context;

    (void)width;
    assert(reg < 1024);

    if (model->mode == I2C_MODEL && reg == 4)
    {
        if (model->fault && model->transfers >= model->fault_after)
        {
            return model->fault;
        }
    }

    if (model->mode == SPI_MODEL && reg == 1 && model->fault &&
        model->transfers >= model->fault_after)
    {
        return model->fault;
    }

    if (model->mode == SPI_MODEL && reg == 2)
    {
        model->registers[1] = 1;
    }

    if (reg == 0x2c && model->rolls)
    {
        --model->rolls;
        return ++model->registers[reg];
    }

    if (reg == 0xf4 && model->rolls)
    {
        --model->rolls;
        return ++model->registers[reg];
    }

    return model->registers[reg];
}

static void model_write(void *context, unsigned int reg, unsigned int width, uint32_t value)
{
    struct model *model = context;

    (void)width;
    assert(reg < 1024);
    ++model->writes;

    if (model->mode == I2C_MODEL && reg == 4)
    {
        assert(model->commands < 256);
        model->command[model->commands++] = value;

        if (value & 0xb0)
        {
            ++model->transfers;
        }

        if (value & 0x40)
        {
            model->registers[reg] = 0;
        }
        else if (value & 0xb0)
        {
            model->registers[reg] = 0x40;
        }
        return;
    }

    if (model->mode == SPI_MODEL && reg == 1)
    {
        model->registers[1] &= ~value;
        return;
    }

    if (model->mode == SPI_MODEL && reg == 2)
    {
        ++model->transfers;
        model->registers[1] = 0;
    }

    model->registers[reg] = value;

    if (model->mode == REGISTER_MODEL && reg == 0x28)
    {
        model->registers[0x30] = value;
    }

    if (model->mode == REGISTER_MODEL && reg == 0x24)
    {
        model->registers[0x2c] = value;
    }
}

static uint64_t model_now(void *context)
{
    struct model *model = context;

    model->clock += 1000;
    return model->clock;
}

static struct ls_periph_io io_for(struct model *model)
{
    struct ls_periph_io io = {model, model_read, model_write, model_now};

    return io;
}

static void test_i2c(void)
{
    struct model model = {.mode = I2C_MODEL};
    struct ls_periph_io io = io_for(&model);
    uint8_t data[2] = {0x12, 0x34};
    struct ls_i2c_message message = {0x50, 0, 2, data};

    CHECK(ls_i2c_setup(&io, 100000000, 100000) == LS_OK);
    CHECK(model.registers[0] == 181 && model.registers[1] == 0);
    CHECK(ls_i2c_setup(&io, 48000000, 400000) == LS_OK);
    CHECK(model.registers[0] == 21);
    CHECK(ls_i2c_setup(&io, 48000000, 400001) == LS_INVALID);
    CHECK(ls_i2c_setup(&io, 100000000, 1) == LS_INVALID);
    CHECK(ls_i2c_setup(&io, 100000000, 0) == LS_INVALID);
    CHECK(ls_i2c_message(&io, &message) == LS_OK);
    message.read = 1;
    CHECK(ls_i2c_message(&io, &message) == LS_OK);
    CHECK(model.command[model.commands - 3] == 0x91);
    CHECK(model.command[model.commands - 2] == 0x21);
    CHECK(model.command[model.commands - 1] == 0x29);
    CHECK(ls_i2c_stop(&io) == LS_OK);
    CHECK(model.command[model.commands - 1] == 0x41);

    model.fault = 0x80;
    CHECK(ls_i2c_message(&io, &message) == LS_NACK);
    ls_i2c_recover(&io);
    CHECK(model.command[model.commands - 1] == 0x41);
    model.fault = 0x20;
    CHECK(ls_i2c_message(&io, &message) == LS_BUSY);
    ls_i2c_recover(&io);
    CHECK(model.command[model.commands - 1] == 1);
    model.fault = 2;
    CHECK(ls_i2c_message(&io, &message) == LS_TIMEOUT);
    ls_i2c_recover(&io);
    CHECK(model.registers[2] == 0x80);
    CHECK(model.clock < 100000);
    model.fault = 0x40;
    unsigned int writes = model.writes;
    CHECK(ls_i2c_setup(&io, 100000000, 100000) == LS_BUSY);
    CHECK(model.writes == writes);
    model.fault = 0;
    model.registers[4] = 0;
    CHECK(ls_i2c_setup(&io, 100000000, 100000) == LS_OK);
    CHECK(ls_i2c_message(&io, &message) == LS_OK);
    CHECK(ls_i2c_stop(&io) == LS_OK);
}

static void test_spi(void)
{
    struct model model = {.mode = SPI_MODEL};
    struct ls_spi_state state = {.io = io_for(&model)};
    uint8_t tx[3] = {0x12, 0x34, 0x56};
    uint8_t rx[3];

    model.registers[0] = 0x51;
    model.registers[1] = 1;
    model.registers[3] = 0x90;
    model.registers[4] = 0x41;
    model.registers[5] = 0xff;
    CHECK(ls_spi_divider(100000000, 50000000) == 0);
    CHECK(ls_spi_divider(100000000, 10000000) == 2);
    CHECK(ls_spi_divider(100000000, 1) == LS_INVALID);
    CHECK(ls_spi_divider(0, 1000000) == LS_INVALID);

    for (unsigned int mode = 0; mode < 4; ++mode)
    {
        CHECK(ls_spi_begin(&state, 100000000, 1000000, mode, mode) == LS_OK);
        CHECK(model.registers[5] == (0xff & ~(0x10U << mode)));
        CHECK((model.registers[0] & 12) == mode * 4);
        CHECK(ls_spi_begin(&state, 100000000, 1000000, 0, 0) == LS_BUSY);
        CHECK(ls_spi_bytes(&state, tx, rx, 3) == LS_OK);
        CHECK(!memcmp(tx, rx, 3));
        ls_spi_end(&state);
        CHECK(model.registers[0] == 0x51 && model.registers[3] == 0x90);
        CHECK(model.registers[4] == 0x41 && model.registers[5] == 0xff);
    }

    CHECK(ls_spi_begin(&state, 100000000, 1000000, 0, 4) == LS_INVALID);
    CHECK(ls_spi_begin(&state, 100000000, 1000000, 0, 0) == LS_OK);
    model.fault = 1;
    model.fault_after = model.transfers + 1;
    CHECK(ls_spi_bytes(&state, tx, rx, 3) == LS_TIMEOUT);
    CHECK(!state.active && model.registers[4] == 0x41);
    model.fault = 0;
    model.registers[1] = 1;
    CHECK(ls_spi_begin(&state, 100000000, 1000000, 0, 0) == LS_OK);
    model.fault = 0x40;
    CHECK(ls_spi_bytes(&state, tx, rx, 3) == LS_IO_ERROR);
    CHECK(!state.active && model.registers[5] == 0xff);
}

static void test_rtc(void)
{
    struct model model = {0};
    struct ls_periph_io io = io_for(&model);
    int64_t value;
    unsigned int fraction;
    const int64_t dates[] = {0, 951782400, 1709164800, 4107542399, 4107542400,
                             INT64_C(253402300799)};

    CHECK(ls_rtc_read(&io, &value, &fraction) == LS_DISABLED);
    model.registers[0x40] = 0x900;

    for (unsigned int i = 0; i < sizeof(dates) / sizeof(dates[0]); ++i)
    {
        CHECK(ls_rtc_write(&io, dates[i], i % 10) == LS_OK);
        CHECK(ls_rtc_read(&io, &value, &fraction) == LS_OK);
        CHECK(value == dates[i] && fraction == i % 10);
    }

    CHECK(ls_rtc_write(&io, 1709164800, 0) == LS_OK);
    CHECK(model.registers[0x30] == 124);
    CHECK(model.registers[0x2c] == ((2U << 26) | (29U << 21)));
    model.registers[0x30] = 200;
    CHECK(ls_rtc_read(&io, &value, &fraction) == LS_INVALID);
    CHECK(ls_rtc_write(&io, -1, 0) == LS_INVALID);
    CHECK(ls_rtc_write(&io, INT64_C(253402300800), 0) == LS_INVALID);
    CHECK(ls_rtc_write(&io, 0, 10) == LS_INVALID);
    CHECK(ls_rtc_write(&io, 0, 0) == LS_OK);
    model.rolls = 16;
    CHECK(ls_rtc_read(&io, &value, &fraction) == LS_BUSY);
    model.registers[0x40] |= 1;
    unsigned int writes = model.writes;
    CHECK(ls_rtc_write(&io, 0, 0) == LS_TIMEOUT);
    CHECK(model.writes == writes);
}

static void test_pwm_watchdog_hpet(void)
{
    struct model model = {0};
    struct ls_periph_io io = io_for(&model);
    uint32_t period;
    uint32_t pulse;
    uint64_t ticks;

    CHECK(ls_pwm_set(&io, 0, 1000000, 250000) == LS_OK);
    CHECK(model.registers[4] == 36000 && model.registers[8] == 48000);
    CHECK(model.registers[12] == 8);
    CHECK(ls_pwm_enable(&io, 0, 1) == LS_OK);
    CHECK(model.registers[12] == 1);
    CHECK(ls_pwm_get(&io, 0, &period, &pulse) == LS_OK);
    CHECK(period == 1000000 && pulse == 250000);
    CHECK(ls_pwm_set(&io, 0, 1000000, 0) == LS_INVALID);
    CHECK(ls_pwm_set(&io, 0, 1000000, 1000000) == LS_INVALID);
    CHECK(ls_pwm_set(&io, 4, 1000, 500) == LS_INVALID);
    CHECK(ls_pwm_set(&io, 0, 10, 5) == LS_INVALID);
    model.registers[12] = 9;
    CHECK(ls_pwm_set(&io, 0, 1000, 500) == LS_OK);
    CHECK(model.registers[12] == 9);
    model.registers[12] = 0x100;
    CHECK(ls_pwm_set(&io, 0, 1000, 500) == LS_BUSY);
    CHECK(ls_pwm_enable(&io, 0, 1) == LS_BUSY);

    memset(&model, 0, sizeof(model));
    CHECK(ls_wdt_feed(&io) == LS_DISABLED);
    CHECK(ls_wdt_timeout(&io, 0) == LS_INVALID);
    CHECK(ls_wdt_timeout(&io, 86) == LS_INVALID);
    CHECK(ls_wdt_timeout(&io, 85) == LS_OK);
    CHECK(model.registers[8] == 4250000000U && model.registers[0] == 0);
    CHECK(ls_wdt_enable(&io, 1) == LS_OK);
    CHECK(model.registers[0] == 2 && model.registers[4] == 1);
    CHECK(ls_wdt_feed(&io) == LS_OK);
    model.registers[0] = 3;
    CHECK(ls_wdt_enable(&io, 0) == LS_OK);
    CHECK(model.registers[0] == 0);

    memset(&model, 0, sizeof(model));
    CHECK(ls_hpet_period(&io, &period) == LS_UNSUPPORTED);
    model.registers[0] = 0x2201;
    model.registers[4] = 20833333;
    CHECK(ls_hpet_period(&io, &period) == LS_OK);
    CHECK(period == 20833333);
    CHECK(ls_hpet_read(&io, &ticks) == LS_DISABLED);
    model.registers[0x10] = 1;
    model.registers[0xf0] = 0x12345678;
    model.registers[0xf4] = 0xabcdef;
    CHECK(ls_hpet_read(&io, &ticks) == LS_OK);
    CHECK(ticks == UINT64_C(0xabcdef12345678));
    model.rolls = 2;
    CHECK(ls_hpet_read(&io, &ticks) == LS_OK);
    model.rolls = 16;
    CHECK(ls_hpet_read(&io, &ticks) == LS_BUSY);
    CHECK(model.writes == 0);
}

int main(void)
{
    test_i2c();
    test_spi();
    test_rtc();
    test_pwm_watchdog_hpet();
    printf("LS2K3000 peripheral engines PASS: %u checks\n", checks);
    return 0;
}
