# CUDA研究状态

GK20A是Kepler GPU；Nouveau图形渲染可用不等于NVIDIA CUDA用户态ABI可用。
CUDA6.5/L4T21旧用户态依赖nvhost/nvmap等旧接口；现代LinuxNouveau没有自动提供这些接口。

本项目使用Gdev实验Driver API做有限整数计算，测试257/8193个元素输出正确。修复项包括32/64位查询写入、GART映射、pushbuf资源引用、ARM缓存与代码上传同步。
Driver API报告4000，CUDA6.5 libcudart返回error35。开发包libcuda stub的cuInit返回-1，不能作为可用性检查。

`probe-gdev-cuda.c`查询初始化/设备；`probe-gdev-kernel.c`加载自己组装的cubin并验证全部输出；`gdev-add-seven.ptx`为简单加7内核。
编译示例：`cc -O2 probe-gdev-kernel.c -ldl -o probe-gdev-kernel`，运行时显式指定libucuda和cubin。
专有nvcc/ptxas/cubin/NVIDIA库不随仓库分发。Gdev仍是实验，不宣称支持任意CUDA程序、OpenCV4Tegra或完整Runtime。

`gdev-mocha.patch`是对shinpei0208/gdev master源码档的实际差异，上游归档SHA256为c54f137616525e8b0357328688043f7bc5766e236f5dd6f4a89e38e873561d8b。应用前核对自己的归档，不能把移动master当作固定版本。上游MIT许可见GDEV-LICENSE.txt。
