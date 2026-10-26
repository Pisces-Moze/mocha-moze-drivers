# 最小 CUDA 语言模型测试

已在 Mocha 安装 **TinyStories 260K**，模型约 1 MB。它是用于英文故事生成的微型 Llama 架构模型，用来验证端到端 CUDA 推理；不具备通用聊天或中文助手能力。模型来自 [karpathy/tinyllamas](https://huggingface.co/karpathy/tinyllamas/tree/main/stories260K)，主机推理入口基于 [llama2.c](https://github.com/karpathy/llama2.c)，均保留 MIT 许可。

## 在平板运行

```sh
mocha-cuda-llm
mocha-cuda-llm -i "Once upon a time" -n 96
```

`-n` 是最多执行的推理步骤，包含提示词，模型上限为 512。默认温度 0，输出确定性英文故事。首次加载 CUDA 模块有约数秒等待，随后逐段输出。安装目录是 `/srv/mocha-data/codex-debian-desktop/cuda-llm-stories260k`，命令已放到 `/usr/local/bin/`，普通 `mocha` 用户可直接调用。

需要检查数值或对比 CPU 时：

```sh
MOCHA_LLM_CHECK=1 mocha-cuda-llm -n 96
MOCHA_LLM_BACKEND=cpu mocha-cuda-llm -n 96
```

正常 CUDA 模式不执行 CPU 前向计算。CUDA 失败会返回错误，不会静默回退到 CPU。`MOCHA_LLM_CHECK=1` 才额外执行同一输入的 CPU 前向并逐个比较 512 个 logits。

## GPU 实际执行的内容

`llm-forward.cu` 针对这份固定模型，把每个 token 的五层 Transformer 前向合并成 **一个 sm_32 CUDA 内核**：RMSNorm、Q/K/V 投影、RoPE、多查询注意力、残差、SwiGLU、输出投影都在 GPU 执行。权重与 KV cache 常驻 GPU 分配的缓冲；CPU 只负责分词、采样和输出文本，每个步骤取回 512 个 logits。

此实现只接受 `dim=64, hidden=172, layers=5, heads=8, kv_heads=4, vocab=512, context=512` 的原始 checkpoint，入口会检查布局和长度。不能把其他模型文件替换进来当作通用推理引擎。

原先逐个小矩阵提交 CUDA 的候选在测试中超时，未采用。当前融合内核减少提交和同步次数，不依赖 cuBLAS、PyTorch 或官方 libcudart。

## 实机结果（2026-10-25）

| 检查 | 结果 |
|---|---|
| 普通用户 CPU／CUDA 对照 | 96 步生成文本完全相同 |
| 数值检查 | 49,152 个 logits，错误 0，最大绝对误差 `5.10215759e-05` |
| CUDA 执行 | 对照模式 96 次内核；普通命令 32 次内核，CPU 对照次数 0 |
| 正常 CUDA 生成 | 约 12.7 token/s，包含初始化的 32 步总耗时约 7.37 秒 |
| 进程内存 | 普通模式 RSS 5308 KiB；显式 GPU 权重 1,056,540 字节、KV cache 655,360 字节，另有输出与驱动分配 |
| 桌面 | Niri／Noctalia 进程保持运行；测试完成后释放 CUDA 缓冲 |

吞吐量由原推理入口计时，排除了首次前向初始化；RSS 不是全部图形缓冲的总占用。CPU 对照约 683 token/s，这个极小模型在当前后端上尚没有 GPU 速度优势。上述结果证明实际 CUDA 推理正确，不代表大模型性能或完整 CUDA 兼容性。原始测试摘要见 [验证记录](validation-llm-2026-10-25.json)。

## 从源码构建

```sh
python3 fetch-llm-inputs.py /path/to/llm-inputs
sh build-llm.sh /path/to/llm-inputs CUDA_6_5_ROOT OLD_TOOLCHAIN_ROOT RUNTIME_BUILD /path/to/output
```

工具链与开放 Runtime 的构建要求见 [README](README.md)。`fetch-llm-inputs.py` 校验固定版本及 SHA256，模型和 tokenizer 下载到独立目录，不纳入源码仓库。`prepare-llm.py` 校验原版 `run.c` 后只接入前向入口，保留原版 CPU 对照、分词和采样逻辑。编译选择 `--cudart=none -arch=sm_32 --fmad=false`，最后链接本项目开放 Runtime。

上游代码提交、模型修订、资源长度及 SHA256 见 [输入清单](llm-inputs-2026-10-25.json)。原上游许可见 [LLAMA2-LICENSE.txt](LLAMA2-LICENSE.txt)。不分发 NVIDIA 编译器或专有库。
