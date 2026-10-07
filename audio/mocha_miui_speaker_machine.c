// SPDX-License-Identifier: GPL-2.0-only
/* Mocha RT5671 routing and codec-master clock arrangement from MIUI
 * tegra_rt5671.c / board-ardbeg.c, expressed through Linux 6.12 ASoC.
 * Two separate codec-to-codec links reproduce MIUI's RT5671 AIF2 ->
 * left/right TFA9890 wiring. The amplifier driver owns DSP protection.
 * This candidate is not installed by the default desktop boot.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "tegra_asoc_machine.h"
#include "rt5670.h"
static int hw_params(struct snd_pcm_substream *s, struct snd_pcm_hw_params *p)
{
    struct snd_soc_pcm_runtime *rtd=snd_soc_substream_to_rtd(s);
    struct tegra_machine *m=snd_soc_card_get_drvdata(rtd->card);
    struct snd_soc_dai *codec=snd_soc_rtd_to_codec(rtd,0);
    unsigned int rate=params_rate(p), base, mclk=256*rate;
    /* The factory speaker links are fixed 48 kHz, stereo, 16 bit. */
    if (rate != 48000 || params_channels(p) != 2 || params_width(p) != 16)
        return -EINVAL;
    int ret;
    switch(rate) {
    case 8000: case 16000: case 32000: case 48000: case 96000: base=368640000;break;
    case 11025: case 22050: case 44100: case 88200: base=282240000;break;
    default:return -EINVAL;
    }
    if(m->set_baseclock!=base || m->set_mclk!=mclk) {
        clk_disable_unprepare(m->clk_cdev1);
        ret=clk_set_rate(m->clk_pll_a,base);if(ret)return ret;
        ret=clk_set_rate(m->clk_pll_a_out0,mclk);if(ret)return ret;
        ret=clk_prepare_enable(m->clk_cdev1);if(ret)return ret;
        m->set_baseclock=base;m->set_mclk=mclk;
    }
    ret=snd_soc_dai_set_pll(codec,0,RT5670_PLL1_S_MCLK,mclk,512*rate);
    if(ret)return ret;
    return snd_soc_dai_set_sysclk(codec,RT5670_SCLK_S_PLL1,512*rate,SND_SOC_CLOCK_IN);
}
static const struct snd_soc_ops ops={.hw_params=hw_params};
SND_SOC_DAILINK_DEFS(mocha_aif1,
    DAILINK_COMP_ARRAY(COMP_EMPTY()),
    DAILINK_COMP_ARRAY(COMP_CODEC(NULL,"rt5670-aif1")),
    DAILINK_COMP_ARRAY(COMP_EMPTY()));
SND_SOC_DAILINK_DEFS(mocha_left,
    DAILINK_COMP_ARRAY(COMP_CODEC(NULL,"rt5670-aif2")),
    DAILINK_COMP_ARRAY(COMP_CODEC(NULL,"tfa98xx-dai")));
SND_SOC_DAILINK_DEFS(mocha_right,
    DAILINK_COMP_ARRAY(COMP_CODEC(NULL,"rt5670-aif2")),
    DAILINK_COMP_ARRAY(COMP_CODEC(NULL,"tfa98xx-dai")));
