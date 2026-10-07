// SPDX-License-Identifier: GPL-2.0-only
/* Mocha RT5671 routing and codec-master clock arrangement from MIUI
 * tegra_rt5671.c / board-ardbeg.c, expressed through Linux 6.12 ASoC.
 * Speaker DSP/amplifiers deliberately remain outside this first codec test.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
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
static struct snd_soc_dai_link link={
    .name="Mocha RT5671",.stream_name="Mocha HiFi",
    .init=tegra_asoc_machine_init,.ops=&ops,
    .dai_fmt=SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_NB_NF|SND_SOC_DAIFMT_CBM_CFM,
    SND_SOC_DAILINK_REG(mocha_aif1),
};
static struct snd_soc_card card={.owner=THIS_MODULE,.driver_name="MochaRT5671",
    .components="codec:rt5671",.dai_link=&link,.num_links=1,.fully_routed=true};
static unsigned int mclk_rate(unsigned int rate){return 256*rate;}
static const struct tegra_asoc_data data={.mclk_rate=mclk_rate,.card=&card,
    .add_common_controls=true,.add_common_dapm_widgets=true};
static const struct of_device_id match[]={
    {.compatible="xiaomi,mocha-audio-rt5671",.data=&data},{}
};
MODULE_DEVICE_TABLE(of,match);
static struct platform_driver mocha_audio_driver={
    .driver={.name="mocha-miui-rt5671",.of_match_table=match},
    .probe=tegra_asoc_machine_probe,
};
module_platform_driver(mocha_audio_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mocha RT5671 codec-master audio using MIUI board routing");
