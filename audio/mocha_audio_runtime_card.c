// SPDX-License-Identifier: GPL-2.0-only
/* Explicit, removable Mocha speaker bring-up card. No DT or MTP changes.
 * Requires mocha_rt5671_board, tegra30-i2s and the protected TFA driver.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <dt-bindings/clock/tegra124-car.h>
#include <dt-bindings/soc/tegra-pmc.h>
#include "rt5670.h"

static struct platform_device *pdev;
static struct i2c_client *amps[2];
static struct clk *osc, *ext, *pin, *old_ext, *old_pin;
static bool clock_on, registered;

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

static int hw_params(struct snd_pcm_substream *s, struct snd_pcm_hw_params *p)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(s);
	struct snd_soc_dai *codec = snd_soc_rtd_to_codec(rtd, 0);
	int ret;

	if (params_rate(p) != 48000 || params_channels(p) != 2 ||
	    params_width(p) != 16)
		return -EINVAL;
	ret = snd_soc_dai_set_pll(codec, 0, RT5670_PLL1_S_MCLK,
				 12000000, 48000 * 512);
	if (ret)
		return ret;
	return snd_soc_dai_set_sysclk(codec, RT5670_SCLK_S_PLL1,
				     48000 * 512, SND_SOC_CLOCK_IN);
}

static const struct snd_soc_ops ops = { .hw_params = hw_params };
SND_SOC_DAILINK_DEFS(main,
	DAILINK_COMP_ARRAY(COMP_CPU("70301000.i2s")),
	DAILINK_COMP_ARRAY(COMP_CODEC("rt5670.0-001c", "rt5670-aif1")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("70301000.i2s")));
SND_SOC_DAILINK_DEFS(left,
	DAILINK_COMP_ARRAY(COMP_CPU("rt5670-aif2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tfa98xx.0-0037", "tfa98xx-dai")));
SND_SOC_DAILINK_DEFS(right,
	DAILINK_COMP_ARRAY(COMP_CPU("rt5670-aif2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tfa98xx.0-0034", "tfa98xx-dai")));
static const struct snd_soc_pcm_stream fixed = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE, .rate_min = 48000, .rate_max = 48000,
	.channels_min = 2, .channels_max = 2,
};
static struct snd_soc_dai_link links[] = {
	{
		.name = "Mocha RT5671", .stream_name = "Mocha HiFi", .ops = &ops,
		.ignore_pmdown_time = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(main),
	}, {
		.name = "RT5671 Left Speaker", .stream_name = "RT5671 Left SPK",
		.c2c_params = &fixed, .num_c2c_params = 1, .ignore_pmdown_time = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		SND_SOC_DAILINK_REG(left),
	}, {
		.name = "RT5671 Right Speaker", .stream_name = "RT5671 Right SPK",
		.c2c_params = &fixed, .num_c2c_params = 1, .ignore_pmdown_time = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		SND_SOC_DAILINK_REG(right),
	},
};
static struct snd_soc_codec_conf conf[] = {
	{ .dlc = { .name = "tfa98xx.0-0037" }, .name_prefix = "Left Spk" },
	{ .dlc = { .name = "tfa98xx.0-0034" }, .name_prefix = "Right Spk" },
};
static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_SPK("Int Left Spk", NULL),
	SND_SOC_DAPM_SPK("Int Right Spk", NULL),
};
static const struct snd_soc_dapm_route routes[] = {
	{ "Int Left Spk", NULL, "Left Spk Playback" },
	{ "Int Right Spk", NULL, "Right Spk Playback" },
};
static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Int Left Spk"),
	SOC_DAPM_PIN_SWITCH("Int Right Spk"),
};
static struct snd_soc_card card = {
	.name = "Mocha Speakers", .driver_name = "MochaSpeakers",
	.owner = THIS_MODULE, .dai_link = links, .num_links = ARRAY_SIZE(links),
	.codec_conf = conf, .num_configs = ARRAY_SIZE(conf),
	.dapm_widgets = widgets, .num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = routes, .num_dapm_routes = ARRAY_SIZE(routes),
	.controls = controls, .num_controls = ARRAY_SIZE(controls),
};

static void cleanup(void)
{
	int i;

	if (registered)
		snd_soc_unregister_card(&card);
	if (!IS_ERR_OR_NULL(pdev))
		platform_device_unregister(pdev);
	for (i = 1; i >= 0; i--)
		if (!IS_ERR_OR_NULL(amps[i]))
			i2c_unregister_device(amps[i]);
	if (clock_on)
		clk_disable_unprepare(pin);
	if (old_pin && clk_set_parent(pin, old_pin))
		pr_err("MOCHA_AUDIO_CARD: pin parent restore failed\n");
	if (old_ext && clk_set_parent(ext, old_ext))
		pr_err("MOCHA_AUDIO_CARD: extern1 parent restore failed\n");
	if (!IS_ERR_OR_NULL(pin))
		clk_put(pin);
	if (!IS_ERR_OR_NULL(ext))
		clk_put(ext);
	if (!IS_ERR_OR_NULL(osc))
		clk_put(osc);
}

static int __init start(void)
{
	struct i2c_adapter *adapter;
	int i, ret;

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
	ret = clk_set_parent(ext, osc);
	if (ret)
		goto fail;
	ret = clk_set_parent(pin, ext);
	if (ret)
		goto fail;
	ret = clk_prepare_enable(pin);
	if (ret)
		goto fail;
	clock_on = true;
	adapter = i2c_get_adapter(0);
	if (!adapter) {
		ret = -ENODEV;
		goto fail;
	}
	for (i = 0; i < 2; i++) {
		struct i2c_board_info info = { I2C_BOARD_INFO("tfa98xx", i ? 0x34 : 0x37) };

		amps[i] = i2c_new_client_device(adapter, &info);
		if (IS_ERR(amps[i])) {
			ret = PTR_ERR(amps[i]);
			i2c_put_adapter(adapter);
			goto fail;
		}
	}
	i2c_put_adapter(adapter);
	pdev = platform_device_register_simple("mocha-audio-runtime", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto fail;
	}
	card.dev = &pdev->dev;
	ret = snd_soc_register_card(&card);
	if (ret)
		goto fail;
	registered = true;
	pr_info("MOCHA_AUDIO_CARD: registered with 12 MHz codec-master MCLK\n");
	return 0;
fail:
	cleanup();
	return ret;
}

static void __exit stop(void)
{
	cleanup();
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Explicit removable Mocha RT5671 and dual TFA9890 bring-up card");
