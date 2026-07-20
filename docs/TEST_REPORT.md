# 自测报告

日期：2026-07-20

## 构建环境

- Windows 10 企业版 22H2，10.0.19045，x64
- Visual Studio Build Tools 2019 16.11，MSVC v142 14.29.30133
- Windows SDK 10.0.17763.0
- CMake 3.20.21032501
- Inno Setup 6.7.1
- Release / Win32 / C++17 / 静态运行库

## 自动化测试

命令：

```bat
scripts\build-release.cmd
```

结果：3/3 通过。

1. `common_tests`
   - UTF-16/UTF-8 中文、空格、括号、日文路径往返。
   - START、PROGRESS、DONE、ERROR 协议解析及非法协议拒绝。
   - PDF 扩展名大小写、输出目录命名、已有输出加 `(2)`、临时目录提交及安全清理。
2. `renderer_tests`
   - 中文路径的两页 PDF 实际转换。
   - 纵向页与固有 90° 旋转页方向。
   - PNG 为 24bpp BGR、144 DPI、白色背景。
   - 20 页 PDF 恰好输出 `page_001.png` 到 `page_020.png`。
   - 损坏 PDF 映射为 `PDF_DAMAGED`。
   - 超过 256 MiB 的单页在分配位图前映射为 `PAGE_TOO_LARGE`。
3. `worker_process_tests`
   - 隐藏启动真实 `PdfWorker.exe`。
   - 成功任务依次输出 START、PROGRESS、DONE 并以 0 退出。
   - 损坏任务输出 ERROR/PDF_DAMAGED 并以 21 退出。

补充样本冒烟：

- PDFium 上游 `encrypted.pdf`：Worker 输出 `ERROR/PASSWORD_REQUIRED` 并以 22 退出。
- PDFium 上游 `annotation_stamp_with_ap.pdf`：生成 1 张 PNG，人工查看确认红色 Stamp 批注可见、背景为白色。

## 可执行文件检查

- `PdfToImage.exe` 与 `PdfWorker.exe`：PE x86，操作系统及子系统版本 6.01。
- 两个 EXE 均无 `VCRUNTIME`、`MSVCP` 或 UCRT DLL 依赖。
- `pdfium.dll` 只依赖 `KERNEL32.dll`、`ADVAPI32.dll`、`GDI32.dll`、`USER32.dll`。
- Manifest 已嵌入 x86、asInvoker、Windows 7 compatibility GUID 和 system-DPI-aware 设置。
- 主窗口进入消息循环后可响应，并能处理 `WM_CLOSE` 后以 0 退出。
- 150% 系统缩放下窗口为 900×570 物理像素；标题、拖拽框和底部说明完整显示。
- 完成页使用 `explorer.exe /e,"完整输出目录"` 精确打开本次图片目录；真实 Shell 窗口反查确认中文、空格、括号路径逐字一致。
- 日志成功写入 `%LOCALAPPDATA%\PdfToImage\logs`，不含文件名和路径。

## 安装包检查

- `PdfToImage-Setup-1.0.0.exe`：4,229,407 字节，小于 12 MiB。
- SHA-256：`6A8539293EC48CAB30BF0CF36166EC02AACF202ABD1298912148DAE5B84C5031`。
- 静默安装到隔离目录成功；四个交付文件和卸载器齐全。
- 已安装程序启动、响应、正常退出成功。
- 标准静默卸载返回 0，安装目录无残留。

## 尚需目标机矩阵验证

当前机器没有 Win7 或 Win11 虚拟机，因此尚未执行方案第 10.1 节要求的 Win7 SP1 x86、Win7 SP1 x64 和 Win11 目标机实机冒烟。工程已落实 Win7 的编译、PE、API 和依赖基线，但正式对外发布前仍应在这三台干净虚拟机上执行安装、转换、关闭与卸载验收。

磁盘写满场景未在当前物理磁盘上进行破坏性模拟；只读目录也未改写本机 ACL。相应错误映射和失败临时文件清理路径已实现，仍应在隔离虚拟机中补做这两项故障注入。
