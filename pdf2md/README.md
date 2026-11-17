# pdf2md —— 将文本型 PDF 转换为 Markdown

该工具参考同级 `mhtml2md` 的组织方式，使用一个入口脚本完成文档解析、Markdown 组装、命令行调用和严格校验。

主要面向 Word 等工具导出的、内含可提取文字的 PDF，重点保留：

- PDF 内置书签对应的标题层级；
- GFM 管道表格，包括合并单元格与跨行内容；
- 原页面顺序、分页标记与首页图片；
- 下划线命令名、`<parameter>` 等技术文本；
- 转换前后的非空白字符完整性校验。

---

## 一、环境准备

需要 Python 3.10 或更高版本：

```powershell
python --version
python -m pip install -r requirements.txt
```

核心依赖是 PyMuPDF：

- 直接读取 PDF 文字对象，避免 OCR 引入错字；
- 读取书签、文字坐标、表格线和嵌入图片；
- `Page.find_tables()` 可返回表格边界、行列与单元格坐标。

---

## 二、用法

### 2.1 转换单个 PDF

```powershell
python pdf2md.py "D:\path\document.pdf"
```

默认在 PDF 同目录生成同名 `.md` 和 `<文件名>_assets` 图片目录。

指定输出文件：

```powershell
python pdf2md.py "D:\path\document.pdf" "D:\output\document.md"
```

本次 AT 指令文档的完整命令：

```powershell
python pdf2md.py `
  "D:\work\IPC\低功耗\ATBM6463\DOC\ATBM6463 AT command.pdf" `
  "D:\ziliao\记录\ATBM6463 AT command.md" `
  --workers 4 `
  --table-strategy lines_strict `
  --images first
```

### 2.2 批量转换

```powershell
python pdf2md.py --dir "D:\PDF目录"
python pdf2md.py --dir "D:\PDF目录" --out "D:\Markdown目录"
```

### 2.3 常用参数

| 参数 | 说明 |
|---|---|
| `--workers 4` | 并行解析的页数；内存紧张时设为 `1` |
| `--table-strategy lines_strict` | 仅用明确表格线识别表格，适合 Word 导出 PDF |
| `--table-strategy lines` | 较宽松的线条表格识别 |
| `--table-strategy text` | 无边框表格可尝试文字对齐识别 |
| `--images first` | 只导出首页图片，可避免将水印碎片当作正文图片 |
| `--images all` | 导出所有页面的嵌入位图 |
| `--images none` | 不导出图片 |
| `--no-strict` | 字符或标题校验失败时仍保留输出 |

---

## 三、转换步骤与方法

### 步骤 1：确认 PDF 是文本型文档

逐页调用 `page.get_text()`。如果整份 PDF 没有可提取文字，工具会停止并提示先做 OCR。

对文本型 PDF 使用 OCR 反而会引入错字，因此本工具优先使用 PDF 内部字符映射。

### 步骤 2：读取页面文字行和坐标

```python
page_dict = page.get_text("dict", sort=True)
```

每个文字行保留：

- 原始文本；
- `bbox=(x0, y0, x1, y1)` 坐标；
- 原文块编号和行号。

坐标会用于判断文字属于普通段落还是表格单元格。

### 步骤 3：识别表格边界

```python
tables = page.find_tables(strategy="lines_strict")
```

不直接信任 `table.extract()` 的单元格文本，因为 PDF 中的下划线可能被重排。例如：

```text
AT+WAKEUP_GPIO
```

可能被错误拆成：

```text
AT+WAKEUP GPIO
_ _ _
```

本工具只使用 `find_tables()` 返回的单元格坐标，然后重新从原始文字行取字。

### 步骤 4：按文字中心点归属单元格

文字边界可能同时与上下两个单元格相交。如果按“边框相交”取字，同一内容会出现两次。

工具使用文字行中心点：

```python
center_x = (x0 + x1) / 2
center_y = (y0 + y1) / 2
```

只将文字归属到中心点所在的单元格，可消除跨行重复。

### 步骤 5：处理合并单元格

PyMuPDF 可能在多个表格行中返回同一个合并单元格坐标。工具使用坐标集合去重，只在第一次出现时输出内容。

### 步骤 6：恢复 PDF 书签标题

```python
toc = document.get_toc(simple=False)
```

书签级别映射为 Markdown 的 `#` 至 `######`。

需要处理两种特殊情况：

1. 章节号与标题在 PDF 中是两个文字行，如 `3.15` 和 `AT+WIFI_AP_STA_LIST ...`；
2. 一个大表格边界把下一章标题合并进表格。

工具先在普通文字行中做归一化匹配，再搜索表格行。如果标题在表格内，就在该行前后拆分表格，将该行恢复为 Markdown 标题。

### 步骤 7：生成 GFM 管道表格

有明确表头的表格：

```markdown
|指令|描述|指令示例|
|---|---|---|
|AT+GET_VER|获取系统版本|AT+GET_VER|
```

“字段—值”表格没有真实表头，则增加空表头行，不吞掉第一条数据：

```markdown
|||
|---|---|
|指令|AT+GET_VER|
|参数|无|
```

单元格内换行转换为 `<br>`，管道符转换为 `\|`。

### 步骤 8：转义 Markdown/HTML 敏感字符

`<pin>`、`<length>` 等参数不能直接写入 Markdown，否则可能被当成 HTML 标签隐藏。

工具输出为：

```markdown
&lt;pin&gt;
```

渲染后仍显示原文 `<pin>`。

### 步骤 9：提取图片与保留分页

图片写入 `<输出文件名>_assets` 目录，Markdown 使用相对路径引用。

每页加入不可见标记和打印分页：

```html
<!-- PDF 第 1 页 -->
<div style="page-break-after: always;"></div>
```

### 步骤 10：做严格内容校验

对每页执行：

1. 从 PDF 直接提取原文；
2. 从分析后的段落与单元格重建可见文本；
3. 去除纯排版空白；
4. 比较字符计数，检测漏字或重复字符；
5. 检查书签标题匹配数。

严格模式下，字符不一致或标题未全部匹配时，命令会以失败状态结束。

---

## 四、作为 Python 库调用

```python
from pdf2md import convert_pdf

report = convert_pdf(
    r"D:\path\document.pdf",
    r"D:\path\document.md",
    workers=4,
    table_strategy="lines_strict",
    images="first",
    strict=True,
)
print(report)
```

`report` 包含页数、表格数、书签数、字符异常页和阅读顺序警告页。

---

## 五、验证

```powershell
python test.py "D:\path\document.pdf"
```

默认会尝试验证本次使用的 `ATBM6463 AT command.pdf`。测试项包括：

- 输出不为空；
- 页面标记数与 PDF 页数一致；
- 存在 Markdown 管道表格；
- 没有将表格写成围栏代码块；
- 书签标题全部匹配；
- 无字符缺失或重复。

---

## 六、已知限制

1. 扫描型 PDF 需先 OCR；本工具本身不使用 OCR。
2. PDF 是坐标格式，不像 DOCX/HTML 那样保存语义结构；非标准复杂表格可能需调整 `--table-strategy`。
3. 不带表格线的表格可尝试 `--table-strategy text`，但误判风险更高。
4. `--images all` 可能把水印、背景碎片也导出；技术文档建议先用 `--images first`。
5. Markdown 不支持原生合并单元格，工具会展平为普通网格，但保留文本内容。

