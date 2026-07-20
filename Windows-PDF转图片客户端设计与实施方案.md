# Windows PDF 转图片客户端设计与实施方案

> 文档版本：1.0  
> 编制日期：2026-07-20  
> 目标：形成一套可直接交给 Codex 实施的 Windows 客户端方案。

## 1. 方案结论

采用以下固定技术路线：

| 项目 | 决策 |
| --- | --- |
| 开发语言 | C++17 |
| 客户端框架 | 原生 Win32 + Direct2D + DirectWrite |
| PDF 渲染 | PDFium，固定为 Chromium 109 系列、Win32 x86、非 V8 构建 |
| 图片编码 | Windows Imaging Component（WIC） |
| 输出格式 | PNG，144 DPI，白色背景 |
| 应用架构 | 主界面进程 + 独立转换 Worker 进程 |
| 安装打包 | Inno Setup，生成一个 x86 Setup.exe |
| 最低系统 | Windows 7 SP1 |
| 覆盖系统 | Win7 SP1、Win10、Win11；x86 和 x64 系统使用同一安装包 |
| 外部依赖 | 不依赖 Java、.NET、VC++ Redistributable，不联网 |

最终交付一个安装包：

```text
PdfToImage-Setup-1.0.0.exe
```

安装后的程序目录包含：

```text
PdfToImage.exe       主界面
PdfWorker.exe        PDF 转换进程
pdfium.dll           PDF 渲染引擎
THIRD_PARTY_NOTICES  第三方许可证说明
```

选择 32 位应用是为了让同一套二进制同时运行在 32 位和 64 位 Windows 上。转换时逐页渲染、逐页释放内存，避免整份 PDF 一次性进入内存。

## 2. 产品边界

### 2.1 只做以下功能

1. 拖入一个 PDF 文件，或单击拖拽区域选择 PDF。
2. 自动读取 PDF 页数。
3. 按页生成 PNG 图片。
4. 显示当前转换页数和总体进度。
5. 完成后打开图片所在文件夹。

### 2.2 明确不做

- 不做图片格式选择。
- 不做 DPI、尺寸、质量等参数设置。
- 不做页码范围选择。
- 不做 PDF 预览。
- 不做批量任务队列。
- 不做 OCR、合并、拆分、编辑、水印。
- 不做登录、联网、自动更新、埋点或云端上传。
- 不保存任务历史。
- 不支持需要密码的 PDF。

“打开输出文件夹”属于完成转换所必需的闭环操作，不视为额外功能。

## 3. 关键设计规则

### 3.1 输入规则

- 一次只处理一个文件。
- 仅接受扩展名为 `.pdf` 的普通文件，扩展名不区分大小写。
- 拖入多个文件时不启动任务，提示：`一次只能转换一个 PDF 文件`。
- 转换过程中再次拖入文件时提示：`正在转换，请稍候`。
- 文件路径统一使用 UTF-16；调用 PDFium 前转换为 UTF-8。
- 输入 PDF 不允许被程序修改、移动或删除。

### 3.2 输出规则

输入文件：

```text
D:\资料\项目方案.pdf
```

第一次转换输出到：

```text
D:\资料\项目方案_图片\
```

输出文件名：

```text
page_001.png
page_002.png
page_003.png
```

页码宽度取 `max(3, PDF 总页数的数字位数)`。如果目标文件夹已经存在，则依次创建：

```text
项目方案_图片 (2)
项目方案_图片 (3)
```

不覆盖任何已有文件。

### 3.3 转换参数

| 参数 | 固定值 |
| --- | --- |
| 格式 | PNG |
| 分辨率 | 144 DPI |
| 缩放比例 | PDF 点数 × 2；因为 PDF 为 72 点/英寸 |
| 颜色 | 24 位 BGR |
| 背景 | 白色 |
| 页面旋转 | 按 PDF 页面旋转信息输出 |
| 批注 | 输出可见批注 |
| 抗锯齿 | 开启 |

144 DPI 对文字和表格足够清晰，同时不会产生过大的图片；PNG 更适合文字型页面，不会产生 JPEG 压缩毛边。

### 3.4 临时文件与原子完成

转换开始后，先在 PDF 同级目录创建本次任务专属临时文件夹：

```text
项目方案_图片.tmp-<pid>-<随机数>
```

所有页面转换成功后，再把整个临时文件夹重命名为最终输出文件夹。失败、取消或 Worker 崩溃时，只删除本次任务创建的临时文件夹，不触碰其他目录。

