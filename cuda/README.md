# GK20A CUDA Runtime 移植

2026-10-25：开放的 Gdev/Ocelot Runtime 已安装到 Mocha，普通 `mocha` 用户可以运行 NVCC 6.5 编译的 ARMhf／`sm_32` 程序。257、8193 个元素的浮点计算和共享内存归约全部正确。详细结果见 [实机记录](validation-2026-10-25.json)。

这是开放 Runtime 的功能移植。它报告 Runtime **3020**、Driver **4000**；NVIDIA 官方 `libcudart 6.5` 的 error 35 问题仍未解决，不改版本号来绕过检查，也不宣称可以运行任意 CUDA 应用。

## 已验证的功能

| 功能 | 实机结果与范围 |
|---|---|
| NVCC fatbinary 注册与原生内核加载 | 选择兼容的 `sm_30`／`sm_32` 原生 cubin；实际测试为 `sm_32` |
| ARMhf Driver ABI | 内部设备地址保持 64 位，Runtime 的主机指针为 32 位；分配范围超出主机指针宽度时释放并报错 |
| 内存 | 分配、释放、H→D、D→D、D→H、memset、锁页主机内存、带 pitch 的二维拷贝通过 |
| GPU 计算 | 浮点 SAXPY、512 字节静态共享内存归约、动态共享内存及全局原子加法通过 |
| 参数布局 | 8／16／64 位参数、32 位指针和包含 double 的结构体通过；保留 cubin 原始偏移 |
| 流与事件 | 创建、带流参数的传输／启动、同步、查询、等待和事件计时通过；当前为串行路径，传输与计算不重叠 |
| 属性及错误 | 真实寄存器数、静态共享内存、1024 线程上限和 sm_32 元数据；空指针、零长度分配、last-error 行为通过 |
| 旧 Driver 程序 | 原有 257 元素整数内核重新通过 |
| 解析器检查 | 实际源码的 ELF32 元数据与 5 类畸形 fatbinary 输入通过 ASan／UBSan |

事件时间取自前序工作完成后的主机单调时钟，包含主机开销；不是 GPU 时间戳性能基准。内核中的 double 字段用于验证参数布局，不代表完成了所有双精度数学函数测试。

## 实机使用

最小语言模型的 CUDA 前向计算已部署并通过 CPU 对照，运行 `mocha-cuda-llm`；模型、用法和性能范围见 [LLM.md](LLM.md)。

```sh
mocha-cuda-runtime-selftest
mocha-cuda-runtime-run /path/to/your-compiled-program
```

兼容命令 `mocha-cuda-run`、`mocha-cuda-selftest` 也指向新 Runtime。实际目录为 `/srv/mocha-data/codex-debian-desktop/cuda-runtime-20261025`，`cuda-runtime-current` 选择当前版本。库只在这些启动命令内加入 `LD_LIBRARY_PATH`；系统 Mesa／Nouveau 库保持原样。旧命令及旧版本选择保存在 `activation-backup/`。

`activate-runtime.py PREFIX MANIFEST` 在激活前核对全部文件哈希、实机测试结果及被测库哈希，拒绝把失败或不匹配的构建设为当前版本。新前缀必须保留 `runtime-api-result.json` 和编译产物清单。

## 构建开放 Runtime

源自 [shinpei0208/gdev](https://github.com/shinpei0208/gdev)，归档内提交为 `e328438f3cca32bd84ce64807e322673d4b66c40`。本轮使用的 `gdev-master.tar.gz` SHA256 为：

```text
c54f137616525e8b0357328688043f7bc5766e236f5dd6f4a89e38e873561d8b
```

移动的 master 下载地址仅用于取得这份已固定哈希的归档，哈希不符时构建脚本拒绝继续。

本轮环境为 Debian 13 x86_64 容器，GCC 14 ARMhf 交叉工具链、CMake、Python 3.12+、Flex、ARMhf DRM／Nouveau 与 Boost system／filesystem／thread 开发包。中国大陆构建环境使用阿里云 Debian 镜像；不要把旧 Ubuntu 工具链安装成系统默认编译器。

```sh
python3 build-runtime.py /path/to/gdev-master.tar.gz /path/to/new-work-directory --jobs 2
```

工作目录必须不存在。脚本先校验归档，应用 `gdev-mocha.patch`，再运行 `apply-runtime-port.py`，交叉编译并生成私有 `prefix/` 与 `runtime-build-files.json`。依赖库一并放入前缀，补齐旧安装缺失的 `libdrm_nouveau.so.2`，不修改系统图形库。

`apply-runtime-port.py` 负责现代 Boost／GCC 兼容、内部指针 ABI、真实内存填充、属性查询及无效成功返回；`port-elf32.py`、`runtime-fatbin.inc` 处理两种 ELF class 与 fatbinary 边界；`port-runtime-arguments.py` 保留 NVCC 参数布局；`port-runtime-events.py` 修复串行后端的事件完成和等待。重复应用不应重新写入未变的文件。

## 编译 CUDA 程序

编译器使用独立的 [NVIDIA CUDA 6.5 官方包](https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1404/)与 Ubuntu Trusty GCC 4.8 交叉 sysroot。编译器、头文件、原厂库和生成的 cubin 留在私有构建目录；仓库只包含自己的 `.cu` 源码和移植代码。

```sh
sh compile-runtime-smoke.sh CUDA_6_5_ROOT OLD_TOOLCHAIN_ROOT RUNTIME_BUILD /path/to/runtime-smoke
```

采用 `--cudart=none -arch=sm_32`，最后显式链接开放 `libucuda`。GCC 4.8 的 C／C++ 头文件用于适配 NVCC 6.5 的旧前端，链接阶段使用现代 ARMhf 工具链。`--gc-sections` 删除未使用的托管内存注册助手；没有添加假的 `__cudaInitModule`。脚本产物需复制到 Runtime 前缀的 `bin/runtime-smoke` 后再运行自检。

## 仍需完成

- PTX-only fatbinary、压缩 PTX 解码与真正的 PTX 即时编译。目前存在兼容原生 cubin 时使用它，否则明确拒绝。
- 真正异步的多流执行、传输／计算重叠、GPU 时间戳；当前串行功能检查不能作为并发验收。
- 常量／全局符号、纹理／surface、图形互操作、三维 grid 与完整错误边界。
- 托管内存、完整 CUDA 6.5 ABI 和官方 cuBLAS／cuFFT 等库的兼容性。
- 更大应用、线程安全、长时间负载及内存压力下的稳定性。

弱占位 Driver API 已改为明确失败；未覆盖到的旧实现仍需要逐项审计。通过这些自检不等于上述项目已经完成。

上游许可保留在 [GDEV-LICENSE.txt](GDEV-LICENSE.txt)。专有 NVIDIA 软件包及设备固件不随仓库分发。
