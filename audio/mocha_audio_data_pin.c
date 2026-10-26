// SPDX-License-Identifier: GPL-2.0-only
/* Mocha-only reversible DAP1 DOUT pin test. Existing boot state: tristate=1. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include "pinctrl-tegra.h"

static struct platform_device *pdev;
static struct pinctrl *pctl;
static struct pinctrl_state *off;
static unsigned long enable_cfg[] = { TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, 0) };
static unsigned long restore_cfg[] = { TEGRA_PINCONF_PACK(TEGRA_PINCONF_PARAM_TRISTATE, 1) };
static const struct pinctrl_map maps[] = {
 PIN_MAP_CONFIGS_GROUP("mocha-audio-data-pin", "enable", "70000868.pinmux", "dap1_dout_pn2", enable_cfg),
 PIN_MAP_CONFIGS_GROUP("mocha-audio-data-pin", "restore", "70000868.pinmux", "dap1_dout_pn2", restore_cfg),
};
static int __init start(void)
{
 struct pinctrl_state *on;
 int ret;
 if (!of_machine_is_compatible("nvidia,mocha")) return -ENODEV;
 ret = pinctrl_register_mappings(maps, ARRAY_SIZE(maps));
 if (ret) return ret;
 pdev = platform_device_register_simple("mocha-audio-data-pin", -1, NULL, 0);
 if (IS_ERR(pdev)) { ret = PTR_ERR(pdev); goto unmap; }
 pctl = pinctrl_get(&pdev->dev);
 if (IS_ERR(pctl)) { ret = PTR_ERR(pctl); goto device; }
 off = pinctrl_lookup_state(pctl, "restore");
 on = pinctrl_lookup_state(pctl, "enable");
 if (IS_ERR(off) || IS_ERR(on)) { ret = -ENOENT; goto put; }
 ret = pinctrl_select_state(pctl, on);
 if (ret) { pinctrl_select_state(pctl, off); goto put; }
 pr_info("MOCHA_AUDIO_DATA: DAP1 DOUT tristate disabled; I2S function unchanged\n");
 return 0;
put:
 pinctrl_put(pctl);
device:
 platform_device_unregister(pdev);
unmap:
 pinctrl_unregister_mappings(maps);
 return ret;
}
static void __exit stop(void)
{
 if (pinctrl_select_state(pctl, off)) pr_err("MOCHA_AUDIO_DATA: failed to restore DOUT tristate\n");
 pinctrl_put(pctl);
 platform_device_unregister(pdev);
 pinctrl_unregister_mappings(maps);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reversible Mocha DAP1 I2S data-output enable test");