这样可以避免用户看到“只有一半页面”的伪成功结果。

## 4. 界面与交互

### 4.1 窗口规格

- 默认尺寸：600 × 380 逻辑像素。
- 最小尺寸：520 × 340 逻辑像素。
- 使用系统标准标题栏，标题为 `PDF 转图片`。
- 主体使用浅色、扁平设计，不使用复杂动画和自定义无边框窗口。
- 字体优先使用 `Microsoft YaHei`，英文和数字使用 `Segoe UI`。
- 使用 Direct2D 绘制界面，使用 DirectWrite 绘制文字。
- Win7 使用系统级 DPI 感知；不使用 Win10 才提供的 Per-Monitor V2 API。

### 4.2 空闲状态

```text
┌──────────────────────────────────────────────┐
│  PDF 转图片                              — □ ×│
│                                              │
│             PDF 转图片                       │
│       拖入 PDF，自动按页生成图片              │
│                                              │
│    ┌────────────────────────────────────┐    │
│    │                                    │    │
│    │          将 PDF 拖到这里            │    │
│    │          或单击选择文件             │    │
│    │                                    │    │
│    └────────────────────────────────────┘    │
│                                              │
│       图片将保存到 PDF 文件所在目录           │
└──────────────────────────────────────────────┘
```

拖拽区域是整个页面唯一的主操作区域。单击它调用 Win32 文件选择对话框，文件过滤器只显示 PDF。

### 4.3 转换状态

```text
正在转换：项目方案.pdf

██████████████████░░░░░░░░░░░░  12 / 20 页

正在生成 page_012.png
```

- 主线程只负责 UI 和消息循环。
- 转换由 Worker 进程执行，界面不得卡死。
- 窗口关闭时终止 Worker，并清理本次临时目录。
- 转换中不提供设置入口。

### 4.4 完成状态

```text
✓ 转换完成
已生成 20 张 PNG 图片

[ 打开图片文件夹 ]

可继续拖入新的 PDF
```

按钮只保留一个：`打开图片文件夹`。用户再次拖入 PDF 后直接开始下一次任务。

### 4.5 失败状态

失败页面不显示技术堆栈，只显示可行动的中文提示，并提供 `重新选择 PDF`。

| 错误 | 提示文案 |
| --- | --- |
| 不是 PDF | 请选择 PDF 文件 |
| 文件不存在 | 文件不存在或已被移动 |
| 无法读取 | 无法读取该 PDF，请检查文件权限 |
| PDF 损坏 | PDF 文件已损坏或格式不受支持 |
| 需要密码 | PDF 已加密，暂不支持转换 |
| 无写入权限 | 无法在 PDF 所在目录创建图片文件夹 |
| 磁盘空间不足 | 磁盘空间不足，无法继续转换 |
| 页面过大 | PDF 页面尺寸过大，无法生成图片 |
| Worker 异常退出 | 转换失败，请更换 PDF 后重试 |

## 5. 程序架构

### 5.1 进程职责

| 进程 | 职责 |
| --- | --- |
| `PdfToImage.exe` | 窗口、拖拽、文件校验、创建输出路径、启动 Worker、显示进度、打开文件夹、清理失败任务 |
| `PdfWorker.exe` | 初始化 PDFium、加载 PDF、逐页渲染、调用 WIC 写 PNG、报告进度、返回错误码 |

PDF 解析放在独立进程中有两个目的：

1. 损坏 PDF 导致 PDFium 异常时，主界面仍能存活并提示失败。
2. 主进程可以通过 Windows Job Object 约束 Worker，并在客户端退出时确保 Worker 一并结束。

### 5.2 进程通信

主进程使用 `CreateProcessW` 隐藏启动 Worker，并重定向 Worker 的标准输出。Worker 按 UTF-8 输出制表符分隔的行协议：

```text
START\t20
PROGRESS\t1\t20\tpage_001.png
PROGRESS\t2\t20\tpage_002.png
DONE\t20
ERROR\tPASSWORD_REQUIRED
```

Windows 文件名不能包含制表符和换行符，因此该协议无需引入 JSON 库。Worker 的退出码仍作为第二重判断：

| 退出码 | 含义 |
| --- | --- |
| 0 | 成功 |
| 10 | 参数错误 |
| 20 | PDF 无法打开 |
| 21 | PDF 格式损坏 |
| 22 | PDF 需要密码 |
| 23 | PDF 安全方案不支持 |
| 30 | 页面渲染失败 |
| 31 | PNG 写入失败 |
| 32 | 页面尺寸或内存超过限制 |
| 40 | 用户取消 |
| 50 | 未分类错误 |

