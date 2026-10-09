# Mocha Moze Drivers

> 让 Debian armhf 跑在小米平板 1 上的树外驱动适配层：MIUI GPL 驱动移植、固件来源说明与实机诊断工具。

[![License: GPL-2.0-only](https://img.shields.io/badge/License-GPL--2.0--only-blue.svg)](LICENSE)

目标设备是小米平板 1（型号 A0101，NVIDIA Tegra124，代号 Mocha），系统为 Debian armhf，内核 6.12.111-moze.1。这个仓库只放树外部分：官方 MIUI 内核里的 GPL 驱动往 6.12 上移的成果、补丁、探测程序和实机诊断工具。

## 简介

完整内核源码在 mocha-moze-linux 仓库，本仓库刻意不重复维护树内 USB、PMIC、DSI 驱动。这样分工的原因是树外模块换起来不用重刷内核，调不通的实验代码也不会污染内核树。

同属这个工程的还有 [mocha-moze-debian](https://github.com/Pisces-Moze/mocha-moze-debian)（总入口、rootfs 与安装文档）、[mocha-moze-boot](https://github.com/Pisces-Moze/mocha-moze-boot)（U-Boot 链式引导）、[mocha-moze-linux](https://github.com/Pisces-Moze/mocha-moze-linux)（6.12.111 内核与两套设备树）和 [mocha-moze-desktop](https://github.com/Pisces-Moze/mocha-moze-desktop)（Niri/Noctalia 桌面与充电界面）。

## 目录与模块

| 目录 | 内容 | 状态 | 限制 |
|---|---|---|---|
| `backlight/` | `mocha_miui_backlight`，LP8556 MIUI 寄存器设置 | 实际亮度调节通过 | 只改亮度寄存器与使能位，保留 bootloader 已写好的配置 |
| `audio/` | TFA9890 移植、RT5671 machine、电源与 MCLK 探测 | 移植与 machine 编译通过，codec NACK；扬声器与麦克风未完成 | TFA 侧 MTP 保护开放着，未接通完整链路 |
| `touch/` | Atmel 接入说明，失败的 Synaptics 移植存档 | 默认 Atmel 准确；Synaptics 不得默认加载 | Synaptics 仅供对照，不能盲刷固件 |
| `diagnostics/` | 原生 DMA-BUF 色块、EGL 渲染探测、frame timing、scanout 读取、Atmel 配置读取 | 独立色块真实显示；不是完整桌面 | 需要临时 native 启动，运行前要停桌面 |
| `cuda/` | Driver API 自检、PTX、Gdev 适配差异 | 有限整数计算通过；CUDA 6.5 runtime 未通过 | 不宣称支持任意 CUDA 程序或完整 Runtime |
| `firmware/` | 从自己的官方 MIUI / 合法软件包提取固件的方法 | 只写方法，不含二进制 | 不分发专有固件与设备校准数据 |
| `tools/` | `build-backlight.sh` | 可用 | 依赖已构建完成的 O= 输出目录 |

### backlight

`mocha_miui_backlight.c` 出自 MiCode Mocha 的 `lp855x_bl.c`，改成 I2C 驱动形态。probe 时继承 bootloader 留下的亮度和控制寄存器状态，正常使用只写亮度寄存器 `0x00` 和使能位；只有芯片报告在断电后丢失配置时，才按 `recover[]` 表恢复那 10 个官方寄存器值。设备树 compatible 为 `xiaomi,mocha-miui-lp8556`，`max_brightness` 为 255。这个驱动在实机上亮度调节有效。

2026-10-10 新 native 内核的 RAM 实测中，修复后的匹配模块 `insmod` 返回 0，继承亮度 31，成功注册背光；随后的面板延迟 probe 完成，Tegra DRM 变为 connected/enabled。色块期间临时写入 96 并恢复 31，用户确认背光亮但黑屏。模块加载已验证，物理图像输出仍未通过；这次没有验证亮度刻度关系。记录见总入口的 [RAM 诊断](https://github.com/Pisces-Moze/mocha-moze-debian/blob/codex/mocha-diagnostics-2026-10-09/docs/DIAGNOSTICS-2026-10-10.md)。

### audio

树外代码覆盖三块内容：TFA9890 codec 移植、RT5671 machine 驱动、以及两个临时探测模块。编译通过，但 codec 侧返回 NACK，扬声器和麦克风都还没打通。

`mocha_rt5671_machine.c` 把 MIUI 的 `tegra_rt5671.c` / `board-ardbeg.c` 里的路由和 codec-master 时钟安排翻成 Linux 6.12 的 ASoC 写法。采样率按 368640000（8k 系）或 282240000（11.025k 系）选 PLL_A 基准，MCLK 取 256×rate，codec 侧 PLL1 取 512×rate。compatible 为 `xiaomi,mocha-audio-rt5671`。扬声器 DSP 和功放被刻意留在第一轮 codec 测试之外。

`mocha_miui_speaker_machine.c` 是更进一步的候选：用两条 codec-to-codec link 复现 MIUI 的 RT5671 AIF2 到左右 TFA9890 接线，功放驱动的 DSP 保护由放大器驱动负责，扬声器 link 固定 48 kHz、立体声、16 bit。probe 阶段还会取 `ldoen` 稳压器并要求电压为 1200000 uV。这个候选没有被默认桌面启动加载。

`mocha_miui_tfa98xx.c` 是 MIUI `tfa98xx.c` 移植到 6.12 的结果，`miui-tfa98xx-linux612.patch` 记录了实际差异。补丁改的是 `snd_soc_codec` 到 `snd_soc_component` 的接口迁移、寄存器缓存的实现方式、`.digital_mute` 到 `.mute_stream`、`.symmetric_rates` 到 `.symmetric_rate`、以及 probe 改用 `devm_snd_soc_register_component`。原来的 one-time calibration 写 MTP 逻辑没有保留：现在的 `tfa98xx_enable_otc()` 读到 `TFA98XX_MTP_SPKR_CAL`（寄存器 `0x80`）低两位不是 `3` 时直接打印 "Factory speaker calibration unavailable; refusing MTP programming" 并返回 `-EOPNOTSUPP`。MTP 零值不能直接判定未校准，禁止为试音盲写 MTP 或绕过保护。

`mocha_audio_clock_probe.c` 和 `mocha_audio_power_probe.c` 是临时诊断模块，不是产品驱动。前者把 PLL_A 设到 368640000、PLL_A_OUT0 设到 12288000，再把 EXTERN1 和 PMC CLK_OUT_1 接上去，卸载时恢复原父子关系和频率。后者持有现有的 `ldoen`（1.2 V，不改电压），并且只在 `enable_32k=1` 时按官方 Mocha 配置改 `PRIMARY_SECONDARY_PAD2`、`CLK32KGAUDIO_CTRL`、`CLK32KG_CTRL`，卸载时写回原值。

### touch

本机触控是 Atmel maXTouch 1664T：I2C 地址 `0x4a`、IRQ GPIO143 低电平有效、reset GPIO84。默认驱动用 Linux 树内 `atmel_mxt_ts`，不需要额外的 ko。官方 Mocha MIUI 的 Atmel 配置和本机含 dummy 的配置字节比对一致，没有给触控控制器刷过固件。横屏由 Niri 的 transform 和 map-to-output 一起完成，实测点击和滑动准确。

`experimental-synaptics/` 保存的是旧 MIUI GPL 移植的 Synaptics DSX 驱动（`synaptics_dsx` / `synaptics_dsx_i2c`，compatible `synaptics,dsx-i2c`），这是一条失败路径的参考。Mocha 存在不同触控与面板批次，把它当默认驱动加载或盲刷它的固件都会让正常机器失去触控。

### diagnostics

五个独立程序，都直接跑在实机上，没有构建系统，用交叉编译器逐个编。

| 程序 | 作用 | 输出 |
|---|---|---|
| `native-dmabuf-scanout-v2.c` | GPU 渲染 → 导出 DMA-BUF → Tegra KMS 扫描输出，全程不做 CPU 像素复制 | 四象限色块与 `NATIVE_GPU_SCANOUT_FRAMES`、`FPS`、`CPU_PIXEL_COPIES=0` |
| `frame-bench.c` | Wayland + EGL 下的帧间隔统计 | `FRAMES`、`FPS`、`MEAN_SWAP_MS`、`MAX_FRAME_GAP_MS`、`GAPS_OVER_25MS` |
| `probe-egl-render.c` | surfaceless EGL pbuffer 纯色三角形与 `glReadPixels` 校验 | `RENDERER`、`PIXEL`、`RENDER_TEST`、`HARDWARE_RENDERING` |
| `read-drm-scanout.c` | 读 CRTC、FB2 与 plane 属性（含 blob 前 32 个 u16） | `CRTC=`、`FB=`、`PLANE=`、`BLOB_BYTES=` 行 |
| `read-atmel-config.c` | 只读读取 maXTouch 对象表与配置，跳过消息 FIFO（T5）对象 | 一行 JSON，含 `info`、`objects[]`、各对象 `data` |

`read-atmel-config.c` 里设备节点写死为 `/dev/i2c-2`、地址写死 `0x4a`。总线号不是参数，换了机器要自己核对实际 I2C 设备与固件版本。

### cuda

GK20A 是 Kepler GPU，Nouveau 图形渲染可用不等于 NVIDIA CUDA 用户态 ABI 可用。CUDA 6.5 / L4T 21 的旧用户态依赖 nvhost、nvmap 这些旧接口，现代 Linux Nouveau 不会自动提供。

`gdev-mocha.patch` 是对 `shinpei0208/gdev` master 源码档的实际差异，上游归档 SHA256 为 `c54f137616525e8b0357328688043f7bc5766e236f5dd6f4a89e38e873561d8b`。应用前核对自己的归档，移动的 master 不能当固定版本。差异集中在几处：集成 GK20A 不是 PCI 设备，SM 数量要查真实值、compute capability 报 3.2；GK20A 与系统共享内存、没有独立 VRAM，显存大小查询回落到物理页统计；拷贝路径增加 cache 同步与 `nouveau_bo_wait`；pushbuf 提交前把 code / data / local memory 全部重新引用并 validate；代码上传后加了一次回读比对，打印 `MOCHA_CODE_READBACK`；设备计数改走 Nouveau 节点枚举。修复项还包括 32/64 位查询写入、GART 映射、pushbuf 资源引用、ARM 缓存与代码上传同步。

Gdev 是实验性质的 Driver API 实现。用它做有限整数计算，257 和 8193 个元素的输出正确。Driver API 报告 4000，CUDA 6.5 的 libcudart 返回 error 35。开发包里的 `libcuda` stub 的 `cuInit` 返回 -1，不能拿它当可用性检查。

`probe-gdev-cuda.c` 查初始化和设备计数，加 `--memory` 时做一次 64 个 u32 的上传下载比对；`probe-gdev-kernel.c` 加载自己组装的 cubin、启动 `add_seven` 内核并逐元素校验；`gdev-add-seven.ptx` 是那个加 7 内核，`.target sm_32`。

```
cc -O2 probe-gdev-kernel.c -ldl -o probe-gdev-kernel
./probe-gdev-kernel <libcuda.so 路径> <cubin 路径> [元素数]
```

## 构建与部署

前置条件：交叉工具链前缀 `arm-linux-gnueabihf-`、`kmod` 提供的 `modinfo`，已经在 `mocha-moze-linux` 里构建完成的目标 profile 的 `O=` 输出目录，且里面有 `Module.symvers` 与 `include/config/kernel.release`。

`tools/build-backlight.sh` 做的就是这件事：

```sh
bash tools/build-backlight.sh ../mocha-moze-linux ../artifacts/kernel
```

脚本先对外部模块执行 `make ... M="$PWD/backlight" clean`，再执行下面的编译命令，最后检查 `.ko` 的 vermagic 中的 release 与目标 `O=` 完全一致。切换 `O=` 时，Kbuild 可能复用另一套内核生成的 `.mod.o`；本次实际重现 native 构建返回成功却留下 stable vermagic，清理与断言用于防止这种混用。

```sh
make -C ../mocha-moze-linux O="$(realpath ../artifacts/kernel)" \
  ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= \
  M="$PWD/backlight" modules
```

几个变量的含义：`O=` 指向内核 out-of-tree 构建输出目录，脚本要求该目录已存在 `Module.symvers`，否则直接退出；`ARCH=arm` 与 `CROSS_COMPILE=arm-linux-gnueabihf-` 决定交叉工具链；`M="$PWD/backlight"` 说明这是外部模块，只编 backlight 目录；`LOCALVERSION=` 清空内核本地版本后缀。

`LOCALVERSION` 会进 vermagic。脚本清空 shell 的 `LOCALVERSION`，避免意外追加后缀；内核配置的 `CONFIG_LOCALVERSION` 仍保留，所以 stable 的目标 release 是 `6.12.111-moze.1`，native 是 `6.12.111-moze.1-native`。模块必须匹配实际启动内核的完整 release。

2026-10-10 已针对两份完整内核输出执行 stable→native→stable 交叉构建；三次检查均匹配对应 release。修复后的 native 模块 SHA256 为 `5cb1c425c994e60cb5cb38a82a6ed946179a3fd55fa3d254bba317a4f36e1f10`，stable 为 `628848de93211c37659d50f06bf1795f67c52395e7675477402335374ad8daaf`。本次只证明模块构建与版本匹配，新内核上的加载及实际亮度变化仍待实机验收。

把模块装到同一个 release 的模块目录，再跑 depmod：

```sh
sudo make -C ../mocha-moze-linux O="$(realpath ../artifacts/kernel)" \
  ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= \
  M="$PWD/backlight" INSTALL_MOD_PATH="$(realpath ../artifacts/rootfs)" \
  INSTALL_MOD_STRIP=1 modules_install
sudo depmod -a -b "$(realpath ../artifacts/rootfs)" 6.12.111-moze.1
```

`INSTALL_MOD_PATH` 是根文件系统镜像的挂载点或 staging 目录，`modules_install` 会把 ko 放进它的 `lib/modules/<内核 release>/` 下，这里的 release 由内核源码的版本和 LOCALVERSION 一起决定，也就是模块必须落进和内核同一个 release 目录，跨 release 放进去不会生效。`INSTALL_MOD_STRIP=1` 装的时候剥掉调试符号。`depmod -b` 的根目录必须和 `INSTALL_MOD_PATH` 一致，后面那个版本号参数要和内核 release 完全相同。加载时不要自动启用音频实验模块。

音频侧按同样方式编，但 `audio/Makefile` 只列了 `mocha_miui_tfa98xx.o` 和 `mocha_miui_speaker_machine.o`，另外两个探测模块需要时单独指定。ccflags 已经指向 `$(srctree)/sound/soc/tegra` 和 `$(srctree)/sound/soc/codecs`，`mocha_rt5671_machine.c` 和 `mocha_miui_speaker_machine.c` 都 include 了树内的 `tegra_asoc_machine.h` 和 `rt5670.h`，编之前确认内核源码里这两个头文件在。

```sh
sudo make -C ../mocha-moze-linux O="$(realpath ../artifacts/kernel)" \
  ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LOCALVERSION= \
  M="$PWD/audio" modules
```

树内的 RT5670 codec 只作接口参考，不代表 RT5671 全兼容已经证明。

`miui-tfa98xx-linux612.patch` 记录的是 `miui/tfa98xx.c` 到 `linux612/mocha_miui_tfa98xx.c` 的全部差异，按 hunks 逐行生成。patch 头里的两个路径只是标签，不对应仓库里的真实目录。

仓库里的 `mocha_miui_tfa98xx.c` 已经是打过补丁之后的成品，正常构建不需要再打一次。只有当你自己从 MIUI 内核源码取到那份 `tfa98xx.c`、想复现这次移植时，才用得上：

```sh
# 先从自己的 MIUI 内核源码取原始文件，再核对差异
diff -u miui/tfa98xx.c audio/mocha_miui_tfa98xx.c | less
```

直接 `patch -p0 miui/tfa98xx.c < audio/miui-tfa98xx-linux612.patch` 之前要确认行号能对上，补丁的上下文来自特定的一份 MIUI 源码。

### 诊断程序编译与运行

编译都在开发机上做，用交叉编译器逐个产物：

```sh
arm-linux-gnueabihf-gcc -O2 diagnostics/probe-egl-render.c -o probe-egl-render -lEGL -lGLESv2
arm-linux-gnueabihf-gcc -O2 diagnostics/read-drm-scanout.c -o read-drm-scanout -ldrm
arm-linux-gnueabihf-gcc -O2 diagnostics/read-atmel-config.c -o read-atmel-config
arm-linux-gnueabihf-gcc -O2 diagnostics/native-dmabuf-scanout-v2.c -o native-dmabuf-scanout -ldrm -lgbm -lEGL -lGLESv2
```

`native-dmabuf-scanout-v2.c` 用到 `gbm_bo_get_modifier()`，需要较新的 libgbm；`read-drm-scanout.c` 用到 `drmModeGetFB2()`，需要 libdrm 2.4.62 以上。这两条是按源码 include 写出来的，没有在本地验证过。

`frame-bench.c` 多一个步骤，它 include `xdg-shell-client-protocol.h`，这个头文件和配套的 `.c` 要先用 wayland-scanner 从协议 XML 生成，再一起链接：

```sh
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
arm-linux-gnueabihf-gcc -O2 diagnostics/frame-bench.c xdg-shell-protocol.c \
  -o frame-bench -lwayland-client -lwayland-egl -lEGL -lGLESv2
```

`frame-bench.c` 要在桌面会话里跑，需要 compositor 支持 xdg-shell。`probe-egl-render.c` 用 surfaceless 平台，不需要窗口系统，也不占 KMS。

### native-dmabuf-scanout 的运行方式

这个程序只应该在临时 native console 启动下运行，不能有 compositor 占着 KMS。它启动时会读 `/proc/cmdline`，找不到 `mocha_native_debug4=1` 就直接退出。

```sh
./native-dmabuf-scanout /dev/dri/card1 /dev/dri/renderD129
```

参数顺序是 KMS 卡在前、render node 在后，两个都不能省。程序会校验第一个参数后面的 DRM 驱动名必须是 `tegra`、第二个必须是 `nouveau`，不匹配就报 `unexpected DRM devices`。不要假设 `card0` 总是 Tegra、`renderD128` 总是 Nouveau，节点编号会随加载顺序和设备树变化，先自己确认。

运行前停桌面，让 native console 持有 CRTC。程序要求当前 CRTC 上已经有一个有效 mode，并且面板尺寸正好是 1536×2048，还会拒绝 llvmpipe / softpipe 之类的软件渲染器。它会自己保存并恢复 console 状态。

判断标准是实际出图。`NATIVE_GPU_SCANOUT_STARTED` 只说明 modeset 成功，不等于屏幕上有东西；在看到四象限色块之后才把结果记成通过。程序结束打印 `NATIVE_GPU_SCANOUT_FRAMES`、`SECONDS`、`FPS` 和 `CPU_PIXEL_COPIES=0`。默认显示仍然慢，这个程序不做 CPU 像素复制，可以用来帮开发原生 Tegra 显示。

### CUDA 自检

```sh
cc -O2 cuda/probe-gdev-cuda.c -ldl -o probe-gdev-cuda
./probe-gdev-cuda /path/to/libcuda.so
./probe-gdev-cuda /path/to/libcuda.so --memory
```

运行时显式指定 libcuda 和 cubin 的路径。专有 nvcc / ptxas / cubin 和 NVIDIA 库都不随仓库分发，需要自己在合法持有 CUDA 6.5 或 L4T 21 包的环境里产出。

## 固件来源与合规

固件一律从自己合法持有的官方 MIUI 包或原系统分区提取，仓库不存放、不分发这些文件。

优先参考 MiCode 官方 `mocha-kk-oss` 源码，commit `79b4898e25fe3b506ff902e182b47068598c838a`，以及自己持有的 MIUI V9.2.4.0 包与原系统分区。官方驱动源码可以按 GPL 移植；把 MIUI 驱动二进制直接复制到 Debian 不能解决内核 ABI 和设备树不匹配的问题。

1. 在电脑上解开自己取得的 MIUI `system.img`（Android sparse 先用 `simg2img`），只读 loop 挂载，或用 `debugfs` 读文件。
2. 在 `system/etc/firmware`、`vendor/firmware` 等目录找 BCM4354 Wi-Fi firmware / NVRAM、BCM 蓝牙 HCD、触控配置和 TFA DSP 资产。
3. 无线 firmware 放进 `/lib/firmware/brcm`。brcmfmac 实际请求的文件名以 dmesg 为准，板级 `brcmfmac4354-sdio.*.txt` 要从自己的设备提取，保留自己的 MAC 与校准。
4. GK20A 启动需要 FECS / GPCCS 等微码，按 Nouveau 日志里的 `nvidia/gk20a/...` 文件名从合法 L4T 或固件包提取。不能把 gk20b / gm20b 微码当作 gk20a。
5. 蓝牙 HCD 名称按 `btbcm` 实际探测到的芯片 revision 匹配，BT 地址由自己的设备信息私有提供。
6. 记录来源版本、文件长度、SHA256、许可。不要把 MAC、NVRAM、TFA 校准 MTP 或整份 `system.img` 提交到公开仓库。

CUDA 那三个 `.deb` 是仓库引导包，不是直接满足现代内核驱动 ABI 的证据：`cuda-repo-l4t-r21.2-6-5-prod_6.5-34_armhf.deb`、`cuda-repo-ubuntu1404_6.5-14_armhf.deb`、`libopencv4tegra-repo_l4t-r21_2.4.10.1_armhf.deb`。可以用 `dpkg-deb -x` 在私有目录检查包内容；`libcuda` developer stub 不是真驱动。

## 未实现与计划

| 事项 | 当前状态 | 下一步 | 出处 |
|---|---|---|---|
| RT5671 codec | machine 编译通过，codec NACK | 先验证官方电源序列、bus、reset、clock 和实际寄存器响应，再注册完整 ASoC 卡 | `audio/mocha_rt5671_machine.c` 注释 |
| TFA9890 扬声器链路 | 移植可编译，未接通 | 需要合法来源的 DSP 资产与真实校准值 | `audio/mocha_miui_tfa98xx.c`、`audio/mocha_miui_speaker_machine.c` |
| TFA MTP 校准 | `enable_otc()` 在低两位不是 `3` 时拒绝编程 | 不允许为了试音盲写 MTP 或绕过保护 | `audio/mocha_miui_tfa98xx.c` 第 340 行、`audio/miui-tfa98xx-linux612.patch` |
| 麦克风 | 未完成 | 未确认 | `audio/mocha_miui_speaker_machine.c` 路由表 |
| 原生 Tegra 显示 | 独立色块真实显示，不是完整桌面 | 继续开发原生显示路径 | `diagnostics/native-dmabuf-scanout-v2.c` |
| Synaptics 触控 | 失败路径存档 | 仅供对照，不做默认驱动 | `touch/experimental-synaptics/` |
| CUDA 完整 Runtime | libcudart 返回 error 35 | 未确认 | `cuda/README.md`、`firmware/README.md` |
| OpenCV4Tegra | 未完成 | 未确认 | `cuda/README.md` |
| Tegra124 硬件编解码 | 未完成 | 未确认 | 本仓库无对应实现 |
| CPU / GPU 调频与超频 | 未启用 | 保持未启用 | `CONTRIBUTING.md` |

## 常见误解

ALSA / PipeWire 服务启动不等于扬声器存在。服务能起来只说明用户态音频栈在运行，codec 侧还在返回 NACK，链路上没有任何一路真的推动了功放。

Wi-Fi 菜单出现不等于蓝牙硬件配对完成。BCM4354 的 Wi-Fi 和蓝牙是两条独立路径，蓝牙还需要匹配自己设备 revision 的 HCD 与私有地址。

不同面板 / 触控批次不要盲刷 Synaptics。本机是 Atmel 1664T，`experimental-synaptics/` 是另一条失败路径的存档；把它当默认驱动或刷它的固件，会让本来正常的触控失效。

编译通过不能标成实机通过，DRM page flip 成功不能标成面板有图，有限 CUDA Driver API 自检不能标成完整 Runtime。提交时注明仓库 commit、内核 release、DT SHA256、冷启动还是 Fastboot 临时启动、实际屏幕观察和系统日志。

## 许可证与来源

项目新编写的内核与驱动适配采用 GPL-2.0-only，新编写的工具和文档采用 GPL-2.0-only，见 [LICENSE](LICENSE)。Linux、U-Boot 和厂商 GPL 文件保留各自的 SPDX、版权头和原许可证。

从 MIUI 移植的代码保留原有版权与 GPL 声明，例如 `backlight/mocha_miui_backlight.c` 源自 MiCode Mocha 的 `lp855x_bl.c`，`audio/mocha_miui_tfa98xx.c` 与 `touch/experimental-synaptics/` 保留 XiaoMi 与 Synaptics 的版权头。Niri / Smithay、Noctalia、Gdev 等外部项目沿用上游许可，补丁不改变上游许可证。

`cuda/GDEV-LICENSE.txt` 是 Gdev 的上游 MIT 许可，覆盖 `cuda/gdev-mocha.patch` 所针对的上游代码：允许使用、复制、修改、合并、发布、分发、再许可和销售，条件是保留版权声明与许可声明，软件按 "AS IS" 提供、不含任何担保。Gdev 仍是实验项目，不宣称支持任意 CUDA 程序、OpenCV4Tegra 或完整 Runtime。

本仓库不授予 NVIDIA CUDA、MIUI 固件、Wi-Fi / 蓝牙固件或 TFA DSP 参数的再分发权，开发者需自行从合法持有的设备或官方包提取。参考源码的完整出处见总入口仓库的 `SOURCES.md`，许可细节见 [LICENSE-NOTES.md](LICENSE-NOTES.md)。

## 问题与协作

同一个项目家族还有 mocha-moze-debian（总入口、rootfs、安装）、mocha-moze-boot（U-Boot 链式引导）、mocha-moze-linux（内核）和 mocha-moze-desktop（Niri / Noctalia / 充电 UI）。全部问题与证据见 [ISSUES](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/docs/ISSUES.md)，协作方式见 [CONTRIBUTING.md](CONTRIBUTING.md)。
