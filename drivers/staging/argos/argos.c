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
#include <linux/pm_qos.h>
#include <linux/gfp.h>
#include <linux/platform_device.h>

#define ARGOS_NAME "argos"

struct boost_entry {
	unsigned int throughput;
	unsigned int cpu_freq;
	unsigned int bus_freq;
};

struct argos_pm_qos {
	struct pm_qos_request cpu_qos_req;
	struct pm_qos_request bus_qos_req;
};

struct argos_block {
	const char *name;
	struct platform_device *pdev;
	struct boost_entry *table;
	struct boost_entry *power_table;
	int num_entry;
	int power_num_entry;
	int prev_level;
	int power_prev_level;
	struct argos_pm_qos *qos;
};

struct argos_platform_data {
	struct argos_block *blocks;
	int nblocks;
	struct notifier_block pm_qos_nfb;
};

static struct argos_platform_data *argos_pdata;
static uint32_t power_mode;
static uint32_t prev_power_mode;

#define UPDATE_PM_QOS(req, class_id, arg) ({ \
		if (arg) { \
			if (pm_qos_request_active(req)) \
				pm_qos_update_request(req, arg); \
			else \
				pm_qos_add_request(req, class_id, arg); \
		} \
	})

#define REMOVE_PM_QOS(req) ({ \
		if (pm_qos_request_active(req)) \
			pm_qos_remove_request(req); \
	})

#ifdef CONFIG_OF
static int argos_parse_dt(struct device *dev)
{
	struct argos_platform_data *pdata = dev->platform_data;
	struct argos_block *block;
	struct device_node *np, *cnp;
	int blk_count = 0;

	np = dev->of_node;
	pdata->nblocks = of_get_child_count(np);
	if (!pdata->nblocks)
		return -ENODEV;

	pdata->blocks = devm_kzalloc(dev, sizeof(struct argos_block) * pdata->nblocks,
			GFP_KERNEL);
	if (!pdata->blocks)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		int num_entry;
		int power_num_entry;
		int i;
		block = &pdata->blocks[blk_count];
		block->name = of_get_property(cnp, "net_boost,label", NULL);

		/* performance parameters */
		if (of_property_read_u32(cnp, "net_boost,table_size", &num_entry))
			return -EINVAL;
		block->num_entry = num_entry;

		block->table = devm_kzalloc(dev, sizeof(struct boost_entry) * num_entry,
				GFP_KERNEL);
		if (!block->table)
			return -ENOMEM;

		for (i = 0; i < num_entry; i++) {
			if (of_property_read_u32_index(cnp, "net_boost,table", i * 3,
						&block->table[i].throughput))
				return -EINVAL;
			if (of_property_read_u32_index(cnp, "net_boost,table", i * 3 + 1,
						&block->table[i].cpu_freq))
				return -EINVAL;
			if (of_property_read_u32_index(cnp, "net_boost,table", i * 3 + 2,
						&block->table[i].bus_freq))
				return -EINVAL;
		}
		block->prev_level = -1;

		/* power parameters */
		if (of_property_read_u32(cnp, "net_boost,power_table_size", &power_num_entry))
			return -EINVAL;
		block->power_num_entry = power_num_entry;

		block->power_table = devm_kzalloc(dev, sizeof(struct boost_entry) * power_num_entry,
				GFP_KERNEL);
		if (!block->power_table)
			return -ENOMEM;
		for (i = 0; i < power_num_entry; i++) {
			if (of_property_read_u32_index(cnp, "net_boost,power_table", i * 3,
						&block->power_table[i].throughput))
				return -EINVAL;
			if (of_property_read_u32_index(cnp, "net_boost,power_table", i * 3 + 1,
						&block->power_table[i].cpu_freq))
				return -EINVAL;
			if (of_property_read_u32_index(cnp, "net_boost,power_table", i * 3 + 2,
						&block->power_table[i].bus_freq))
				return -EINVAL;
		}
		block->power_prev_level = -1;

		block->qos = devm_kzalloc(dev, sizeof(struct argos_pm_qos), GFP_KERNEL);
		if (!block->qos)
			return -ENOMEM;
		blk_count++;
	};

	return 0;
}
#endif

static void argos_freq_unlock(int type)
{
	struct argos_pm_qos *qos = argos_pdata->blocks[type].qos;
	pr_info("%s: CPU: 0 BUS: 0\n", __func__);

	REMOVE_PM_QOS(&qos->cpu_qos_req);
	REMOVE_PM_QOS(&qos->bus_qos_req);
}

