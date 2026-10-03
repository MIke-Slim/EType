# EType 0.1.0

Windows 英文词汇输入法。输入完整英文单词，选择中文释义。

第一版支持离线词库、一词多义、词形变化、按空格触发纠错、固定候选顺序、
音标/词性、手动离线英语发音、英文直输与中英文标点。默认美式发音。

[下载 Windows 试用包](releases/EType-0.1.0-Windows.zip)（约 3.5 MB）。
完整解压后运行 `EType.exe`，无需安装即可在自带文本框里试用。

![bank 的银行与河岸候选](docs/images/bank.png)

## 使用

构建后的程序在 `build/EType/EType.exe`。运行后点击“直接试用（无需安装）”，
在本窗口输入 `bank` 并按 `2`，应输出“河岸”。输入 `apple` 按空格输出“苹果”。

在其他软件里使用，需要点击“安装系统输入法”完成 Windows 管理员授权，
再从系统输入法列表选择 EType。安装/卸载脚本仅处理 EType，保留原有输入法
和默认选择。完整说明见 [使用说明](docs/使用说明.txt)。

## 源码和构建

- `src/core.*`：离线查询、纠错及输入状态机。
- `src/tsf.cpp`：原生 COM/TSF 输入服务、组合文字、候选和注册。
- `src/platform.*`：候选窗口、偏好和发音启动。
- `src/app.cpp`：设置、组件试用、SAPI 离线发音与诊断。
- `src/editor_store.h`：组件试用窗口的 TSF 文字宿主。
- `scripts/build_dictionary.py`：可重复生成词库，保留源文件哈希与许可。
- `scripts/build.ps1`：构建 x64/x86 DLL、设置程序并运行测试。

构建需要 Python 3 和 LLVM MinGW，不需要安装 .NET SDK。
本项目开发工具保存在 `tools/` 中；运行发布包不需要这些工具。

```powershell
python tools/bootstrap.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
python scripts/package.py
```

本机工具链版本与下载地址记录在 `tools/toolchain-release.json`。
词库基于 [ECDICT](https://github.com/skywind3000/ECDICT)，源文件和派生数据哈希
记录在 `data/manifest.json`。候选编辑覆盖项见 `data/overrides.json`。

## 验证边界

核心状态机测试和真实 Windows TSF 组合/提交测试在构建时运行。
TSF 测试使用进程内临时配置，让 Windows 创建并激活实际 DLL，再调用
其按键接口验证文字提交；它不安装全局配置，也不验证系统向其他应用的按键路由。

设置程序的 `--ui-selftest <report.json>` 在真实试用文本框中验证组件；
该宿主使用同一个输入法 DLL 和 Windows TSF 上屏接口，并自行转交按键。
因此组件试用成功与系统安装成功、微信/Word/浏览器兼容是分别验证的事项。

发音依赖本机离线 SAPI 英语音源。本机美式可用，英式未安装。
源词库音标部分为英式，不会把它们自动标为美式音标。
目前聚焦桌面文本服务；现代应用、受保护输入区域和第三方软件需分别验收。

## 接口参考

- [Microsoft TSF 注册说明](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)
- [文字编辑会话](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)
- [输入配置注册](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-registerprofile)
- [SAPI 发音音源](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms719807(v=vs.85))
