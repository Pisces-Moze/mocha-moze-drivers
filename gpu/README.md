# GK20A 性能候选：2026-10-11

本机已验证原生 Tegra KMS、Nouveau GK20A 和 Mesa WC 映射组合。受控窗口绘制达到约 60 FPS，完整桌面仍有偶发卡顿；见 [远端交接](https://github.com/Pisces-Moze/mocha-moze-debian/blob/codex/mocha-diagnostics-2026-10-09/docs/HANDOFF-2026-10-11.md)。

## 两条策略不能同时运行

`mocha-native-gpu-policy.sh` 是本轮实际测试的用户空间策略，使用上游原厂 OPP 和驱动电压管理，不添加频率、电压或超频状态。需要 debugfs 已挂载、已验证的原生显示、BQ27520-G4、BQ24192 和进程名为 `niri` 的桌面。GPU pstate 值来自本机表，其他硬件不能直接套用。

| 条件 | 请求 |
|---|---|
| Niri/屏幕未就绪；启动前 3 次采样；屏幕关闭 | auto |
| 充电器在线、输入限额 ≥900 mA、温度/电压正常 | 0f：852 MHz |
| 电脑 500 mA USB、电池供电、GPU 达到 75°C | 0a：648 MHz |
| 电池低于 3.55 V | 08：540 MHz；到 3.75 V 才解除 |
| GPU 热降频 | 降至 648 MHz；到 68°C 才恢复高频 |

每 2 秒采样。退出恢复 auto。实际观测到 GPU 75°C 降到 648 MHz、冷却后恢复 852 MHz。内置候选目录已更新，但该策略完整冷启动/长期运行尚未验收；不能宣称此前黑屏根因已解决。未改 CPU 频率或供电表。

原型机临时测试：

```sh
sudo systemd-run --unit=mocha-gpu-policy-trial /absolute/path/mocha-native-gpu-policy.sh
journalctl -u mocha-gpu-policy-trial -f
sudo systemctl stop mocha-gpu-policy-trial
```

先停止原有频率策略，避免两个进程争写 pstate。测试自定义名称的 Niri profiler 时也停此服务，否则 `pidof niri` 检查会把频率切回 auto。受控测试结束恢复正常 Niri 和策略；不要热卸载 Nouveau。

`experimental/0010-nouveau-gk20a-dvfs-tuning.patch` 保存此前内核 PMU 调参候选，供继续研究。它默认沿用 70% target、90% fast-ramp 和 100 ms 采样，提供有界 module 参数 `gk20a_load_target`、`gk20a_load_max`、`gk20a_poll_ms`。**当前约 60 FPS 结果使用未加此补丁的 Nouveau，不证明该 PMU 候选稳定。** 在 6.12.111 源码上 `patch -p1`，按同一个 O= 目录重新构建模块；不由本仓库任何安装脚本自动应用。先在 RAM 中验证、保存失败日志，再考虑默认启用。实验模块此前重载后出现 SSH 失联，因果未确定，不能作为稳定优化。
