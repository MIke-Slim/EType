# 免费本地 AI 验证

日期：2026-10-04。开发分支：`codex/sentence-mode`。

## 当前交付

独立本地预览地址：<http://127.0.0.1:49181>。

- 输入英文句子，生成 1–3 条中文候选；准确优先，不保证每次都有多个候选。
- 点击候选查看中文，并可复制；这不是 TSF 向其他应用上屏。
- 单词与整句的美式女声 Heart、男声 Michael，正常与慢速、停止播放、本地缓存。
- 本机运行，无 API 凭据与按次调用费用；下载完成后推理无需互联网。
- 页面标注开发预览，使用用户确定的 Logo 与蓝色候选条方向。

已安装的单词版没有被修改。本次没有构建安装包或 EType 发布 EXE。

## 模型和运行环境

- 翻译：官方 `Qwen/Qwen3-4B-GGUF`，`Q4_K_M`，仓库修订 `bc640142c66e1fdd12af0bd68f40445458f3869b`。
- 模型 SHA-256：`7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5`，已与官方 Hugging Face 元数据一致性校验。
- 推理工具：官方 llama.cpp `b11146` Windows CUDA 12.4 构建，开发目录内使用，没有安装系统服务。
- 语音：Kokoro v1.0 INT8 ONNX，`kokoro-onnx 0.5.0`，`onnxruntime 1.24.4`，CPU 两线程。
- 本机：i7-12700H、约 16GB 内存、RTX 3060 Laptop 6GB 显存。
- 三个主要模型文件合计约 2.44 GiB；Python 环境、推理工具及下载压缩包另占磁盘空间。
- 下载地址、文件大小、固定哈希及哈希来源见 `tools/local-ai-assets.json`。除 Qwen 外，其他固定哈希来自首次下载记录，不冒充发布者公布的校验值。

## 实测与修正

1. 首轮候选改写出现信息遗漏和否定范围变化，例如把“没有说他偷钱”变成“说他没有偷钱”。收紧生成指令，并增加独立请求复核每条候选，不合格候选剔除；全部被拒绝时显示失败，保留英文。
2. 最终 11 条短句均返回可用候选。测试包含 bank 的银行/河岸、否定范围、数字和截止时间、条件句、习惯表达、姓名日期，以及把输入中的指令当成待译句子。最终一轮总耗时 0.349–0.778 秒/句，包含生成与模型复核，使用本机 GPU；页面另一次否定句请求约 0.936 秒。
3. 最终一轮剔除了 6 条候选。模型复核仍可能出错，这不是任意句子的准确率证明；当前测试样本较少，尚未建立完整人工标注评测集。
4. 生成并检查 11 个不同语音样本，均为有效的 24kHz WAV，非静音；包含男女声的单词、整句与慢速。浏览器实际加载并解码了生成音频。尚未由用户确认音色听感。
5. 语音默认线程过多造成长等待。独立 CPU 两线程测试：模型加载约 1.242 秒，测试短句首次合成约 2.362 秒、预热后约 2.487 秒；整段音频约 2.197 秒。页面冷启动一次总生成约 3.977 秒；缓存重播生成步骤约 0 秒。
6. DirectML + FP16 GPU 语音实验在 ConvTranspose 执行时失败，已移除该实验后端，当前采用 CPU 语音。GPU 翻译仍正常运行。
7. 修正页面改变英文、音色或速度后仍保留旧音频的问题，避免通过原生播放器误播上一段文本。请求晚到时也不覆盖已改变的输入。
8. 8 项适配层测试通过，覆盖输入边界、非法模型响应、重复候选、审核过滤/全部失败/非法审核响应、语音参数与仅本机端点；Python 依赖一致性检查通过。
9. 实际点击页面关闭服务后，已核查预览与所属模型进程退出、两个监听端口释放；随后重新启动供用户试听。

## 使用与复现

本机已准备开发环境。重新启动：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/start-local-ai.ps1
```

然后访问 <http://127.0.0.1:49181>。初次启动需要加载模型；页面底部“关闭验证服务”会同时停止它启动的翻译进程，释放资源。

在另一套开发环境准备资源（当前启动方案针对 Windows NVIDIA CUDA 显卡，其他电脑需单独验证）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/setup-local-ai.ps1 -Python "C:\path\to\python.exe"
```

当前实测 Python 3.12.14。开发资源全部位于被 Git 忽略的 `build/local-ai/`，不提交模型和运行环境。

验证脚本：

```powershell
build/local-ai/venv/Scripts/python.exe tests/local_ai_tests.py
build/local-ai/venv/Scripts/python.exe scripts/validate_local_ai.py --only translation
build/local-ai/venv/Scripts/python.exe scripts/validate_local_ai.py --only speech
```

真实报告保存在 `build/local-ai/`：`translation-results.json`、`speech-results.json`、
`audio-validation.json`、`speech-benchmark.json`、`requirements-lock.txt`。

## 用户试听与原生接入

- 2026-10-04 用户认可试听音色并要求继续，现已接入工作区开发版 TSF 和原生设置窗口。
- 已实现后台翻译、过期结果丢弃、单词横排候选与句子换行候选；英文原文经 Kokoro 生成后由 Windows PCM 输出播放。
- 真实原生试用框已验证 `I sat on the bank.` → `我坐在河岸上。`、数字和标点保留、等待期间编辑及切换焦点保护。原先的 Enter 输出英文交互已于 2026-10-04 按用户要求改为 Enter 翻译/确认中文、Ctrl+Enter 输出英文，测试同步更新。
- 仍需扩大带人工标注的句子评测，重点验证否定、时态、歧义与长句。二次模型复核不能保证所有译文正确。
- 目前验证的是开发版原生组件；没有覆盖已安装版，也未验证系统向 Word、微信等外部软件的按键路由。使用方式见 `原生句子开发版.md`。

## 官方与项目来源

- [Kokoro 模型](https://huggingface.co/hexgrad/Kokoro-82M)
- [Kokoro ONNX 适配与模型发布](https://github.com/thewh1teagle/kokoro-onnx)
- [Qwen 官方 GGUF 模型](https://huggingface.co/Qwen/Qwen3-4B-GGUF)
- [llama.cpp](https://github.com/ggml-org/llama.cpp)
- [ONNX Runtime DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html)
