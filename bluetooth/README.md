# Mocha 蓝牙与输入设备

2026-10-19 已修复 BCM4354 上电握手，完成固件 RAM 加载、实际扫描、S102 持久配对及蓝牙音频播放；用户确认能听到测试音。ThinkBook Bluetooth Mouse 已持久配对、信任并连接，内核建立 UHID 输入节点。K480 也已完成配对、保存绑定、信任和连接，建立内核键盘输入节点。用户反馈的手动连接和配对码残留已在桌面源码修复并安装；新的配对完成回调通过回归，未重复删除实机绑定做整套配对。常规打字和鼠标移动／滚轮仍没有单独的用户确认。

原故障中 Reset 能回答，而版本／特征命令无法正常回答。Mocha 的上电顺序需要先释放主机 RTS，芯片退出 shutdown 后再恢复流控。使用既有 `drive_rts_on_open` 路径后，Reset、Read Local Version、Read Local Features 均正常；底层 UART 继续使用原 `serial-tegra`。修复限定 `nvidia,mocha`，对应内核源码及 overlay 的 `drivers/bluetooth/hci_bcm.c`。

当前 VDE2 使用独立的 `mocha_bcm_rts` serdev 模块显式加载，无自动匹配别名。`load-bcm-rts-module.py` 校验板型、精确 release、文件所有者／权限、SHA256 和 vermagic；绑定失败恢复原驱动。`mocha-bluetooth-hardware.service` 持久启用，并在桌面硬件准备之后、BlueZ 之前执行。当前会话实际初始化及服务幂等检查通过，尚未单独冷启动复查这项服务。

输入设备需要 `/dev/uhid`，AVRCP 还需要 uinput。当前内核缺少的两模块已从同一 Linux 构建输出编译、安装并通过 depmod 加载；启动加载器也会先加载它们。后续内核配置启用 `CONFIG_UHID=m`、`CONFIG_INPUT_UINPUT=m`。内核自带 HIDP 与 generic HID，支持经典蓝牙及 BLE 输入协议；实际键盘支持必须以配对和输入事件为准。

构建工具在内核仓库的 `moze/tools/build-bcm-rts-dev-module.py` 与 `build-bluetooth-input-modules.py`，只构建，不安装或绑定。公开 BCM 工具在匹配构建目录重新编译通过，派生源码与已安装模块对应源码一致；不同构建路径的二进制哈希不要求相同。安装器仍固定核对已验收产物。

配对时临时开启 Pairable，结束后恢复原状态。桌面使用 KeyboardDisplay 代理；不应使用自动接受任意附近设备的通用代理。S102 的 LinkKey 已确认保存，但密钥、MAC、附近设备扫描和固件二进制不纳入公开仓库。固件来源沿用私有仓库授权文件。

实测边界见 [本轮记录](validation-2026-10-19.json)。下面保留先前失败诊断。

## 2026-10-18 历史 UARTC 诊断

`build-uart-pio-probe.py` 从固定 Linux 6.12.111 的 `serial-tegra.c` 派生隔离诊断模块，保留原 NVIDIA 版权头和 GPL-2.0 许可。输入源码 SHA256 为 `6125dc3a3a68b7586cbe88fad4f5d2ac1c99cb0d971f64e3087ffe0efbbe23a7`，输出目录必须对应 `6.12.111-moze.1-native-vde2`。

```sh
python3 build-uart-pio-probe.py --source /path/to/linux \
  --output /path/to/vde2-build --destination /path/to/new-probe
```

加 `--raw-tty` 可生成独立 TTY 版本，用于隔离 serdev/HCI 初始化流程。两者仅允许 `nvidia,mocha` 的 `0x70006200` UARTC 探测，名称与默认驱动分开；工具只构建，不安装、不绑定，不加入默认模块加载列表。派生源码、模块与构建记录留在指定输出目录。

2026-10-18 实机结果：PIO 和原 UART 路径均能收到 HCI Reset 成功应答，但后续 Read Local Version 超时。保持运行时供电、设备唤醒线变化、独立原始 TTY、复位后立即请求 Download Minidriver，以及复位后在主机端切换 921600/3000000 波特率读取版本，均未恢复后续应答。不能把这些测试称为蓝牙驱动修复，也不能由主机端换速测试推断芯片实际波特率。

后续的单次顺序对照 `Reset → Reset → Read Local Version → Reset` 中，三次 Reset 均应答成功，版本读取没有应答，UART TX/RX 计数分别为 16/21 字节。故障不是简单的“第一个命令后整个传输停止”。按上游复位后延迟要求，单独等待 200 ms 再发 Download Minidriver 也没有应答；不继续盲传固件。

每轮结束恢复 `serial-tegra` 绑定、清除 `driver_override`、释放 GPIO 并卸载临时模块；最后一次实机读取确认无 `mocha_uart_*` 模块残留。没有上传固件、写校准数据或修改永久存储。后续应对照固定厂商源码检查 ROM 初始化状态、命令时序及实际发送行为，收到合法握手后再继续 RAM 固件加载；扫描、配对和音频均未验收。

参见 [受限实机结果](validation-2026-10-18.json)。
