# 协作方式

通用规则（证据要求、断言口径、不要提交的东西、许可与来源）见总入口的 [CONTRIBUTING.md](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/CONTRIBUTING.md)。这里只写本仓库特有的验收口径。

## 本仓库的验收口径

- 模块必须针对目标 kernelrelease 编译。写清 `uname -r` 与所用的 `O=` 构建目录，不要把不同 release 的 `.ko` 混用，也不要把 stable 的模块装进 native 环境。
- 声明「可用」时给现象，不是给服务状态：背光要说亮度真的变了，显示要说屏幕上真的出图，音频要说真的出声。ALSA/PipeWire 服务起来、Wi-Fi 菜单出现、BlueZ 启动，都不能当硬件可用。
- `diagnostics/` 的程序要附实际输出与运行参数：`NATIVE_GPU_SCANOUT_FRAMES`、`SECONDS`、`FPS`、`CPU_PIXEL_COPIES=0`，以及传进去的 KMS 卡与 render node 路径。`NATIVE_GPU_SCANOUT_STARTED` 只说明 modeset 成功。
- TFA 的 MTP 不要盲写，也不要绕过 `enable_otc()` 的保护。MTP 读数为零不等于未校准，DSP 时序没跑起来时它本来就可能是零。
- 音频实验模块不随默认安装启用；改默认行为要单独说明理由与实机结果。
- 新增移植代码时注明来源（哪个官方包、哪个 commit）与许可，保留原版权头。

## 提交前自查

- `bash tools/build-backlight.sh` 能过，模块落在与内核相同的 release 目录下。
- 没有把专有固件、校准数据、提取出来的 NVRAM 或 MAC 带进提交。
- 写清这次是交叉编译通过，还是已经在平板上跑过。
