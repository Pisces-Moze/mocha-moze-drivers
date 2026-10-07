# Mocha Moze Drivers

现代Linux6.12.111外置适配、官方MIUI GPL移植、固件来源说明与实机诊断。完整内核源码在linux仓库，避免重复维护树内USB/PMIC/DSI驱动。

| 目录 | 内容 | 状态 |
|---|---|---|
| backlight | mocha_miui_backlight，LP8556 MIUI寄存器设置 | 实际亮度调节通过 |
| audio | TFA9890移植、RT5671 machine、电源/MCLK探测 | 编译通过，codec NACK；扬声器/麦克风未完成 |
| touch | Atmel接入说明与失败Synaptics移植参考 | 默认Atmel准确；Synaptics不得默认加载 |
| diagnostics | 原生DMA-BUF色块、EGL、frame timing、scanout读取 | 独立色块真实显示；不是完整桌面 |
| cuda | Driver API自检、PTX、Gdev适配差异 | 有限计算通过；CUDA6.5runtime未通过 |
| firmware | 从自己的官方MIUI/合法软件包提取的方法 | 不分发专有固件与设备校准 |

```sh
bash tools/build-backlight.sh ../mocha-moze-linux ../artifacts/kernel
# 安装到同一release的模块目录，depmod后加载；不要自动启用音频实验。
sudo make -C ../mocha-moze-linux O="$(realpath ../artifacts/kernel)" \
 ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= M="$PWD/backlight" \
 INSTALL_MOD_PATH="$(realpath ../artifacts/rootfs)" INSTALL_MOD_STRIP=1 modules_install
```

ALSA/PipeWire服务启动不等于扬声器存在；Wi-Fi菜单出现不等于蓝牙硬件配对完成。
RT5671研究下一步应继续验证官方电源序列、bus、reset、clock及实际寄存器响应，再注册完整ASoC卡。TFA MTP零值不能直接判定未校准，禁止为试音盲写MTP或绕过保护。
树内RT5670 codec仅作为接口参考，不代表RT5671全兼容已经证明。

默认显示仍慢；diagnostics/native-dmabuf-scanout-v2.c不做CPU像素复制，可帮助开发原生Tegra显示。
在native内核环境里显式传入KMS卡与Nouveau render node（参数顺序KMS在前）。不能假设card0总是Tegra或renderD128总是Nouveau。
例：`./native-dmabuf-scanout /dev/dri/card1 /dev/dri/renderD129`。运行前停止桌面；确认实际出图后才报告成功。

全部问题与证据见[ISSUES](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/docs/ISSUES.md)。
