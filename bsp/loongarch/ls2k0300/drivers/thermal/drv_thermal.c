/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: drv thermal.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

#include "drv_thermal.h"
#include "loongson_soc_info.h"
#include "bsp_thermal.h"
#include <drivers/sensor.h>

#define DBG_TAG "drv.thermal"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Thermal sensor device structure */
struct ls2k300_thermal_dev
{
    struct rt_sensor_device sensor;
};

static struct ls2k300_thermal_dev *thermal_dev = RT_NULL;

/**
 * @brief Sensor fetch data interface (required by RT-Thread sensor framework)
 * @param sensor RT-Thread sensor device
 * @param buf Buffer to store sensor data
 * @param len Buffer length
 * @return Size of data read (sizeof(struct rt_sensor_data)) on success, 0 on failure
 */
static rt_ssize_t ls2k300_thermal_fetch_data(struct rt_sensor_device *sensor, void *buf,
                                             rt_size_t len)
{
    struct rt_sensor_data *data = (struct rt_sensor_data *)buf;
    struct ls2k300_thermal_dev *dev = RT_NULL;
    rt_int32_t temp_val;
    rt_err_t ret;

    if (buf == RT_NULL || len != 1)
    {
        LOG_E("Invalid buffer for thermal data");
        return 0;
    }

    dev = rt_container_of(sensor, struct ls2k300_thermal_dev, sensor);

    temp_val = ls2k300_thermal_get_temp();

    /* Fill sensor data structure */
    data->type = RT_SENSOR_CLASS_TEMP;
    data->data.temp = (temp_val / 100);  /* Sensor framework uses deci-degrees Celsius. */
    data->timestamp = rt_sensor_get_ts();

    return 1;
}

/**
 * @brief Sensor control interface (required by RT-Thread sensor framework)
 * @param sensor RT-Thread sensor device
 * @param cmd Control command
 * @param args Command arguments
 * @return RT_EOK on success, -RT_ERROR on failure
 */
static rt_err_t ls2k300_thermal_control(struct rt_sensor_device *sensor, int cmd, void *args)
{
    struct ls2k300_thermal_dev *dev = rt_container_of(sensor, struct ls2k300_thermal_dev, sensor);
    rt_err_t ret = RT_EOK;

    switch (cmd)
    {
    case RT_SENSOR_CTRL_GET_ID:
        /* Return sensor ID if needed */
        if (args)
        {
            *(rt_uint32_t *)args = 0x230;  /* LS2K300 ID */
        }
        break;

    default:
        ret = -RT_EINVAL;
        break;
    }

    return ret;
}

/* RT-Thread sensor operations structure */
static struct rt_sensor_ops ls2k300_thermal_ops = {
    .fetch_data = ls2k300_thermal_fetch_data,
    .control = ls2k300_thermal_control,
};

/**
 * @brief Initialize and register LS2K300 thermal sensor driver
 * @return RT_EOK on success, -RT_ERROR on failure
 */
int rt_hw_ls2k300_thermal_init(void)
{
    rt_err_t ret;
    struct rt_sensor_config cfg = { 0 };

    /* Allocate device structure */
    thermal_dev = rt_malloc(sizeof(struct ls2k300_thermal_dev));
    if (thermal_dev == RT_NULL)
    {
        LOG_E("Failed to allocate thermal device memory");
        return -RT_ENOMEM;
    }

    /* Initialize device structure */
    rt_memset(thermal_dev, 0, sizeof(struct ls2k300_thermal_dev));

    /* Configure sensor device */
    cfg.intf.type = RT_SENSOR_INTF_ONEWIRE;  /* No interface needed for on-chip sensor */
    cfg.intf.dev_name = RT_NULL;

    /* Set sensor operations */
    thermal_dev->sensor.ops = &ls2k300_thermal_ops;
    thermal_dev->sensor.info.type = RT_SENSOR_CLASS_TEMP;
    thermal_dev->sensor.info
        .vendor = RT_SENSOR_VENDOR_UNKNOWN;  /* Can be set to Loongson specific */
    thermal_dev->sensor.info.model = "ls2k300-thermal";
    thermal_dev->sensor.info.unit = RT_SENSOR_UNIT_DCELSIUS;    /* Tenths of a degree Celsius */
    thermal_dev->sensor.info.range_min = -400;           /* -40°C */
    thermal_dev->sensor.info.range_max = 1250;           /* +125°C */

    /* Register sensor device */
    ret = rt_hw_sensor_register(&thermal_dev->sensor, "2k300", RT_DEVICE_FLAG_RDONLY, &cfg);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to register thermal sensor device");
        rt_free(thermal_dev);
        thermal_dev = RT_NULL;
        return ret;
    }

    return RT_EOK;
}

INIT_DEVICE_EXPORT(rt_hw_ls2k300_thermal_init);

/* Export to RT-Thread components */
#ifdef RT_USING_FINSH
#include <finsh.h>

/**
 * @brief Shell command to read current temperature
 * @return Current temperature in degrees Celsius (printed to console)
 */
static int cpu_sensor_read(void)
{
    struct rt_sensor_data data;
    rt_device_t dev = rt_device_find("temp_2k300");

    if (dev == RT_NULL)
    {
        rt_kprintf("Thermal sensor not found\n");
        return -1;
    }

    if (rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        rt_kprintf("Failed to open thermal sensor\n");
        return -1;
    }

    if (rt_device_read(dev, 0, &data, 1) == 1)
    {
        rt_kprintf("Current temperature: %d deci-degrees C\n", data.data.temp);
    }
    else
    {
        rt_kprintf("Failed to read temperature\n");
    }

    rt_device_close(dev);
    return 0;
}

MSH_CMD_EXPORT(cpu_sensor_read, Read LS2K300 thermal sensor temperature);
#endif /* RT_USING_FINSH */
