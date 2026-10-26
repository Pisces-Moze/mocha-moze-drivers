// SPDX-License-Identifier: GPL-2.0-only
/* Temporary Mocha I2C0 clock-divisor diagnostic. No codec register writes.
 * Hold runtime PM so a resume cannot silently replace the temporary rate.
 * Serialize clock changes with transfers; restore the rate on every exit.
 */
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

static struct i2c_adapter *adapter;
static struct device *controller;
static struct clk *div_clk;
static bool resumed, changed;
static unsigned long original_rate, active_rate;
module_param(original_rate, ulong, 0444);
module_param(active_rate, ulong, 0444);

static void restore(void)
{
	if (changed) {
		i2c_lock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
		if (clk_set_rate(div_clk, original_rate))
			pr_err("MOCHA_AUDIO_I2C: could not restore divider clock\n");
		i2c_unlock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
	}
	if (!IS_ERR_OR_NULL(div_clk))
		clk_put(div_clk);
	if (resumed)
		pm_runtime_put(controller);
	if (adapter)
		i2c_put_adapter(adapter);
}

static int __init start(void)
{
	struct device_node *node;
	u32 frequency;
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
		goto error;
	}
	ret = of_property_read_u32(node, "clock-frequency", &frequency);
	of_node_put(node);
	if (ret || frequency != 400000) {
		ret = -EINVAL;
		goto error;
	}
	ret = pm_runtime_resume_and_get(controller);
	if (ret < 0)
		goto error;
	resumed = true;
	div_clk = clk_get(controller, "div-clk");
	if (IS_ERR(div_clk)) {
		ret = PTR_ERR(div_clk);
		goto error;
	}
	i2c_lock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
	original_rate = clk_get_rate(div_clk);
	if (!original_rate || original_rate % 4) {
		ret = -EINVAL;
	} else {
		changed = true;
		ret = clk_set_rate(div_clk, original_rate / 4);
		active_rate = clk_get_rate(div_clk);
		if (!ret && active_rate != original_rate / 4)
			ret = -ERANGE;
	}
	i2c_unlock_bus(adapter, I2C_LOCK_ROOT_ADAPTER);
	if (ret)
		goto error;
	pr_info("MOCHA_AUDIO_I2C: temporary divider clock %lu -> %lu\n",
		original_rate, active_rate);
	return 0;
error:
	restore();
	return ret;
}

static void __exit stop(void)
{
	restore();
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary Mocha I2C0 quarter-rate diagnostic, never autoload");
