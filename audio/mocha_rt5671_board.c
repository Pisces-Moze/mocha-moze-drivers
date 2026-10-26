// SPDX-License-Identifier: GPL-2.0-only
/* Temporary Mocha codec bring-up without altering the default DTB.
 * The board's CDC_RESET/PX5 must stay high for RT5671 to answer.
 * A future DT reset GPIO should replace this explicit board shim.
 */
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

static void __iomem *gpio_base;
static u32 saved_oe, saved_out;
static struct i2c_client *codec;
static bool register_codec;
module_param(register_codec, bool, 0444);

static void restore(void)
{
	if (codec) {
		i2c_unregister_device(codec);
		codec = NULL;
	}
	if (saved_oe)
		gpio_direction_output(189, !!saved_out);
	else {
		gpio_set_value(189, !!saved_out);
		gpio_direction_input(189);
	}
	gpio_free(189);
}

static int __init start(void)
{
	struct i2c_adapter *adap;
	struct device_node *np;
	struct i2c_board_info info = { I2C_BOARD_INFO("rt5671", 0x1c) };
	u8 reg = 0xff, val[2] = {};
	struct i2c_msg msg[] = {
		{ .addr = 0x1c, .len = 1, .buf = &reg },
		{ .addr = 0x1c, .flags = I2C_M_RD, .len = 2, .buf = val },
	};
	int ret;

	if (!of_machine_is_compatible("nvidia,mocha"))
		return -ENODEV;
	adap = i2c_get_adapter(0);
	if (!adap)
		return -ENODEV;
	np = of_find_node_by_path("/i2c@7000c000");
	if (!np || !adap->dev.parent || adap->dev.parent->of_node != np) {
		of_node_put(np);
		ret = -ENODEV;
		goto put;
	}
	of_node_put(np);
	gpio_base = ioremap(0x6000d000, 0x1000);
	if (!gpio_base) {
		ret = -ENOMEM;
		goto put;
	}
	if (readl(gpio_base + 0x50c) & BIT(5)) {
		ret = -EBUSY;
		goto unmap;
	}
	saved_oe = readl(gpio_base + 0x51c) & BIT(5);
	saved_out = readl(gpio_base + 0x52c) & BIT(5);
	ret = gpio_request(189, "mocha-codec-enable");
	if (ret)
		goto unmap;
	ret = gpio_direction_output(189, 1);
	if (ret)
		goto disable;
	msleep(200);
	ret = i2c_transfer(adap, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		ret = ret < 0 ? ret : -EIO;
		goto disable;
	}
	if (val[0] != 0x62 || val[1] != 0x71) {
		ret = -ENODEV;
		goto disable;
	}
	pr_info("MOCHA_RT5671: PX5 high, verified codec ID 0x6271\n");
	if (register_codec) {
		codec = i2c_new_client_device(adap, &info);
		if (IS_ERR(codec)) {
			ret = PTR_ERR(codec);
			codec = NULL;
			goto disable;
		}
	}
	i2c_put_adapter(adap);
	return 0;
disable:
	restore();
unmap:
	iounmap(gpio_base);
put:
	i2c_put_adapter(adap);
	return ret;
}
static void __exit stop(void)
{
	restore();
	iounmap(gpio_base);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mocha RT5671 board enable and verified I2C registration");
