# 触控批次与官方配置

本机为Atmel maXTouch1664T：I2C0x4a、IRQ GPIO143低电平、reset GPIO84。默认驱动是Linux树内atmel_mxt_ts，无需额外ko。
官方Mocha MIUI的Atmel配置与本机含dummy的配置字节比对一致；没有给触控控制器刷固件。
横屏配置由Niri transform和map-to-output共同完成，实测点击/滑动准确。

Mocha存在不同触控/面板批次；experimental-synaptics中的旧MIUI GPL移植是失败路径参考，不是本机默认驱动，不得盲刷其固件。
`diagnostics/read-atmel-config.c`可读取对象表/配置作对照，先确认实际I2C设备与固件版本。
