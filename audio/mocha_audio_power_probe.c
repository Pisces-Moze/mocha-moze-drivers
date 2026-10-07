// SPDX-License-Identifier: GPL-2.0-only
/* MIUI's codec basic power: hold the existing fixed ldoen regulator.
 * No voltage changes. All acquisition/enabling is balanced on unload.
 */
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mfd/palmas.h>
static struct regulator *codec_power;
static bool enable_32k;
module_param(enable_32k,bool,0444);
static struct device *pmic_dev;
static struct palmas *pmic;
static unsigned int saved_pad,saved_audio,saved_general;
static bool clock_changed;
static void restore_clock(void)
{
 if(clock_changed){
  palmas_update_bits(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KG_CTRL,1,saved_general);
  palmas_update_bits(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KGAUDIO_CTRL,1,saved_audio);
  palmas_update_bits(pmic,PALMAS_PU_PD_OD_BASE,PALMAS_PRIMARY_SECONDARY_PAD2,6,saved_pad);
 }
 if(pmic_dev)put_device(pmic_dev);
 pmic_dev=NULL;clock_changed=false;
}
static void log_pmic(void)
{
 struct device *dev=bus_find_device_by_name(&i2c_bus_type,NULL,"4-0058");
 struct palmas *p;
 unsigned int pad=0,in=0,dir=0,out=0,kg=0,ka=0;
 if(!dev)return;
 p=dev_get_drvdata(dev);
 if(p){
  palmas_read(p,PALMAS_PU_PD_OD_BASE,PALMAS_PRIMARY_SECONDARY_PAD2,&pad);
  palmas_read(p,PALMAS_GPIO_BASE,PALMAS_GPIO_DATA_IN,&in);
  palmas_read(p,PALMAS_GPIO_BASE,PALMAS_GPIO_DATA_DIR,&dir);
  palmas_read(p,PALMAS_GPIO_BASE,PALMAS_GPIO_DATA_OUT,&out);
  palmas_read(p,PALMAS_RESOURCE_BASE,PALMAS_CLK32KG_CTRL,&kg);
  palmas_read(p,PALMAS_RESOURCE_BASE,PALMAS_CLK32KGAUDIO_CTRL,&ka);
  pr_info("MOCHA_AUDIO_POWER: PAD2=%02x GPIO in=%02x dir=%02x out=%02x clk32kg=%02x audio=%02x\n",pad,in,dir,out,kg,ka);
 }
 put_device(dev);
}
static int __init audio_power_init(void)
{
 int ret;
 if(enable_32k){
  pmic_dev=bus_find_device_by_name(&i2c_bus_type,NULL,"4-0058");
  if(!pmic_dev)return -ENODEV;
  pmic=dev_get_drvdata(pmic_dev);
  if(!pmic){ret=-ENODEV;goto clock_fail;}
  ret=palmas_read(pmic,PALMAS_PU_PD_OD_BASE,PALMAS_PRIMARY_SECONDARY_PAD2,&saved_pad);
  if(ret)goto clock_fail;
  ret=palmas_read(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KGAUDIO_CTRL,&saved_audio);
  if(ret)goto clock_fail;
  ret=palmas_read(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KG_CTRL,&saved_general);
  if(ret)goto clock_fail;
  clock_changed=true;
  /* Exact official Mocha pin_gpio5 = clk32kgaudio, clock-boot-enable. */
  ret=palmas_update_bits(pmic,PALMAS_PU_PD_OD_BASE,PALMAS_PRIMARY_SECONDARY_PAD2,6,2);
  if(ret)goto clock_fail;
  ret=palmas_update_bits(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KGAUDIO_CTRL,1,1);
  if(ret)goto clock_fail;
  /* Official Mocha enables both clk32k_kg and clk32k_kg_audio at boot. */
  ret=palmas_update_bits(pmic,PALMAS_RESOURCE_BASE,PALMAS_CLK32KG_CTRL,1,1);
  if(ret)goto clock_fail;
 }
 codec_power=regulator_get(NULL,"ldoen");
 if(IS_ERR(codec_power)){ret=PTR_ERR(codec_power);goto clock_fail;}
 if(regulator_get_voltage(codec_power)!=1200000){ret=-EINVAL;goto fail;}
 ret=regulator_enable(codec_power);if(ret)goto fail;
 msleep(200);
 pr_info("MOCHA_AUDIO_POWER: ldoen enabled at existing 1200000 uV\n");
 log_pmic();
 return 0;
fail:regulator_put(codec_power);
clock_fail:restore_clock();return ret;
}
static void __exit audio_power_exit(void)
{
 regulator_disable(codec_power);regulator_put(codec_power);
 restore_clock();
}
module_init(audio_power_init);module_exit(audio_power_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary Mocha MIUI codec power hold via regulator API");
