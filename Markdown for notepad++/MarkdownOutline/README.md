# MarkdownOutline — Notepad++ 插件

在 Notepad++ 右侧显示当前 Markdown 文档的**多级大纲**（标题树）。
与内置的「函数列表」不同，这里的层级是真正的嵌套：H1 下面挂 H2，H2 下面挂 H3 ……，
每个节点双击后跳转到编辑器中对应的标题行。

## 功能

- 识别 ATX 标题（`## 标题`）和 Setext 标题（正文行 + `===` / `---`）。
- 跳过围栏代码块（``` 或 ~~~）和缩进代码块里的伪标题。
- 最多 6 级嵌套，H1..H6。
- 双击节点或选中后按 Enter 跳转到该标题所在行。
- 切换文档、修改文档时自动刷新（带 400ms 防抖，避免每敲一个字就重扫全文）。
- 非 Markdown 文件时面板显示提示文字。

## 大纲的展开与折叠

- 面板顶部有 **Expand All** / **Collapse All** 两个按钮。
- 打开文档时默认展开到**第 2 级**：H1 处于展开状态（因此 H2 可见），H2 本身保持折叠（因此 H3 不可见）。
- 用户手动展开或折叠的节点，其状态会在编辑触发重建后**保留下来**；只有新出现的标题才使用上面的默认策略。
- 点击 Expand All / Collapse All 之后，这个选择同样会被记住，重建后不会被打回原形。

## 编译

需要 MinGW-w64（MSYS2 自带即可）：

```bash
# 在 MSYS2 UCRT64 或 MINGW64 终端里
pacman -S --needed mingw-w64-x86_64-toolchain
```

然后在本目录执行：

```bash
# MSYS2 MINGW64 终端
cd /d/ziliao/code/opensource/markdown/MarkdownOutline
bash build.sh
```

产物：`MarkdownOutline.dll`（x64）。

> **推荐用 `build.sh` 而不是 `make`。** `build.sh` 会把 winpthread 静态链接进去，
> 产出的 DLL 只依赖 Windows 自带的系统库：
>
> ```
> COMCTL32.dll  GDI32.dll  KERNEL32.dll  msvcrt.dll  USER32.dll
> ```
>
> 而不会带上 `libwinpthread-1.dll` / `libgcc_s_seh-1.dll` / `libstdc++-6.dll`，
> 这样把 DLL 拷到 Notepad++ 目录就能直接用，不用再附带一堆运行库。
>
> `Makefile` 提供了等价的功能，但需要额外安装 `make`
> （`pacman -S make`），本机目前没装，所以日常用 `build.sh`。

### 前置依赖

**1. 编译器：MSYS2 + MinGW-w64（x64）**

```bash
# 在 MSYS2 里
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-winpthreads
```

下载地址：

| 用途 | 网址 |
| --- | --- |
| MSYS2 官方 | https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe |
| 清华镜像（快） | https://mirrors.tuna.tsinghua.edu.cn/msys2/distrib/msys2-x86_64-latest.sfx.exe |

**2. Notepad++ 源码树**

插件要用到里面的插件 API 头文件（`PluginInterface.h`、`Notepad_plus_msgs.h`、
`Docking.h`）和 Scintilla 的头文件。路径通过 `NPP_ROOT` 指定：

```bash
NPP_ROOT=/d/path/to/notepad-plus-plus bash build.sh
```

默认是 `/d/ziliao/code/opensource/notepad-plus-plus`。

### 常见问题

| 现象 | 原因与处理 |
| --- | --- |
| `libwinpthread.a not found` | 少装了 winthreads：`pacman -S mingw-w64-x86_64-winpthreads` |
| `PluginInterface.h: No such file` | `NPP_ROOT` 没指向 Notepad++ 源码根目录 |
| `x86_64-w64-mingw32-g++: command not found` | 没进 MINGW64 环境，或没把 `/mingw64/bin` 加进 `PATH` |
| 插件加载报「找不到依赖的 DLL」 | 用了 `make` 而没有静态链接 winpthread，改用 `build.sh` |
| Notepad++ 启动时插件没出现 | DLL 放错位置，见下面的「安装」 |

> Notepad++ 是什么位数，插件就编译成什么位数。
> 你的 `notepad++.exe` 是 64 位，所以需要 64 位的编译器（`x86_64-w64-mingw32-g++`）。

## 运行测试

两个测试都是独立可执行文件，不依赖 Notepad++：

```bash
# 标题解析（需要一个 .md 文件作为参数）
g++ -std=c++17 -O1 -DUNICODE -D_UNICODE -static -o parser_test.exe \
    parser_test.cpp MarkdownParser.cpp
./parser_test.exe /d/ziliao/code/opensource/notepad-plus-plus/BUILD.md

# 面板的展开/折叠行为（自带测试数据，无需参数）
g++ -std=c++17 -O1 -DUNICODE -D_UNICODE -static -o panel_test.exe \
    panel_test.cpp OutlinePanel.cpp MarkdownParser.cpp -lcomctl32 -lgdi32
./panel_test.exe
```

## 安装

把 `MarkdownOutline.dll` 放到：

```
D:\software\Notepad++\plugins\MarkdownOutline\MarkdownOutline.dll
```

注意：Notepad++ 只扫描 `plugins` 目录的**一级子目录**，每个子目录里同名 `.dll` 才会被当作插件加载。
所以 DLL 必须放在 `plugins\MarkdownOutline\` 里，不能直接放在 `plugins\` 下。

重启 Notepad++，然后：

**插件 → Markdown Outline → Show/Hide Markdown Outline**

面板默认停靠在右侧，与「函数列表」同一栏。

## 代码结构

| 文件 | 作用 |
| --- | --- |
| `MarkdownParser.h/.cpp` | Markdown 标题解析（纯逻辑，无 Win32 依赖） |
| `OutlinePanel.h/.cpp` | 面板窗口 + TreeView，按级别建树，负责展开/折叠策略 |
| `MarkdownOutline.cpp` | Notepad++ 插件接口（`setInfo` / `getName` / `getFuncsArray` / `beNotified` / `messageProc`） |
| `Makefile` | 构建脚本 |
| `parser_test.cpp` | 标题解析的独立测试 |
| `panel_test.cpp` | 面板展开/折叠行为的独立测试 |

## 使用的 Notepad++ 接口

| 接口 | 用途 |
| --- | --- |
| `NPPM_GETCURRENTSCINTILLA` | 取得当前主/副视图的 Scintilla 句柄 |
| `NPPM_GETFULLCURRENTPATH` | 判断当前文件是不是 Markdown |
| `NPPM_DMMREGASDCKDLG` | 把面板注册成可停靠窗口 |
| `NPPM_DMMSHOW` / `NPPM_DMMHIDE` | 显示/隐藏面板 |
| `NPPM_ADDSCNMODIFIEDFLAGS` | 订阅文本修改事件 |
| `NPPN_READY` / `NPPN_BUFFERACTIVATED` / `NPPN_LANGCHANGED` / `NPPN_FILEOPENED` | 刷新时机 |

## 已知限制

- 面板用 `DWS_ICONTAB` 但没有自绘标签图标，所以标签页上不显示图标。
- 深色模式下树视图配色跟随 Notepad++ 的通用插件暗色处理，个别细节可能与内置面板略有差异。