### 5.3 Worker 约束

主进程创建 Windows Job Object，并设置：

- `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`：主进程结束时自动终止 Worker。
- Worker 进程内存上限建议 768 MiB。
- Worker 不创建窗口，不访问网络，不写注册表。
- 每次只打开一个 PDF，一次只在内存中保留一页位图。
- 单页位图缓冲区上限 256 MiB，超过即返回 `PAGE_TOO_LARGE`。
- 编译时开启 `/LARGEADDRESSAWARE`，提升 x86 程序在 64 位 Windows 上的可用地址空间。

### 5.4 模块划分

```text
PdfToImage/
├─ CMakeLists.txt
├─ README.md
├─ docs/
│  └─ SPEC.md
├─ src/
│  ├─ app/
│  │  ├─ main.cpp
│  │  ├─ app_window.h
│  │  ├─ app_window.cpp
│  │  ├─ drop_handler.h
│  │  ├─ drop_handler.cpp
│  │  ├─ worker_client.h
│  │  └─ worker_client.cpp
│  ├─ worker/
│  │  ├─ worker_main.cpp
│  │  ├─ pdf_renderer.h
│  │  ├─ pdf_renderer.cpp
│  │  ├─ png_encoder.h
│  │  └─ png_encoder.cpp
│  └─ common/
│     ├─ output_path.h
│     ├─ output_path.cpp
│     ├─ utf.h
│     ├─ utf.cpp
│     ├─ error_code.h
│     └─ scope_guard.h
├─ resources/
│  ├─ app.rc
│  ├─ app.manifest
│  └─ app.ico
├─ third_party/
│  └─ pdfium/
│     ├─ include/
│     ├─ lib/x86/pdfium.lib
│     ├─ bin/x86/pdfium.dll
│     └─ licenses/
├─ installer/
│  └─ PdfToImage.iss
├─ tests/
│  ├─ unit/
│  └─ samples/
└─ scripts/
   ├─ build-release.cmd
   └─ package.cmd
```

## 6. PDF 渲染实现

### 6.1 初始化与释放

Worker 启动后调用一次：

```cpp
FPDF_InitLibrary();
```

任务结束前调用：

```cpp
FPDF_DestroyLibrary();
```

所有 PDFium 句柄必须使用 RAII 包装，确保以下函数成对调用：

- `FPDF_LoadDocument` / `FPDF_CloseDocument`
- `FPDF_LoadPage` / `FPDF_ClosePage`
- `FPDFBitmap_CreateEx` / `FPDFBitmap_Destroy`

### 6.2 加载文件

Win32 拖拽得到 UTF-16 路径，使用 `WideCharToMultiByte(CP_UTF8, ...)` 转换为 UTF-8，再调用：

```cpp
FPDF_DOCUMENT document = FPDF_LoadDocument(utf8Path.c_str(), nullptr);
```

失败后立即读取 `FPDF_GetLastError()` 并映射为业务错误码，不在成功调用之后读取该值。

### 6.3 单页处理伪代码

```cpp
for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
    checkCancellation();

    auto page = FPDF_LoadPage(document, pageIndex);
    float widthPt = FPDF_GetPageWidthF(page);
    float heightPt = FPDF_GetPageHeightF(page);

    int widthPx = ceil(widthPt * 2.0f);   // 144 / 72
    int heightPx = ceil(heightPt * 2.0f);
    int stride = align4(widthPx * 3);

    checkDimensionsAndMemory(widthPx, heightPx, stride);

    std::vector<uint8_t> pixels(stride * heightPx, 0xFF);
    auto bitmap = FPDFBitmap_CreateEx(
        widthPx,
        heightPx,
        FPDFBitmap_BGR,
        pixels.data(),
        stride
    );

    FPDFBitmap_FillRect(bitmap, 0, 0, widthPx, heightPx, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(
        bitmap,
        page,
        0,
        0,
        widthPx,
        heightPx,
        0,
        FPDF_ANNOT | FPDF_RENDER_LIMITEDIMAGECACHE
    );

    writePngWithWic(outputPath, pixels, widthPx, heightPx, stride);
    reportProgress(pageIndex + 1, pageCount, outputFileName);
}
```

