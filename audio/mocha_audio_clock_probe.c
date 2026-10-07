// SPDX-License-Identifier: GPL-2.0-only
/* Temporary audio-only MCLK diagnostic; restore parents/rates on removal. */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <dt-bindings/clock/tegra124-car.h>
#include <dt-bindings/soc/tegra-pmc.h>
static struct clk *a,*out,*ext,*pin,*old_ext,*old_pin;
static unsigned long old_a,old_out;
static bool enabled;
static struct clk *get(const char *path,int id)
{
 struct of_phandle_args p={.np=of_find_node_by_path(path),.args_count=1,.args={id}};
 struct clk *c;
 if(!p.np)return ERR_PTR(-ENOENT);
 c=of_clk_get_from_provider(&p);of_node_put(p.np);return c;
}
static void restore(void)
{
 if(enabled)clk_disable_unprepare(pin);
 if(!IS_ERR_OR_NULL(pin)&&old_pin)clk_set_parent(pin,old_pin);
 if(!IS_ERR_OR_NULL(ext)&&old_ext)clk_set_parent(ext,old_ext);
 if(!IS_ERR_OR_NULL(out)&&old_out)clk_set_rate(out,old_out);
 if(!IS_ERR_OR_NULL(a)&&old_a)clk_set_rate(a,old_a);
 if(!IS_ERR_OR_NULL(pin))clk_put(pin);
 if(!IS_ERR_OR_NULL(ext))clk_put(ext);
 if(!IS_ERR_OR_NULL(out))clk_put(out);
 if(!IS_ERR_OR_NULL(a))clk_put(a);
}
static int __init start(void)
{
 int ret;
 a=get("/clock@60006000",TEGRA124_CLK_PLL_A);
 out=get("/clock@60006000",TEGRA124_CLK_PLL_A_OUT0);
 ext=get("/clock@60006000",TEGRA124_CLK_EXTERN1);
 pin=get("/pmc@7000e400",TEGRA_PMC_CLK_OUT_1);
 if(IS_ERR(a)||IS_ERR(out)||IS_ERR(ext)||IS_ERR(pin)){ret=-ENODEV;goto error;}
 old_a=clk_get_rate(a);old_out=clk_get_rate(out);old_ext=clk_get_parent(ext);old_pin=clk_get_parent(pin);
 pr_info("MOCHA_AUDIO_MCLK: inherited pll_a=%lu out=%lu pin=%lu\n",old_a,old_out,clk_get_rate(pin));
 ret=clk_set_rate(a,368640000);if(ret)goto error;
 ret=clk_set_rate(out,12288000);if(ret)goto error;
 ret=clk_set_parent(ext,out);if(ret)goto error;
 ret=clk_set_parent(pin,ext);if(ret)goto error;
 ret=clk_prepare_enable(pin);if(ret)goto error;
 enabled=true;pr_info("MOCHA_AUDIO_MCLK: temporary MIUI 48kHz MCLK=%lu\n",clk_get_rate(pin));return 0;
error:restore();return ret;
}
static void __exit stop(void){restore();}
module_init(start);module_exit(stop);MODULE_LICENSE("GPL");
