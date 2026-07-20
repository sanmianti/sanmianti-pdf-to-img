# PDF 转图片

离线 Windows 桌面客户端：把一个 PDF 的每一页转换为 144 DPI、白色背景的 PNG。程序使用原生 Win32/Direct2D/DirectWrite，PDF 转换在独立 Worker 进程完成。

## 构建

要求 Visual Studio 2019 16.11（v142）与 Windows SDK。仓库已锁定 Win32、非 V8 的 PDFium 109.0.5406.0。

```bat
scripts\build-release.cmd
```

手工命令：

```bat
cmake -S . -B build -A Win32 -T v142
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

产物位于 `build\Release`。安装包使用 Inno Setup 6 编译 `installer\PdfToImage.iss`。
本次构建的详细自测证据见 [`docs/TEST_REPORT.md`](docs/TEST_REPORT.md)。

## 使用

启动 `PdfToImage.exe`，拖入一个 PDF 或单击拖拽区选择文件。结果写到 PDF 同级的 `原文件名_图片`，已有目录永不覆盖。

最低系统为 Windows 7 SP1。程序是 x86、静态 C/C++ 运行库构建，不依赖 Java、.NET 或 VC++ Redistributable，也不包含联网功能。