实现时必须使用选定 PDFium 版本自带的头文件，不要把网上最新头文件与旧版 DLL 混用。旋转 PDF 应加入像素级测试；如果固定版本的 `FPDF_GetPageWidthF/HeightF` 和默认渲染未自动体现页面旋转，则读取页面旋转值、交换宽高，并将旋转值传给 `FPDF_RenderPageBitmap`。

### 6.4 WIC 写入 PNG

Worker 线程调用：

```cpp
CoInitializeEx(nullptr, COINIT_MULTITHREADED);
```

使用以下 WIC 组件：

1. `IWICImagingFactory`
2. `IWICStream`
3. `IWICBitmapEncoder`，容器格式 `GUID_ContainerFormatPng`
4. `IWICBitmapFrameEncode`
5. 像素格式 `GUID_WICPixelFormat24bppBGR`

调用 `IWICBitmapFrameEncode::SetResolution(144.0, 144.0)` 写入正确的 DPI 元数据。PDFium 默认的文字、图像和路径抗锯齿保持开启；不要使用 `FPDF_LCD_TEXT`，避免把针对屏幕子像素排列的彩边固化到输出图片中。

图片先写到同一临时目录中的 `page_NNN.png.tmp`，编码完成并提交后再改名为 `page_NNN.png`。写入失败不得留下半成品文件。

## 7. Win7 与单安装包实现

### 7.1 编译基线

建议固定构建环境：

- Visual Studio 2019 16.11
- MSVC v142
- Windows 10 SDK 10.0.19041
- CMake 3.24 或同级稳定版本
- 目标平台：`Win32`
- `_WIN32_WINNT=0x0601`
- 静态 C/C++ 运行库：`/MT`
- 子系统版本：`/SUBSYSTEM:WINDOWS,6.01`

Release 构建命令：

```bat
cmake -S . -B build -A Win32 -T v142
cmake --build build --config Release
```

