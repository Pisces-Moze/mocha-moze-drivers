# Mocha 内置音频

## 2026-10-22 当前结果

内置左右扬声器均已由用户确认发声，已持久安装到 `6.12.111-moze.1-native-vde2`。普通应用经 PipeWire 输出通过：48 kHz、S16_LE、双声道，默认设备“Mocha 内置扬声器”，软件音量 65%。停止播放后约 3 秒进入空闲，PCM 关闭，两颗功放 PWDN=1、AMPE=0。安装后的服务停止／启动通过；尚未重启复查开机自动加载。耳机、麦克风及其他格式尚未验收。

实际接线为 I2S0 → RT5671 AIF1 → DAC1 → RxDP → DSP DL Bypass → TxDC_DAC → AIF2 → 左 TFA 0x37／右 TFA 0x34。Codec 为时钟提供方，MCLK 使用 12 MHz oscillator。关键修复：

- PX5／GPIO189 保持高，codec 在 0x1c 返回 ID 0x6271；GPIO178 是耳机检测，不能当复位脚。
- DAP1 DOUT PN2 保留 I2S0 功能，关闭三态，否则时钟与 DSP 正常也没有音频数据。
- 主线 codec 的 `DAC1 Playback Switch` 总开关需要打开；仅打开左右 mixer 不会放音。
- 功放启动移到 DAPM 回调之后的工作队列，等待时钟及 MTP shadow 就绪；寄存器更新返回 1 同样视为成功。
- 保护固件下载并读回核对，工厂 MTP 低两位为 3、阻抗 shadow 非零后才放音。错误保持静音／关闭功放，无无限重试，不写 MTP。
- RT5670 无耳机检测时卸载需要保护空 jack 指针，补丁见 `rt5670-no-jack-remove.patch`。

## 构建与实机安装布局

`build-bringup.sh KERNEL_SOURCE KERNEL_OUTPUT NEW_DESTINATION` 使用精确内核的 `.config`／`Module.symvers`，构建 RT5670 修补模块及 board、I2S、data-pin、TFA、runtime-card 共六个模块。2026-10-22 六模块完整 `W=1` 构建通过。`audio/Makefile` 中早期 DT machine 并非当前实机加载路径。

实机安装路径：

- `/usr/local/lib/mocha-audio/6.12.111-moze.1-native-vde2/20261022/`：匹配模块和 `manifest.json`，记录每个模块与合法私有固件的 SHA256。
- `/usr/local/libexec/mocha-speaker-hardware`：来自 `mocha-speaker-hardware.py`，校验机器、内核、模块 ABI 及所有资产哈希，再配置路由；默认物理增益 -18 dB，不在启动时播放测试音。
- `/etc/systemd/system/mocha-speaker-hardware.service`：已启用，挂载数据区与硬件初始化后加载，进入桌面前完成。
- `/etc/wireplumber/wireplumber.conf.d/51-mocha-speakers.conf` 与 `/usr/share/alsa-card-profile/mixer/profile-sets/mocha-speakers.conf`：固定立体声输出，软件音量，空闲挂起；不暴露未验证麦克风。
- `/lib/firmware/mocha/`：用户设备的 MIUI DSP 资产；本仓库不分发固件和工厂校准。

这些源文件不自动安装未经验证的二进制。构建路径变化会改变模块调试信息和哈希；安装清单必须记录实际部署模块，不能直接复用旧哈希。当前生产模块与完整重建产物分开记录，见 `validation-2026-10-22.json`。

## 回退与限制

停止并禁用 `mocha-speaker-hardware.service` 会静音并关闭扬声器端点，不强制卸载桌面仍可能使用的模块。需要恢复配置时，先备份并移走以上 WirePlumber／ACP 文件，再重新启动用户音频服务；原始安装备份在数据区 `installed-setup-20261017/speaker-install-backup-20261022T235022`。默认引导未改变。

不要在线反复卸载 card 后直接重载：codec remove 会复位硬件，但其 I2C regmap cache 仍存在。维护时必须先停止音频、正常卸载 card，再 unbind／bind codec，或者完整重启；禁止强制卸载。普通开机与服务停止／启动不进行此操作。

当前通过可撤销的板级模块注册 disabled I2S 节点、codec 和 runtime card；正式设备树整合、耳机与录音仍待后续处理。临时时钟、供电、GPIO-I2C、地址探针仅供诊断，不加入开机加载。

## 历史诊断（以下状态已被上述结果取代）

