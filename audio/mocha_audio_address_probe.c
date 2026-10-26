// SPDX-License-Identifier: GPL-2.0-only
/* The P2B schematic labels PX5 CDC_RESET and the codec pin I2C_ADDR.
 * This tablet responds at 0x1c only with PX5 high, not at the schematic's
 * annotated 0x1d. Do not infer reset/address polarity from that sheet alone.
 * Diagnostic only: hold each level, read IDs, then restore
 * SFIO, direction and output latch. No codec value or TFA register writes.
 */
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

#define ADDR_GPIO 189 /* PX5 */
#define PORT_X 0x50c /* Tegra124: bank 5, port 3 */
#define PIN_BIT BIT(5)
static int low_1c = -ENODATA, low_1d = -ENODATA;
static int high_1c = -ENODATA, high_1d = -ENODATA;
module_param(low_1c, int, 0444);
module_param(low_1d, int, 0444);
module_param(high_1c, int, 0444);
module_param(high_1d, int, 0444);

static int read_id(struct i2c_adapter *adap, u8 addr)
{
	u8 reg = 0xff, out[2] = {};
	struct i2c_msg msgs[] = {
		{ .addr = addr, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 2, .buf = out },
	};
	int ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	return ret == ARRAY_SIZE(msgs) ? out[0] << 8 | out[1] :
		ret < 0 ? ret : -EIO;
}

static int __init start(void)
{
	struct i2c_adapter *adap;
	struct device_node *np;
	void __iomem *base;
	u32 cnf, oe, out;
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
	base = ioremap(0x6000d000, 0x1000);
	if (!base) {
		ret = -ENOMEM;
		goto put;
	}
	cnf = readl(base + PORT_X) & PIN_BIT;
	oe = readl(base + PORT_X + 0x10) & PIN_BIT;
	out = readl(base + PORT_X + 0x20) & PIN_BIT;
	/* Freeing a Tegra GPIO restores SFIO. Require that original state. */
	if (cnf) {
		ret = -EBUSY;
		goto unmap;
	}
	ret = gpio_request(ADDR_GPIO, "mocha-codec-address");
	if (ret)
		goto unmap;
	ret = gpio_direction_output(ADDR_GPIO, 0);
	if (ret)
		goto restore;
	msleep(200);
	low_1c = read_id(adap, 0x1c);
	low_1d = read_id(adap, 0x1d);
	gpio_set_value(ADDR_GPIO, 1);
	msleep(200);
	high_1c = read_id(adap, 0x1c);
	high_1d = read_id(adap, 0x1d);
restore:
	if (oe)
		gpio_direction_output(ADDR_GPIO, !!out);
	else {
		gpio_set_value(ADDR_GPIO, !!out);
		gpio_direction_input(ADDR_GPIO);
	}
	gpio_free(ADDR_GPIO);
	if ((readl(base + PORT_X) & PIN_BIT) != cnf ||
	    (readl(base + PORT_X + 0x10) & PIN_BIT) != oe ||
	    (readl(base + PORT_X + 0x20) & PIN_BIT) != out)
		pr_err("MOCHA_AUDIO_ADDR: GPIO state restoration mismatch\n");
	else
		pr_info("MOCHA_AUDIO_ADDR: GPIO state restored\n");
	pr_info("MOCHA_AUDIO_ADDR: low 1c=%d 1d=%d high 1c=%d 1d=%d\n",
		low_1c, low_1d, high_1c, high_1d);
unmap:
	iounmap(base);
put:
	i2c_put_adapter(adap);
	return ret;
}
static void __exit stop(void) {}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded Mocha codec address-select diagnostic");