CMake 中固定静态运行库：

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601)
```

Release 产物必须在一台未安装 Visual Studio、未安装 VC++ 运行库、未安装 Java 和 .NET 4.x 的干净 Win7 SP1 虚拟机中验证。

### 7.2 PDFium 版本固定

Win7 最后对应 Chromium 109 系列，因此使用 `bblanchon/pdfium-binaries` 中 Chromium 109 分支最后一个可在 Win7 SP1 运行的 Win-x86 非 V8 构建。

依赖接入要求：

1. 不使用 `latest` 下载地址。
2. 在 `third_party/pdfium/VERSION.lock` 记录 release tag、Chromium branch 和 SHA-256。
3. 固定使用该发行包自带的 `include`、`pdfium.lib`、`pdfium.dll` 和许可证文件。
4. 使用 `dumpbin /DEPENDENTS pdfium.dll` 检查依赖。
5. 在 Win7 SP1 x86 与 x64 上分别执行启动和转换冒烟测试。

由于 Win7 已停止支持，不能把 Win7 兼容的 PDFium 109 当作可持续升级的现代版本。v1 通过独立 Worker、进程内存限制、无网络和最小功能面降低影响；如果未来面向大量外部用户分发，应在同一安装包中为 Win10/11 增加当前 PDFium，并仅在 Win7 上加载旧版引擎。

### 7.3 安装器

使用 Inno Setup 生成单文件安装包，采用按用户安装，避免管理员权限：

```ini
[Setup]
AppName=PDF 转图片
AppVersion=1.0.0
DefaultDirName={localappdata}\Programs\PdfToImage
PrivilegesRequired=lowest
MinVersion=6.1sp1
ArchitecturesAllowed=x86compatible
Compression=lzma2/ultra64
SolidCompression=yes
OutputBaseFilename=PdfToImage-Setup-1.0.0
```

安装器行为：

- 安装主程序、Worker、PDFium 和许可证文件。
- 创建开始菜单快捷方式。
- 默认创建桌面快捷方式，可由用户取消。
- 提供标准卸载入口。
- 不注册 PDF 文件关联。
- 不添加右键菜单。
- 不设置开机启动。
- 不下载任何组件。

安装包目标体积为 6～10 MiB，建议验收上限为 12 MiB。最终体积取决于固定 PDFium 构建；不得为了压缩体积使用 UPX，以免增加杀毒软件误报。

## 8. 应用状态机

| 当前状态 | 事件 | 下一状态 | 动作 |
| --- | --- | --- | --- |
| IDLE | 拖入合法 PDF | PREPARING | 计算输出目录、创建临时目录 |
| IDLE | 拖入非法文件 | ERROR | 显示输入错误 |
| PREPARING | Worker 启动成功 | CONVERTING | 显示进度 |
| PREPARING | 准备失败 | ERROR | 清理临时目录 |
| CONVERTING | 收到 PROGRESS | CONVERTING | 更新页码与进度条 |
| CONVERTING | 收到 DONE 且退出码为 0 | SUCCESS | 临时目录改名为最终目录 |
| CONVERTING | Worker 返回错误 | ERROR | 清理临时目录、显示错误 |
| CONVERTING | Worker 异常退出 | ERROR | 清理临时目录、显示通用错误 |
| CONVERTING | 用户关闭窗口 | CANCELLING | 结束 Job Object、清理临时目录 |
| SUCCESS | 拖入新 PDF | PREPARING | 开始新任务 |
| ERROR | 选择新 PDF | PREPARING | 开始新任务 |

只有主进程可以完成临时目录到最终目录的重命名，因此 `Worker 返回成功` 不等于 `任务最终成功`。

## 9. 日志与隐私

- 软件完全离线运行。
- 不请求网络权限，不包含联网代码或更新检查。
- 不上传 PDF、页面图片、文件名或文件路径。
- 诊断日志位于 `%LOCALAPPDATA%\PdfToImage\logs`。
- 日志最多保留 3 个文件，每个不超过 256 KiB。
- 日志只记录版本号、系统版本、阶段、页码和错误码，不记录用户文件名、完整路径或文档内容。

## 10. 测试方案

### 10.1 系统矩阵

同一个安装包逐项验证：

| 系统 | 架构 | 必测 |
| --- | --- | --- |
| Windows 7 SP1 | x86 | 是 |
| Windows 7 SP1 | x64 | 是 |
| Windows 10 22H2 | x64 | 是 |
| Windows 11 25H2 或当前稳定版 | x64 | 是 |

Win11 ARM64 可作为补充验证项，通过系统 x86 模拟运行，但不列入 v1 强制承诺。

### 10.2 PDF 样本

至少准备以下测试文件：

1. 单页 A4 中文文本。
2. 20 页中英文混排文档。
3. 纵向、横向和旋转页混合文档。
4. 含图片、透明元素、表格、矢量图和可见批注的文档。
5. 文件名及路径含中文、空格、括号和日文的文档。
6. 0 页或结构异常 PDF。
7. 截断或损坏 PDF。
8. 密码保护 PDF。
9. 超大纸张 PDF。
10. PDF 所在目录只读、磁盘空间不足场景。

### 10.3 核心验收标准

- 同一个 Setup.exe 可在四个必测系统上安装、启动和卸载。
- 干净 Win7 SP1 不安装 Java、.NET 4.x 或 VC++ 运行库也可运行。
- 拖入 N 页 PDF 后恰好生成 N 张 PNG。
- 页码顺序、页面方向、页面内容和可见批注正确。
- 图片背景为白色，无透明黑底，无明显文字锯齿。
- 输出目录已存在时不覆盖旧结果。
- 转换中主窗口可移动、重绘和关闭，不出现假死。
- Worker 异常退出时主界面仍然存在，临时目录被清理。
- 密码 PDF、损坏 PDF、只读目录显示对应中文错误。
- Process Monitor 验证程序不发起网络连接。
- 安装包不超过 12 MiB；若超过，先检查是否误打包 x64/V8/XFA/调试符号，不通过 UPX 解决。

## 11. Codex 实施顺序

不要要求 Codex 一次性完成全部代码。按下面顺序逐阶段实施，每阶段必须构建和验收后再进入下一阶段。

### 阶段一：工程骨架与 Win7 空程序

目标：创建 CMake Win32 工程、两个可执行文件、资源、Manifest 和 Inno Setup 脚本。主程序只显示空闲界面，Worker 只返回固定测试事件。

验收：

- Release Win32 构建通过。
- 主界面可拖拽 PDF 并显示文件名。
- Worker 进度协议可被主进程读取。
- Setup.exe 可安装和卸载。
- 空程序先在 Win7 SP1 启动成功。

### 阶段二：PDFium 最小转换闭环

目标：接入固定 PDFium，Worker 完成单页和多页 PDF 到 144 DPI PNG 的转换。

验收：

- 中文路径可读取。
- 1 页和 20 页 PDF 输出数量正确。
- 纵横页面方向正确。
- Worker 逐页释放位图。
- 密码、损坏和无法读取错误正确映射。

### 阶段三：完整交互与任务清理

目标：完成 IDLE、CONVERTING、SUCCESS、ERROR 状态，输出目录命名、临时目录、Job Object、关闭取消和打开文件夹。

验收：

- 转换过程 UI 不阻塞。
- 旧输出不被覆盖。
- 失败和关闭后没有任务临时目录残留。
- Worker 崩溃不带崩主程序。

### 阶段四：兼容、打包与回归

目标：完成 DPI 适配、图标、Release 优化、许可证、Inno Setup 和四系统测试。

验收：

- 完成第 10 章全部验收项。
- 记录 PDFium SHA-256 和最终安装包 SHA-256。
- 生成 `release/` 目录，只放安装包、校验值和简短发布说明。

## 12. 可直接交给 Codex 的总任务指令

```text
请实现一个名为“PDF 转图片”的 Windows 桌面客户端。

