# CPU 与内存运行时验证候选

此策略仅适用于 `native-runtime` 候选内核。已验收的 `-native` 内核没有 CPU_FREQ、CPU_IDLE、ZRAM，不应把写 governor 的命令当成调频成功。候选开启上游 Tegra124 cpufreq、PMIC DFLL、LZ4 zram、PSI 和 MGLRU，保持原厂 CVB speed-bin 频率／电压表；不添加超频或降压 OPP。

首次临时启动应带 `cpuidle.off=1`，先验证 DVFS、显示和 RAM 交换，再单独验证 C7 core power-off。Tegra124 的上游驱动禁用未实现的 CC6；启用 CPU_IDLE 不代表深度睡眠已经验收。

`mocha-runtime-policy.py` 选择 schedutil，将最高 CPU 频率限制在可用原厂 OPP 中不高于 1.2 GHz 的档位。CPU/GPU 达到 80°C 时限制到不高于 792 MHz，到 72°C 才解除；熄屏限制到不高于 510 MHz。缺少温度数据时使用保守上限。内核 thermal 保护仍保留。GPU 继续由独立的 `gpu/mocha-native-gpu-policy.sh` 管理，不允许两个服务同时争写同一频率节点。

桌面用户的 `~/.config/mocha/runtime.json` 可保存 `cpu_max_mhz`（600–1200）和 `swappiness`（10–100，默认 80）。只有范围内的整数可影响策略；所有频率都向下选择实际存在的原厂档位，运行时每两秒读取一次。

策略只初始化未配置的 zram0，384 MiB 为逻辑交换容量，不会预先分配同量物理内存。使用 LZ4、交换优先级 100，不创建闪存交换。已有 zram 配置原样保留；服务退出不执行 swapoff/reset，避免回收压缩页时触发内存不足。停止服务会恢复启动前的 CPU governor、上限和 swappiness。

安装脚本和默认启动提升必须在 RAM 启动、动态频率／电压测量、有界内存压力、桌面显示和重复冷启动验证后进行。当前文件为待实机验收的候选，不能据此宣称调频、深度待机或长期内存稳定性已通过。

2026-10-14 的关闭 DFLL 对照两次进入 Debian，CPU 固定 696 MHz；独立调用 `start_zram()` 后，32 MiB 自有匿名页实际压缩到 LZ4 zram 并校验读回一致，PSI/MGLRU 可读。此对照没有 CPUFreq policy，不启用本策略的主循环，也不代表开启 DFLL 的默认候选或长期内存压力验收通过。

2026-10-15：补齐模块挂载顺序后，DFLL 开启版本启动成功，但 powersave 请求 204 MHz 时，DFLL monitor 与独立 PMU 计数均约 1.85–1.9 GHz，软件限频未实际生效。本策略先检查稳定 CPUFreq 请求与硬件 monitor；明显不匹配或无法读取时，在任何 governor／上限／swap 修改前拒绝启动。六项回归通过。该检查不修复硬件控制失效，禁止据此启用候选为默认。
