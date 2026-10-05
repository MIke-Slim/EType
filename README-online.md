# EType 在线轻量版开发分支

本分支为 `codex/online-lite`。已安装的 0.2.0、原安装包及 `codex/sentence-mode` 的 `8758588` 保留；新功能在独立目录和服务端口开发。

- 单词：免费托管的 ECDICT 衍生分片词库，保留完整拼写、bank 多义候选及纠错。
- 句子：MyMemory 免费在线翻译，额度不足保留英文；不自动启用付费接口。
- 朗读：Edge 在线语音，不携带 Qwen、Kokoro、CUDA 或科学计算环境。
- 操作：Ctrl+Shift+空格切换，句子 Enter 翻译/选择中文，Ctrl+Enter 输出英文。

开发构建使用 `scripts/build-online-dev.ps1 -ToolchainPath <llvm-mingw目录>`；预览使用 `scripts/start-online-dev.ps1 -ShowPreview`。服务在 49182，原生组件在 `build/online-lite/native`。依赖见 `requirements-online.txt`；开发构建不会注册系统输入法或生成安装包。

新版还没有替换已安装的系统输入法。只有用户明确要求时才生成完整 EXE 或安装包；安装总占用和下载包大小将在届时实测。

- [实现与服务限制](docs/在线精简版方案.md)
- [测试证据与验证边界](docs/在线轻量版验证.md)
- [保留的离线版说明](README.md)