开始编码前先完整阅读 docs/SPEC.md，并将它视为不可擅自扩展的产品与工程基线。应用只做一件事：用户拖入一个 PDF，程序自动把每一页转换为 144 DPI、白色背景的 PNG 图片，并保存到 PDF 同级的“原文件名_图片”目录。

固定约束：
1. C++17、原生 Win32、Direct2D、DirectWrite、WIC。
2. 主程序和转换 Worker 分进程；主程序不得直接解析 PDF。
3. PDFium 使用已经放入 third_party/pdfium 的固定 Win32 x86 非 V8 构建，不得自动升级，不得混用其他版本头文件。
4. 构建目标为 Win32，_WIN32_WINNT=0x0601，/MT，兼容 Windows 7 SP1、Windows 10、Windows 11。
5. x86 和 x64 Windows 使用同一个 Inno Setup 安装包。
6. 不依赖 Java、.NET、VC++ Redistributable，不联网。
7. 不添加设置、预览、批量队列、格式选择、页码选择、OCR、更新检查等未要求功能。
8. 转换逐页完成，每次只保留一页位图；单页缓冲区最大 256 MiB。
9. 所有输出先进入本任务专属临时目录，全部成功后由主进程把目录原子重命名为最终目录；失败或取消只能清理本任务创建的临时目录。
10. 每完成一个阶段，先构建、运行测试并说明验证结果，再继续下一阶段。

请先只完成“阶段一：工程骨架与 Win7 空程序”。不要提前接入 PDFium。完成后列出新增文件、构建命令、已验证项目和仍待验证项目。
```

## 13. 技术取舍说明

### 13.1 为什么不用 Java 8 + PDFBox

Java 方案实现简单，但要保证干净 Win7 双击可用，就必须携带 Java 运行时。即使裁剪，整体体积通常也明显高于本方案，并且“是否安装了正确 Java”会成为额外变量。它适合内部已有统一 Java 环境的场景，不适合本项目的单包、零依赖目标。

### 13.2 为什么不用 C#/.NET

Win7 默认没有可直接依赖的现代 .NET 运行时。随安装包携带 .NET 会显著增加体积；依赖系统现有 .NET 又无法保证干净 Win7 一次安装即用。

### 13.3 为什么不用 Electron、Qt、Ghostscript

- Electron 对单功能工具过重。
- Qt 会引入一批框架 DLL，并增加打包体积和插件部署问题。
- Ghostscript 功能远超需求，体积与许可证处理都更复杂。

原生 Win32 + PDFium + WIC 的实现代码更多，但运行时最小、依赖最少，最符合这个工具的长期定位。

## 14. 已核实的外部依据

- Google 官方说明 Chrome 109 是最后支持 Windows 7 的 Chrome 版本：<https://support.google.com/chrome/a/answer/7100626>
- PDFium 官方 `FPDF_RenderPageBitmap` API：<https://pdfium.googlesource.com/pdfium/+/main/public/fpdfview.h>
- PDFium 源码采用 BSD-style license，分发时仍需保留第三方许可证：<https://pdfium.googlesource.com/pdfium/>
- Microsoft 说明 Win7 内置 WIC 编解码器包含 PNG，且支持多线程单元：<https://learn.microsoft.com/windows/win32/wic/-wic-howwicworks>
- Inno Setup 官方说明支持 Windows 7、Windows 10、Windows 11：<https://jrsoftware.org/isinfo.php>
- Inno Setup `MinVersion` 默认最低为 Windows 7 SP1：<https://jrsoftware.org/ishelp/topic_setup_minversion.htm>
