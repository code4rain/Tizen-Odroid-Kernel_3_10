/*
 * drivers/stanging/argos/argos.c
 *
 * A Network extended QoS driver
 *
 * Copyright (C) 2015-2016 Samsung, Inc
 *
 * Kyoungdon Jang <alex.jang@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>

#define ARGOS_NAME "argos"

struct argos_platform_data {
};
static struct argos_platform_data *argos_pdata;

#ifdef CONFIG_OF
static int argos_parse_dt(struct device *dev)
{
	return 0;
}
#endif

static int argos_probe(struct platform_device *pdev)
{
	struct argos_platform_data *pdata;

	if (pdev->dev.of_node) {
		int ret = 0;
		pdata = devm_kzalloc(&pdev->dev, sizeof(struct argos_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev, "Failed to allocate platform data\n");
			return -ENOMEM;
		}
		pdev->dev.platform_data = pdata;

		ret = argos_parse_dt(&pdev->dev);
		if (!ret) {
			dev_err(&pdev->dev, "Failed to parse device tree\n");
			return ret;
		}
	} else
		pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}
	argos_pdata = pdata;
	platform_set_drvdata(pdev, pdata);

	return 0;
}

static int argos_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id argos_dt_ids[] = {
	{ .compatible = "samsung,argos"},
	{ }
};
#endif

static struct platform_driver argos_driver = {
	.driver = {
		.name = ARGOS_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table	= of_match_ptr(argos_dt_ids),
#endif
	},
	.probe = argos_probe,
	.remove = argos_remove
};

static int __init argos_init(void)
{
	return platform_driver_register(&argos_driver);
}

static void __exit argos_exit(void)
{
	return platform_driver_unregister(&argos_driver);
}

subsys_initcall(argos_init);
module_exit(argos_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SAMSUNG Electronics");
MODULE_DESCRIPTION("ARGOS DEVICE");
