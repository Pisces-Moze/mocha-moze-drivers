// SPDX-License-Identifier: GPL-2.0-only
/* Temporary Mocha codec-master oscillator MCLK diagnostic.
 * MIUI tegra_asoc_utils_set_parent(..., is_i2s_master=0) selects clk_m.
 * No PLL, regulator, codec, amplifier or nonvolatile register is written.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <dt-bindings/clock/tegra124-car.h>
#include <dt-bindings/soc/tegra-pmc.h>

static struct clk *osc, *ext, *pin, *old_ext, *old_pin;
static bool enabled;

static struct clk *get_clock(const char *path, unsigned int id)
{
	struct of_phandle_args args = {
		.np = of_find_node_by_path(path), .args_count = 1, .args = { id },
	};
	struct clk *clock;

	if (!args.np)
		return ERR_PTR(-ENOENT);
	clock = of_clk_get_from_provider(&args);
	of_node_put(args.np);
	return clock;
}

static void restore(void)
{
	if (enabled)
		clk_disable_unprepare(pin);
	if (old_pin && clk_set_parent(pin, old_pin))
		pr_err("MOCHA_AUDIO_OSC: pin parent restore failed\n");
	if (old_ext && clk_set_parent(ext, old_ext))
		pr_err("MOCHA_AUDIO_OSC: extern1 parent restore failed\n");
	if (!IS_ERR_OR_NULL(pin))
		clk_put(pin);
	if (!IS_ERR_OR_NULL(ext))
		clk_put(ext);
	if (!IS_ERR_OR_NULL(osc))
		clk_put(osc);
}

static int __init start(void)
{
	int ret;

	if (!of_machine_is_compatible("nvidia,mocha"))
		return -ENODEV;
	osc = get_clock("/clock@60006000", TEGRA124_CLK_CLK_M);
	ext = get_clock("/clock@60006000", TEGRA124_CLK_EXTERN1);
	pin = get_clock("/pmc@7000e400", TEGRA_PMC_CLK_OUT_1);
	if (IS_ERR(osc) || IS_ERR(ext) || IS_ERR(pin)) {
		ret = -ENODEV;
		goto fail;
	}
	if (__clk_is_enabled(ext) || __clk_is_enabled(pin) ||
	    clk_get_rate(osc) != 12000000) {
		ret = -EBUSY;
		goto fail;
	}
	old_ext = clk_get_parent(ext);
	old_pin = clk_get_parent(pin);
	if (!old_ext || !old_pin) {
		ret = -EINVAL;
		goto fail;
	}
	ret = clk_set_parent(ext, osc);
	if (ret)
		goto fail;
	ret = clk_set_parent(pin, ext);
	if (ret)
		goto fail;
	ret = clk_prepare_enable(pin);
	if (ret)
		goto fail;
	enabled = true;
	if (clk_get_rate(pin) != 12000000) {
		ret = -ERANGE;
		goto fail;
	}
	pr_info("MOCHA_AUDIO_OSC: codec-master MCLK=12000000, PLL rates unchanged\n");
	return 0;
fail:
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
MODULE_DESCRIPTION("Temporary Mocha codec-master 12 MHz oscillator MCLK probe");
