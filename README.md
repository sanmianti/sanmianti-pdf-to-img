# PDF 图片转换

离线 Windows 桌面客户端：拖入单个 PDF 可按页生成 144 DPI PNG，拖入图片可合成为 PDF。多张图片支持拖动调整顺序、按名称或时间快捷排序，并可在排序时继续拖入图片。程序使用原生 Win32/Direct2D/DirectWrite，转换在独立 Worker 进程完成。

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

启动 `PdfToImage.exe`，拖入一个 PDF、单张图片或多张图片，程序会自动判断转换方向。可从右上角设置保存位置和界面语言；转换结果写到所选目录，已有文件和目录永不覆盖。

最低系统为 Windows 7 SP1。程序是 x86、静态 C/C++ 运行库构建，不依赖 Java、.NET 或 VC++ Redistributable，也不包含联网功能。
