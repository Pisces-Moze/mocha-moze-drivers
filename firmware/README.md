# 私有固件提取与来源

优先参考MiCode官方mocha-kk-oss源码commit79b4898e25fe3b506ff902e182b47068598c838a和自己持有的MIUI V9.2.4.0包/原系统分区。
官方驱动源码可以按GPL移植；“把MIUI驱动二进制复制到Debian”不能解决Linux内核ABI和设备树不匹配。

1. 在电脑上解开自己取得的MIUI system.img（Android sparse先simg2img），只读loop挂载或用debugfs读文件。
2. 在system/etc/firmware、vendor/firmware等目录寻找BCM4354 Wi-Fi firmware/NVRAM、BCM蓝牙HCD、触控配置和TFA DSP资产。
3. 将无线firmware放入/lib/firmware/brcm。brcmfmac实际请求文件名以dmesg为准，板级`brcmfmac4354-sdio.*.txt`应从自己的设备提取，保留自己的MAC与校准。
4. GK20A启动需要FECS/GPCCS等微码，按Nouveau日志的`nvidia/gk20a/...`文件名从合法L4T/固件包提取。不能把gk20b/gm20b微码当作gk20a。
5. Bluetooth HCD名称根据btbcm实际探测的芯片revision匹配；BT地址由自己的设备信息私有提供。
6. 记录来源版本、文件长度、SHA256、许可。不要把MAC、NVRAM、TFA校准MTP或整份system.img提交公开。

CUDA三个.deb是仓库引导包，不是直接满足现代内核驱动ABI的证据：
`cuda-repo-l4t-r21.2-6-5-prod_6.5-34_armhf.deb`、`cuda-repo-ubuntu1404_6.5-14_armhf.deb`、`libopencv4tegra-repo_l4t-r21_2.4.10.1_armhf.deb`。
可用dpkg-deb -x在私有目录检查包/仓库内容；libcuda developer stub不是真驱动。当前完整libcudart仍error35。