static void argos_freq_lock(int type, int level)
{
	struct argos_pm_qos *qos = argos_pdata->blocks[type].qos;
	struct boost_entry *e;
	unsigned int cpu_freq, bus_freq;

	if (power_mode)
		e = &argos_pdata->blocks[type].power_table[level];
	else
		e = &argos_pdata->blocks[type].table[level];
	cpu_freq = e->cpu_freq;
	bus_freq = e->bus_freq;

	pr_info("%s: CPU: %u BUS: %u\n", __func__, cpu_freq, bus_freq);
	if (power_mode) {
		if (cpu_freq)
			UPDATE_PM_QOS(&qos->cpu_qos_req, PM_QOS_CPU_FREQUENCY, cpu_freq);
		else
			REMOVE_PM_QOS(&qos->cpu_qos_req);

		if (bus_freq)
			UPDATE_PM_QOS(&qos->bus_qos_req, PM_QOS_BUS_FREQUENCY, bus_freq);
		else
			REMOVE_PM_QOS(&qos->bus_qos_req);
	} else {
		if (cpu_freq)
			UPDATE_PM_QOS(&qos->cpu_qos_req, PM_QOS_CPU_MAX_FREQUENCY, cpu_freq);
		else
			REMOVE_PM_QOS(&qos->cpu_qos_req);

		if (bus_freq)
			UPDATE_PM_QOS(&qos->bus_qos_req, PM_QOS_BUS_MAX_FREQUENCY, bus_freq);
		else
			REMOVE_PM_QOS(&qos->bus_qos_req);
	}
}

#define TYPE_SHIFT 4
#define TYPE_MASK_BIT ((1 << TYPE_SHIFT) - 1)

static void __argos_pm_qos_notify(struct argos_block *block, int type, long speed)
{
	int i;
	int level;
	int prev_level = block->prev_level;
	char *name = block->name;

	/* find proper level */
	for (level = -1, i = 0; i < block->num_entry; i++) {
		if (speed < block->table[i].throughput)
			break;
		level++;
	}

	pr_info("%s: name: %s : speed: %ld (%d->%d)\n", __func__, name, speed,
			prev_level, level);
	if (level != prev_level) {
		if (level == -1)
			argos_freq_unlock(type);
		else
			argos_freq_lock(type, level);
		block->prev_level = level;
	}
}
static void __argos_pm_qos_notify_power(struct argos_block *block,
					int type, long speed)
{
	int i;
	int level;
	int prev_level = block->power_prev_level;
	char *name = block->name;

	/* find proper level */
	for (level = -1, i = 0; i < block->power_num_entry; i++) {
		if (speed < block->power_table[i].throughput)
			break;
		level++;
	}

	pr_info("%s: name: %s : speed: %ld (%d->%d)\n", __func__, name, speed,
			prev_level, level);
	if (level != prev_level) {
		if (level == -1)
			argos_freq_unlock(type);
		else
			argos_freq_lock(type, level);
		block->power_prev_level = level;
	}
}
static int argos_pm_qos_notify(struct notifier_block *nfb, unsigned long speedtype, void *arg)
{
	int type;
	unsigned long speed;
	struct argos_block *block;

	pr_info("%s: arg: %ld\n", __func__, speedtype);
	type = (speedtype & TYPE_MASK_BIT) - 1;
	speed = speedtype >> TYPE_SHIFT;
	block = &argos_pdata->blocks[type];

	if (type > argos_pdata->nblocks)
		return NOTIFY_BAD;
	if (power_mode)
		__argos_pm_qos_notify_power(block, type, speed);
	else
		__argos_pm_qos_notify(block, type, speed);

	return NOTIFY_OK;
}

static ssize_t
power_mode_store(struct device *cd, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	int i;

	for (i = 0; i < argos_pdata->nblocks; i++) {
		argos_freq_unlock(i);
		argos_pdata->blocks[i].prev_level = -1;
		argos_pdata->blocks[i].power_prev_level = -1;
	}
	prev_power_mode = power_mode;
	power_mode = !!simple_strtoul(buf, NULL, 10);
	return count;
}

static ssize_t power_mode_show(struct device *cd,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", power_mode);
}
static DEVICE_ATTR(power_mode, S_IRUGO | S_IWUGO, power_mode_show, power_mode_store);
static int argos_create_sysfs(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int err;

	err = device_create_file(dev, &dev_attr_power_mode);

	return err;
}

static int argos_probe(struct platform_device *pdev)
{
	struct argos_platform_data *pdata;

	pr_info("%s: probe+\n", __func__);
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
		if (ret) {
			dev_err(&pdev->dev, "Failed to parse device tree\n");
			return ret;
		}
	} else
		pdata = pdev->dev.platform_data;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}
	pdata->pm_qos_nfb.notifier_call = argos_pm_qos_notify;
	pm_qos_add_notifier(PM_QOS_NETWORK_THROUGHPUT, &pdata->pm_qos_nfb);

	argos_pdata = pdata;
	platform_set_drvdata(pdev, pdata);
	argos_create_sysfs(pdev);

	pr_info("%s: probe-\n", __func__);
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
	pr_info("%s: init\n", __func__);
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
