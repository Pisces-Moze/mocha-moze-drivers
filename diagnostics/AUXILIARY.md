# 补充诊断源码

以下是历史取证工具，不是默认安装的运行时依赖。项目自编源码为 GPL-2.0-only。

```sh
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra charger-id-probe.c -o charger-id-probe
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra read-framebuffer.c -o read-framebuffer
arm-linux-gnueabihf-gcc -O2 -Wall -Wextra ../cuda/probe-legacy-cuda.c -o probe-legacy-cuda -ldl
```

`charger-id-probe` 只读取 I2C-1、0x6b、寄存器 0x0a 的芯片身份，不扫描总线、不设置充电电流。`read-framebuffer` 从 `/dev/mem` 读取历史启动布局的 0xf1700000、1536×2048、16 bpp 帧缓冲并写到标准输出；先核对地址/格式，不能用它判断原生 GPU 路径是否完成。`probe-legacy-cuda /path/to/libcuda.so` 动态加载指定库、检查初始化和设备数；专有库自行提供，返回失败不是构建错误。

这些程序的公开构建检查不代表重新进行了平板硬件测试。驱动与参数总索引见 [PARAMETERS.md](https://github.com/Pisces-Moze/mocha-moze-debian/blob/main/docs/PARAMETERS.md)。
