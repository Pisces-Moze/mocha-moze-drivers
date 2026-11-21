# Mocha 双摄像头移植

当前状态：已对照旧版板级 DTS、传感器驱动和 6.12 的采集驱动，已有双路分开验收的工具。未启用候选设备树或确认实机出帧。

2026-11-20，实机 Debian `6.12.111-moze.1-native-runtime`：前置 OV5693、后置 IMX179 和 AD5823 未在 I²C 设备列表注册；运行中的设备树没有匹配的摄像头、VI 或 CSI 节点，也没有可采集的媒体节点。内核配置关闭了 `CONFIG_VIDEO_OV5693` 和 `CONFIG_VIDEO_TEGRA`，启用的 `CONFIG_VIDEO_TEGRA_VDE` 是视频解码器。两路均未进行帧采集。这组结果只说明当前镜像未接通摄像头，不判断传感器是否完好。

| 模组 | 旧版相机 I²C | 旧版板级代码操作的 CSI pad | 时钟 |
|---|---|---|---|
| 前置 OV5693 | 2，`0x36` | CSIE | `mclk2`，24 MHz |
| 后置 IMX179 | 2，`0x10` | CSIA / CSIB | `mclk`，24 MHz |
| 后置 AD5823 对焦 | 2，`0x0c` | 不经过 CSI | 不适用 |

参考：`MiPadLinux/android_kernel_xiaomi_mocha` 的 `lineage-19.1-ul`，`tegra124-mocha-camera.dtsi`、`board-ardbeg-sensors.c` 和两支传感器驱动。Pad 的电源控制不等于已确认 CSI lane 连接；旧 I²C 编号不能直接用作 Debian 的总线编号。旧电源序列还包括共用相机电源和 AF GPIO，不能用同地址的通用 DTS 替代。`arttttt/android-camera-hal` 可参考 Bayer 转换与调校，但它依赖 Android Camera2 和图形缓冲接口，不能直接用在 Debian。

从原厂 V9.2.4.0 的启动 DTB 读到：VI 采集控制器是 `nvidia,tegra124-vi`，MMIO `0x54080000`、长度 `0x40000`、旧版内存客户端 `0x12`；MIPI 校准块是 `nvidia,tegra124-mipical`，MMIO `0x700e3000`、长度 `0x100`。旧 `board-ardbeg.h` 将前置电源控制和复位分别连到 PBB6、PCC1，后置复位为 PBB3，PBB7 是对焦电源控制，旧前置上电序列也会操作它。