static const struct snd_soc_pcm_stream speaker_params={
    .formats=SNDRV_PCM_FMTBIT_S16_LE,.rate_min=48000,.rate_max=48000,
    .channels_min=2,.channels_max=2,
};
static struct snd_soc_dai_link links[]={
    {
        .name="Mocha RT5671",.stream_name="Mocha HiFi",
        .init=tegra_asoc_machine_init,.ops=&ops,.ignore_pmdown_time=1,
        .dai_fmt=SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF|SND_SOC_DAIFMT_CBM_CFM,
        SND_SOC_DAILINK_REG(mocha_aif1),
    },
    {
        .name="RT5671 Left Speaker",.stream_name="RT5671 Left SPK",
        .c2c_params=&speaker_params,.num_c2c_params=1,.ignore_pmdown_time=1,
        .dai_fmt=SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF|SND_SOC_DAIFMT_CBS_CFS,
        SND_SOC_DAILINK_REG(mocha_left),
    },
    {
        .name="RT5671 Right Speaker",.stream_name="RT5671 Right SPK",
        .c2c_params=&speaker_params,.num_c2c_params=1,.ignore_pmdown_time=1,
        .dai_fmt=SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF|SND_SOC_DAIFMT_CBS_CFS,
        SND_SOC_DAILINK_REG(mocha_right),
    },
};
static struct snd_soc_codec_conf amp_conf[]={
    {.name_prefix="Left Spk"}, {.name_prefix="Right Spk"},
};
static const struct snd_soc_dapm_widget widgets[]={
    SND_SOC_DAPM_SPK("Int Left Spk",NULL),
    SND_SOC_DAPM_SPK("Int Right Spk",NULL),
    SND_SOC_DAPM_HP("Headphone Jack",NULL),
    SND_SOC_DAPM_MIC("Mic Jack",NULL),
    SND_SOC_DAPM_MIC("Int Mic",NULL),
};
/* Exact MIUI board endpoints; AIF2 forwarding is selected by mixer controls. */
static const struct snd_soc_dapm_route routes[]={
    {"Headphone Jack",NULL,"HPOR"}, {"Headphone Jack",NULL,"HPOL"},
    {"IN1P",NULL,"Mic Jack"}, {"IN1N",NULL,"Mic Jack"},
    {"micbias2",NULL,"Int Mic"}, {"IN2P",NULL,"micbias2"},
    {"IN2N",NULL,"micbias2"}, {"IN4P",NULL,"micbias2"},
    {"IN4N",NULL,"micbias2"},
    {"Int Left Spk",NULL,"Left Spk Playback"},
    {"Int Right Spk",NULL,"Right Spk Playback"},
};
static const struct snd_kcontrol_new controls[]={
    SOC_DAPM_PIN_SWITCH("Int Left Spk"),SOC_DAPM_PIN_SWITCH("Int Right Spk"),
    SOC_DAPM_PIN_SWITCH("Headphone Jack"),SOC_DAPM_PIN_SWITCH("Int Mic"),
    SOC_DAPM_PIN_SWITCH("Mic Jack"),
};
static struct snd_soc_card card={.owner=THIS_MODULE,.driver_name="MochaMIUISpeakers",
    .components="codec:rt5671 amps:tfa9890",.dai_link=links,.num_links=ARRAY_SIZE(links),
    .codec_conf=amp_conf,.num_configs=ARRAY_SIZE(amp_conf),.fully_routed=true,
    .dapm_widgets=widgets,.num_dapm_widgets=ARRAY_SIZE(widgets),
    .dapm_routes=routes,.num_dapm_routes=ARRAY_SIZE(routes),
    .controls=controls,.num_controls=ARRAY_SIZE(controls),
};
static unsigned int mclk_rate(unsigned int rate){return 256*rate;}
static const struct tegra_asoc_data data={.mclk_rate=mclk_rate,.card=&card,
    .add_common_controls=false,.add_common_dapm_widgets=false};
static const struct of_device_id match[]={
    {.compatible="xiaomi,mocha-audio-rt5671-speakers",.data=&data},{}
};
MODULE_DEVICE_TABLE(of,match);
static void put_node(void *node) { of_node_put(node); }
static void release_power(void *power)
{
    regulator_disable(power);
    regulator_put(power);
}
static int full_probe(struct platform_device *pdev)
{
    struct device *dev=&pdev->dev;
    struct device_node *codec,*left,*right;
    struct regulator *power;
    int ret;
    codec=of_parse_phandle(dev->of_node,"nvidia,audio-codec",0);
    if (!codec) return -EINVAL;
    ret=devm_add_action_or_reset(dev,put_node,codec);if(ret)return ret;
    left=of_parse_phandle(dev->of_node,"nvidia,left-amplifier",0);
    if (!left) return -EINVAL;
    ret=devm_add_action_or_reset(dev,put_node,left);if(ret)return ret;
    right=of_parse_phandle(dev->of_node,"nvidia,right-amplifier",0);
    if (!right) return -EINVAL;
    ret=devm_add_action_or_reset(dev,put_node,right);if(ret)return ret;
    links[1].cpus[0].of_node=codec;links[2].cpus[0].of_node=codec;
    links[1].codecs[0].of_node=left;links[2].codecs[0].of_node=right;
    amp_conf[0].dlc.of_node=left;amp_conf[1].dlc.of_node=right;
    power=regulator_get(NULL,"ldoen");
    if(IS_ERR(power))return PTR_ERR(power);
    if(regulator_get_voltage(power)!=1200000){regulator_put(power);return -EINVAL;}
    ret=regulator_enable(power);
    if(ret){regulator_put(power);return ret;}
    ret=devm_add_action_or_reset(dev,release_power,power);if(ret)return ret;
    return tegra_asoc_machine_probe(pdev);
}
static struct platform_driver mocha_audio_driver={
    .driver={.name="mocha-miui-speakers",.of_match_table=match},
    .probe=full_probe,
};
module_platform_driver(mocha_audio_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mocha RT5671 and dual TFA9890 speaker links using MIUI board routing");
