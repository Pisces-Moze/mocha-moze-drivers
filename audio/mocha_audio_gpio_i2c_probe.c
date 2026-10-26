// SPDX-License-Identifier: GPL-2.0-only
/* Bounded Mocha I2C0 diagnostic through the same PC4/PC5 pads.
 * Adapter is locked; only open-drain signalling (low or release) is used.
 * Only RT5671 ID and the two TFA revision registers are addressed.
 * Every path frees GPIOs (Tegra restores SFIO on free), PM and adapter refs.
 * No value register, amplifier setting or nonvolatile memory is written.
 */
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

#define SCL 20 /* PC4, GEN1_I2C_SCL */
#define SDA 21 /* PC5, GEN1_I2C_SDA */
#define HALF_US 10
static int codec_result = -ENODATA, left_result = -ENODATA;
static int right_result = -ENODATA;
module_param(codec_result, int, 0444);
module_param(left_result, int, 0444);
module_param(right_result, int, 0444);

static int low(unsigned int pin)
{
	return gpio_direction_output(pin, 0);
}

static int release(unsigned int pin)
{
	return gpio_direction_input(pin);
}

static int high_clock(void)
{
	unsigned int elapsed;
	int ret = release(SCL);
	if (ret)
		return ret;
	/* Explicit bound: a broken/missing device cannot leave insmod waiting. */
	for (elapsed = 0; elapsed < 1000; elapsed += HALF_US) {
		if (gpio_get_value(SCL)) {
			udelay(HALF_US);
			return 0;
		}
		udelay(HALF_US);
	}
	return -ETIMEDOUT;
}

static int start_bus(void)
{
	int ret = release(SDA);
	if (ret)
		return ret;
	ret = high_clock();
	if (ret)
		return ret;
	if (!gpio_get_value(SDA))
		return -EBUSY;
	ret = low(SDA);
	udelay(HALF_US);
	if (ret)
		return ret;
	return low(SCL);
}

static int stop_bus(void)
{
	int ret = low(SCL), err;
	if (ret)
		return ret;
	ret = low(SDA);
	udelay(HALF_US);
	err = high_clock();
	if (!ret)
		ret = err;
	err = release(SDA);
	udelay(HALF_US);
	return ret ?: err;
}

static int write_byte(u8 value)
{
	int bit, ret, ack;
	for (bit = 7; bit >= 0; bit--) {
		ret = value & BIT(bit) ? release(SDA) : low(SDA);
		if (ret)
			return ret;
		udelay(HALF_US);
		ret = high_clock();
		if (ret)
			return ret;
		ret = low(SCL);
		if (ret)
			return ret;
	}
	ret = release(SDA);
	if (ret)
		return ret;
	udelay(HALF_US);
	ret = high_clock();
	if (ret)
		return ret;
	ack = !gpio_get_value(SDA);
	ret = low(SCL);
	if (ret)
		return ret;
	return ack ? 0 : -EREMOTEIO;
}

static int read_byte(bool last)
{
	int bit, ret, value = 0;
	ret = release(SDA);
	if (ret)
		return ret;
	for (bit = 7; bit >= 0; bit--) {
		udelay(HALF_US);
		ret = high_clock();
		if (ret)
			return ret;
		value |= !!gpio_get_value(SDA) << bit;
		ret = low(SCL);
		if (ret)
			return ret;
	}
	ret = last ? release(SDA) : low(SDA);
	if (ret)
		return ret;
	udelay(HALF_US);
	ret = high_clock();
	if (ret)
		return ret;
	ret = low(SCL);
	if (ret)
		return ret;
	ret = release(SDA);
	return ret ?: value;
}

static int read_id(u8 address, u8 reg)
{
	int ret, hi, lo, stop;
	ret = start_bus();
	if (ret)
		goto done;
	ret = write_byte(address << 1);
	if (ret)
		goto done;
	ret = write_byte(reg); /* Register pointer, no register value payload. */
	if (ret)
		goto done;
	ret = start_bus();
	if (ret)
		goto done;
	ret = write_byte((address << 1) | 1);
	if (ret)
		goto done;
	hi = read_byte(false);
	if (hi < 0) {
		ret = hi;
		goto done;
	}
	lo = read_byte(true);
	ret = lo < 0 ? lo : hi << 8 | lo;
done:
	stop = stop_bus();
	return ret < 0 ? ret : stop ?: ret;
}

static int __init start(void)
{
	struct i2c_adapter *adapter;
	struct device_node *node;
	struct device *controller;
	bool scl = false, sda = false;
	int ret;

	if (!of_machine_is_compatible("nvidia,mocha"))
		return -ENODEV;
	adapter = i2c_get_adapter(0);
	if (!adapter)
		return -ENODEV;
	controller = adapter->dev.parent;
	node = of_find_node_by_path("/i2c@7000c000");
	if (!node || !controller || controller->of_node != node) {
		of_node_put(node);
		ret = -ENODEV;
		goto put_adapter;
	}
	of_node_put(node);
	ret = pm_runtime_resume_and_get(controller);
	if (ret < 0)
		goto put_adapter;
	i2c_lock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
	ret = gpio_request(SCL, "mocha-audio-i2c-scl");
	if (ret)
		goto restore;
	scl = true;
	ret = gpio_request(SDA, "mocha-audio-i2c-sda");
	if (ret)
		goto restore;
	sda = true;
	ret = release(SCL);
	if (ret)
		goto restore;
	ret = release(SDA);
	if (ret)
		goto restore;
	codec_result = read_id(0x1c, 0xff);
	left_result = read_id(0x34, 0x03);
	right_result = read_id(0x37, 0x03);
	pr_info("MOCHA_AUDIO_GPIO_I2C: id=%d left=%d right=%d SCL=%d SDA=%d\n",
		codec_result, left_result, right_result,
		gpio_get_value(SCL), gpio_get_value(SDA));
restore:
	if (sda) {
		release(SDA);
		gpio_free(SDA);
	}
	if (scl) {
		release(SCL);
		gpio_free(SCL);
	}
	i2c_unlock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
	pm_runtime_put(controller);
put_adapter:
	i2c_put_adapter(adapter);
	return ret;
}

static void __exit stop(void) {}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded Mocha audio ID check through temporary open-drain GPIO I2C");
