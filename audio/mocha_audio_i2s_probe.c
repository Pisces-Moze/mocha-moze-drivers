// SPDX-License-Identifier: GPL-2.0-only
/* Temporary activation of the existing Mocha I2S0 DT node. This avoids a
 * reboot while testing machine routing. No live DT properties are changed.
 */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static struct platform_device *i2s, *ahub;
static struct device_node *node;

static int __init start(void)
{
	struct device_node *parent;
	int ret;

	if (!of_machine_is_compatible("nvidia,mocha"))
		return -ENODEV;
	parent = of_find_node_by_path("/ahub@70300000");
	ahub = of_find_device_by_node(parent);
	of_node_put(parent);
	if (!ahub)
		return -EPROBE_DEFER;
	if (!ahub->dev.driver) {
		ret = -EPROBE_DEFER;
		goto put;
	}
	node = of_find_node_by_path("/ahub@70300000/i2s@70301000");
	if (!node) {
		ret = -ENODEV;
		goto put;
	}
	if (of_node_test_and_set_flag(node, OF_POPULATED)) {
		ret = -EBUSY;
		goto node_put;
	}
	i2s = of_device_alloc(node, NULL, &ahub->dev);
	if (!i2s) {
		ret = -ENOMEM;
		goto clear;
	}
	i2s->name = dev_name(&i2s->dev);
	i2s->id = PLATFORM_DEVID_NONE;
	i2s->dev.bus = &platform_bus_type;
	i2s->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	i2s->dev.dma_mask = &i2s->dev.coherent_dma_mask;
	ret = device_add(&i2s->dev);
	if (ret) {
		platform_device_put(i2s);
		goto clear;
	}
	if (!i2s->dev.driver) {
		ret = -ENODEV;
		of_device_unregister(i2s);
		goto clear;
	}
	pr_info("MOCHA_AUDIO_I2S: %s bound to %s\n",
		dev_name(&i2s->dev), i2s->dev.driver->name);
	return 0;
clear:
	of_node_clear_flag(node, OF_POPULATED);
node_put:
	of_node_put(node);
put:
	put_device(&ahub->dev);
	return ret;
}
static void __exit stop(void)
{
	of_device_unregister(i2s);
	of_node_clear_flag(node, OF_POPULATED);
	of_node_put(node);
	put_device(&ahub->dev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mocha I2S0 temporary device registration for audio bring-up");