[上游 VI 硬件版本说明](https://lkml.iu.edu/hypermail/linux/kernel/2603.0/03641.html)确认 Tegra124 是 VI2，Tegra114 是 VI1，Tegra210 是 VI3。[旧版 VI 平台驱动](https://github.com/MiPadLinux/android_kernel_xiaomi_mocha/blob/lineage-19.1-ul/drivers/video/tegra/host/vi/vi.c)可用于对照资源与上电；标准 V4L2 排队、DMA 和出帧仍需要移植。

前置探针的独立设备树在内核仓库的 `moze/dts/camera-front-probe.dts`。已用构建容器的 dtc 编译，读回确认 OV5693 位于 `i2c@7000c500` 的 `0x36`，分配 24 MHz，三个电源引用均已生成，节点保持禁用。原厂启动 DTB 的 `i2c2` 别名和 `avdd_ov5693` 消费者分别确认了总线与 Palmas LDO4。当前主线驱动期望 19.2 MHz，原厂使用 24 MHz；419.2 MHz 是当前主线模式要求，并非已经测量到的板上链路速率。下一步是处理独立的 VIM2 输出门控、原厂电源顺序，并做仅芯片 ID 的 RAM 探针（寄存器 `0x300a` 应读到 `0x5690`）；配置 CSI 路由与首帧采集放在 ID 确认后。

- 先移植 Tegra124 的 VI/CSI 接收端，单独验证 pad 校准、DMA 和同步点；6.12 `tegra-video` 无 Tegra124 的 VI/CSI 匹配项。上游 2026-11-13 的 Tegra114 系列可参考实现结构，但不是 Tegra124 的直接替代。
- 前置先用已有的 `ovti,ov5693` 子设备驱动核对上电和识别，再接入媒体拓扑。后置将旧 IMX179 的首个模式与上电过程移植成 V4L2 subdev。取到稳定 Bayer 帧之后再做 AD5823 对焦、旋转、色彩和自动曝光。
- 用 `sh mocha-camera-probe.sh` 检查 I²C、媒体图和视频节点，根据图选定节点后用 `sh mocha-camera-probe.sh front /dev/videoN` 或 `sh mocha-camera-probe.sh rear /dev/videoN` 验证八帧。脚本需要 `v4l-utils` 与 Python 3，会检查指定摄像头到捕获节点之间的媒体链路都已启用，不保存或上传画面。只有两路各自拍到有效图像，才算实机验收；只有 `/dev/videoN` 不算，VDE 解码器也会创建视频节点。




### 前置 ID 探针实机记录

- 通过原厂 Fastboot 临时启动，已通过 USB SSH 核对 eMMC 为只读。执行 BusyBox insmod 时返回 Segmentation fault，尚未读到芯片 ID。模块构建目录的 uImage 与 RAM 包内的 uImage 的 SHA-256 相同，排除了误拿另一个同名内核镜像的情况。
- 第二轮把探针移到 USB 初始化之前，在确认 eMMC 只读后执行，并在屏幕打印加载结果和内核栈。需用栈区分模块加载器、时钟／供电路径或 I²C 报错。

- 第三轮先建立 USB SSH，再加载探针。日志确认 I²C 地址 0x36 返回 -121，随后内核在 __clk_put → clk_bulk_put → devres_release_group 崩溃。探针把栈上的时钟和稳压器数组交给了延后清理的 bulk API。源码改为逐个获取受托管的句柄，同时按原厂设备树增加 3 ms、3 ms、10 ms 的上电等待。读 ID 仍待复测；不能把 -121 当作硬件损坏的证据。

- 修正版已用匹配的 native-runtime 内核交叉编译（W=1），模块 SHA-256 为 344171138148524cb1fd0977c6e15e7e6836383246be24ac871c988d30eb44e0，已重新封入 RAM 包并核对归档内容。等待下次 Fastboot 才能加载修正版；当前 RAM 会话仍是旧版。

- 修正版实机：insmod 成功返回，随后 rmmod 完成，未再出现 Oops；地址 0x36 仍返回 -121。记录在 artifacts/mocha-front-camera-id/kernel-after-probe.log。
- 单变量时钟对照：原厂 mclk2 使用 171 号硬件门控、父时钟 vi_sensor2，不会对这一门控执行复位；当前共用门控表使用 11 号位、父时钟 clk_m。对照补丁只让 Tegra124 使用原厂映射，Tegra210 登记和前置探针顺序不变。先用 RAM 内核验证，未确认为正式修复。

- 时钟对照实机通过：保留同一设备树和 ID 探针，仅换 Tegra124 VIM2 门控修正版内核（uImage SHA-256：6485ae680414aab162242097654e801f11db006eda4617e2aa1701bb42e0c32a）。USB SSH 核对 eMMC 只读；前置 2-0036 读回芯片 ID 0x5690，模块加载、卸载均成功。此结果验证了前置传感器通信，未验证 CSI/VI 出帧或后置传感器。

- 后置 IMX179 探针和双摄设备树已编译，沿用验证过的 Tegra124 时钟内核。后置地址 0x10，从 0x0002 读两字节，原厂板级描述的预期为 0x8179。两个探针顺序执行，中间卸载模块，避免争用共用的供电和 GPIO；尚未启动双摄包。

- 双摄 ID 实机通过：用已验证的时钟修正版内核、双摄设备树和只读 RAM 诊断包。前置 2-0036 读回 0x5690，后置 2-0010 读回 0x8179；两个模块均成功加载、卸载。未写 eMMC。本轮只确认传感器 I²C 通信，未配置 CSI/VI、未抓帧，也未验证后置自动对焦。详见 artifacts/mocha-front-camera-id/dual-id-passed.log。

- 前摄分段测试：已准备 check_mode=1 的 RAM 探针。用已通过的 24 MHz 上电路径，写入原厂 1280×720 非 HDR 模式表，然后读回 0x3808–0x380b 验证分辨率。传感器保持 standby，即不执行 0x0100=1；这一步只证明可配置模式，不验证 CSI 信号或出帧。独立测试包为 artifacts/mocha-front-camera-id/mode-ram-payload。
- 采集实现参考：NVIDIA 旧版 VI2 后端用 RAW10 CSI 数据类型，T_R16_I 内存格式；应使用写入结束（MW_ACK_DONE）同步点确认 DMA 完成，不能拿帧起始作为完成条件。当前 Linux 已有 Tegra124 的 MIPI 校准驱动和 CSIE pad（索引 4）；原厂 Mocha 对前摄操作 CSIE 电源，但路由仍需实测。原厂 720p 模式表来自 MiPadLinux/android_kernel_xiaomi_mocha 的 ov5693.c；VI2 对照见 https://github.com/antmicro/tx2-deep-learning-kit-bsp/blob/master/kernel/kernel-4.4/drivers/media/platform/tegra/camera/vi/vi2_fops.c 。

- 前摄 720p 待机模式实机通过：读到 0x5690 后写入原厂非 HDR 模式，回读为 1280×720；模块加载和卸载状态均为 0。RAM 下 eMMC 只读。未发送起流命令，也未验证 CSI/VI 帧接收。见 artifacts/mocha-front-camera-id/front-mode-720p-passed.log。

- 前摄 VI2 首帧后端：moze/camera/mocha-vi2-capture.c 已用匹配的 ARM 内核以 W=1 编译为对象文件。固定 1280×720，CSI RAW10 每行 1600 字节，VI T_R16_I 写入步幅 2560 字节（256 字节对齐），单帧缓冲 1843200 字节。完成判据是 CSIE 的 MW_ACK_DONE 同步点，同时检查 VI 错误位。调用端仍须接入 VI 电源域、时钟、CSI 数据链路、MIPI 校准、DMA 分配及超时后的复位；这个对象不能单独加载或用于采集。

- CSIE 接收逻辑已接入单帧后端：先启用校准并准备两条 CSI 数据线，再启动传感器，等校准和整帧 DMA 完成。结束或报错均会停传感器、关闭 CSI 接收、复位 VI 并检查单帧触发位是否清除；若仍未清除，调用方必须保留 DMA 缓冲和同步点，直至整个 VI 复位。ARM W=1 编译和两个对象的链接已经通过。还缺平台驱动：接通电源域、获取时钟和 reset、分配对应设备的 DMA 缓冲和 host1x 同步点，再连到传感器的起流操作。本轮未生成可加载模块，未接触平板。

- VI2 RAM 平台探针已完成 ARM W=1 编译与 modpost，并打包到 artifacts/mocha-front-camera-id/vi2-resource-ram-payload。设备树在 host1x 上描述 VI 的 MMIO、三个时钟、VENC 电源域（PMC 管理 VI 复位）、VI IOMMU 组和 CSIE 校准提供者。探针仅验证运行时电源、时钟以及 32 位 DMA 能力设置，不启用 CSI 或 DMA，不进行采集。使用 run-camera-ram.py --vi2-resources；实机通过标记为 VI2 resource check complete。


- VI2 平台实机验证通过：54080000.vi 加入 IOMMU group 2；VENC 域启动并关停，VI 92.4 MHz、CSI 102 MHz、CILE 12 MHz，32 位 DMA mask 设置成功。模块加载、卸载均成功，未启动 DMA；这些频率是资源探针的读回值，不代表 720p 起流时的目标频率。详见 artifacts/mocha-front-camera-id/vi2-resource-result.log。

- 前摄单帧候选已完成 ARM W=1 编译和打包，命令为 run-camera-ram.py --vi2-frame。探针显式打开 OV5693 内置彩条，申请 VI 设备的 IOMMU DMA 缓冲和 host1x 同步点，收完后逐行检查缓冲区是否被写入并输出 CRC32。若不能确认引擎停稳，缓冲位址不交还给分配器，需重启 RAM 测试环境。尚无实机帧结果。

- 同一只读 RAM 启动实测两次：CSIA 配置位在二级门控关/开都能写入，CSIE 在两种状态都读回零（CSIA=1/1，CSIE=0/0）；测试未开启 DMA。由此应先核对 Tegra124 的 CSIE 物理输入到 parser 的映射，不继续盲调 CIL 频率。日志 vi2-csia-csie-latch.log。
- 流程优化：run-camera-ram.py --vi2-frame --keep-session 启动并保留本机受限 SSH 会话；随后 --vi2-frame --reuse-session 经 USB SSH 把新模块传到 /run 并核对 SHA-256，再加载、卸载。实机同一启动内复测成功执行。内核/DTB 哈希变化或内核崩溃、DMA 未停稳时才重新 RAM 启动；整个过程保持 eMMC 只读。

- 扩展寄存器探针已用 W=1 编译并在同一 RAM 会话热加载，不需要 Fastboot。实测 parser A/B 和 VI 0/1 的配置位在门控开关两种状态都能写入；候选 parser E（偏移 0x1838）和 VI 4（IMAGE_DT 偏移 0x520）在两种状态都不能写入。测试随即退出，没有起流或启动 DMA。A/B 可写并不能证明 CSIE 接到了其中某一路。下一步以 Tegra124 的 CSI 输入选择和 VI 章节核对路由，避免把 Tegra210 端口号直接当作 K1 的通道号。见 artifacts/mocha-front-camera-id/vi2-parser-vi-mapping.log。

- 同一 RAM 会话热加载复测：A/B 的解析器输入控制均读回 0x00ff0004，C/D/E/F 为 0；Tegra114 共享窗口的 CILE 候选偏移 0x90c、0x910、0x938 和 Tegra210 独立 E 窗口的 CIL 对照均读回 0。只读快照不能证明寄存器不存在或路由位置。保留现有起流前检查；没有为未确认的路由编造寄存器写值。下一步以原厂 Tegra124 TRM 的 CSI 输入选择章节或原厂相机运行的寄存器数据确定 CSIE 路由。日志见 artifacts/mocha-front-camera-id/vi2-cil-window-comparison.log。