2026-10-19：蓝牙 S102 已通过 PipeWire 输出并由用户确认有声音，内置扬声器仍未启用。RT5671 在 I2C0 的 `0x1c` 不应答，ALSA 没有内置声卡；TFA 的 `0x34`、`0x37` revision 均为 `0x0080`。不能把功放可读取称为完整扬声器可用。

固定厂商参考为 MiCode `79b4898e25fe3b506ff902e182b47068598c838a` 的 Mocha DTS、Palmas 供电表、`tegra_rt5671.c` 和 `rt5671.c`。保持现有 ldoen 1.2 V、临时启用厂商的 32 kHz 与 12.288 MHz MCLK 后，读取 codec ID 仍 NACK；厂商流程中的 `reg 0x00 = 0x0000` 软件复位写入也 NACK，复位前后 ID 读取与无寄存器地址的直接读取均返回 errno 121。没有写 TFA MTP、绕过保护或播放未经校准的内置功放。

`mocha_audio_clock_probe.c` 现在先恢复父级 PLL_A，再恢复 OUT0，避免临时 PLL 频率导致 OUT0 恢复时的分频舍入漂移。匹配 VDE2 的修复模块实机加载／卸载后，PLLA 恢复 282.24 MHz、OUT0 恢复 11.2896 MHz，两个时钟关闭，没有探针模块残留。

`mocha_audio_i2c_rate_probe.c` 只用于受限的 Mocha I2C0 时钟诊断。实机首次 NACK 后，Tegra I2C 错误恢复会重新初始化控制器时钟；因此不能把该次结果写成“持续 100 kHz 下 codec 仍失败”。这些探针不加入开机加载。

后续需要确认 codec 供电／复位／时钟的实际板级交接，再继续声卡和功放路径；目前没有内置扬声器听音验收。

## 2026-10-20 LineageOS 参考复核

已固定并检查 [LineageOS-mocha 清单](https://github.com/LineageOS-mocha/android/blob/cm-14.1/default.xml) 指向的 `cm-14.1` 内核／设备分支，而非将用户提供的 ROM 文章当作硬件验收。`board-ardbeg.c` 明确注册 I2C0 的 RT5671 `0x1c` 及两颗 TFA `0x34/0x37`，与现有候选一致；codec-master I2S 配置也一致。该树的 PMIC、固定供电及 board power 文件与此前 MiCode 参考逐字节一致，dbvdd／avdd 使用已开启的 1.8 V，共用 ldoen 为 Palmas GPIO6 的 1.2 V；没有发现可以据此新增的供电切换。此次只读复查仍为 ALSA 无卡，音频探针无残留。

此分支递归树有 codec 源码，却没有 `sound/soc/tegra/tegra_rt5671.c`，设备层音频 XML 还保留 `tegrart5639` 卡名。不能将其作为完整、可直接移植的 Mocha 音频实现。来源、提交、文件 SHA256 和边界见 [参考核对记录](lineage-reference-2026-10-20.json)。下一步仍是取得真实 codec 应答，再完成受保护的扬声器路径；不重复未经新证据支持的时钟／地址盲试。

用户提供的 [CSDN 参考文章](https://blog.csdn.net/weixin_28716181/article/details/167596339) 已通过浏览器读到全文。它介绍一个 LineageOS 16 非官方 ROM 及通用刷机、日志排查，未提供 RT5671 上电时序、VDE 解码实现或 CUDA 接口；文章中的 Android 分区刷写与格式化步骤不适用于当前 Debian 分区布局。本次只提取设备与软件版本的核对线索，没有下载或刷入 ROM。

## 2026-10-21 Smoke 内核参考

进一步固定检查 [SmokeR21.5 内核](https://github.com/Insei/SmokeR21.5-kernel/tree/a6a72bff97f4e35160537087750ca2f4a53f8907)：这棵树包含 RT5671 machine 驱动、codec 的 DT 解析及 Mocha 专用引脚文件。供电表仍使用 Palmas GPIO6 的 1.2 V ldoen，dbvdd／avdd 仍来自共享 1.8 V；没有找到新的 codec 供电线索。codec GPIO 178 用于耳机检测，不能据此当作复位脚翻转。PMIC reset 与 USB extcon 的差异也不能解释音频 NACK。固定提交、十个文件哈希与限制见 [参考记录](smoke-reference-2026-10-21.json)。本次未更改实机音频寄存器、时钟、供电或默认内核，扬声器仍待启用。
