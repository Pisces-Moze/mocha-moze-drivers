// SPDX-License-Identifier: GPL-2.0-only
/* LP8556 register control adapted from MiCode Mocha lp855x_bl.c.
 * Preserve the proven bootloader configuration at probe. Only brightness and
 * the enable bit change during normal use; recover the official register set
 * only when the chip reports it lost configuration after being switched off.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/delay.h>
struct mocha_bl { struct i2c_client *client; struct backlight_device *bl; };
static int update(struct backlight_device *bl) {
    struct mocha_bl *m=bl_get_data(bl);
    int b=backlight_get_brightness(bl), ctrl, ret, i;
    static const u8 recover[][2]={{0x16,0x0f},{0x98,0x80},{0x9e,0x21},{0xa1,0x3f},{0xa3,0x00},{0xa5,0x24},{0xa7,0xf5},{0xa9,0xb2},{0xaa,0x8f},{0x01,0x82}};
    ctrl=i2c_smbus_read_byte_data(m->client,1);
    if(ctrl<0) return ctrl;
    if(b && !(ctrl&0x80)) {
        for(i=0;i<ARRAY_SIZE(recover);i++) {
            ret=i2c_smbus_write_byte_data(m->client,recover[i][0],recover[i][1]);
            if(ret<0) return ret;
        }
        ctrl=0x82;usleep_range(1000,3000);
    }
    ret=i2c_smbus_write_byte_data(m->client,0,b);
    if(ret<0)return ret;
    usleep_range(1000,3000);
    return i2c_smbus_write_byte_data(m->client,1,b?(ctrl|1):(ctrl&~1));
}
static int read_brightness(struct backlight_device *bl) {
    struct mocha_bl *m=bl_get_data(bl);
    return i2c_smbus_read_byte_data(m->client,0);
}
static const struct backlight_ops ops={.options=BL_CORE_SUSPENDRESUME,.update_status=update,.get_brightness=read_brightness};
static int probe(struct i2c_client *client) {
    struct backlight_properties props={.type=BACKLIGHT_PLATFORM,.max_brightness=255};
    struct mocha_bl *m; int brightness,ctrl;
    if(!i2c_check_functionality(client->adapter,I2C_FUNC_SMBUS_BYTE_DATA))return -EOPNOTSUPP;
    brightness=i2c_smbus_read_byte_data(client,0);ctrl=i2c_smbus_read_byte_data(client,1);
    if(brightness<0)return brightness;
    if(ctrl<0)return ctrl;
    m=devm_kzalloc(&client->dev,sizeof(*m),GFP_KERNEL);if(!m)return -ENOMEM;
    m->client=client;props.brightness=brightness;
    m->bl=devm_backlight_device_register(&client->dev,"mocha-miui-backlight",&client->dev,m,&ops,&props);
    if(IS_ERR(m->bl))return PTR_ERR(m->bl);
    i2c_set_clientdata(client,m);
    dev_info(&client->dev,"Mocha MIUI backlight: inherited brightness=%d ctrl=%#x\n",brightness,ctrl);
    return 0;
}
static const struct of_device_id match[]={{.compatible="xiaomi,mocha-miui-lp8556"},{}};
MODULE_DEVICE_TABLE(of,match);
static struct i2c_driver mocha_backlight_driver={.driver={.name="mocha-miui-backlight",.of_match_table=match},.probe=probe};
module_i2c_driver(mocha_backlight_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xiaomi Mocha MIUI-compatible LP8556 backlight, preserving boot state");
