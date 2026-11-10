# mhtml2md —— 浏览器保存的网页(.mhtml) 转 Markdown

把浏览器「网页另存为 → 网页，单文件(.mhtml)」保存的页面，转成 Markdown 文件。
重点支持 **腾讯元宝 / 千问等 AI 对话分享页**（问答 + 深度思考 + 代码块 + 表格完整保留），
同时也能处理普通技术网页。

---

## 一、环境准备

只需要 Node.js（已装则跳过）：

```powershell
node -v          # 需要 v18 以上
npm -v
```

安装依赖：

```powershell
cd D:\ziliao\code\mhtml2md
npm install linkedom
```

`linkedom` 是纯 JS 的 DOM 解析器，不需要浏览器、不需要联网抓取，
完全在本地离线运行。

---

## 二、用法

### 2.1 转换单个文件

```powershell
node mhtml2md.js "D:\path\某个网页.mhtml"
```

默认在同目录生成同名 `.md`（`xxx.mhtml` → `xxx.md`）。

也可以指定输出路径：

```powershell
node mhtml2md.js "D:\path\某个网页.mhtml" "D:\path\输出.md"
```

> **注意（Windows 编码坑）**：PowerShell 5.1 往原生程序传中文参数时，
> 会按 GBK 编码，路径中的中文可能变成乱码导致「文件不存在」。
> 三种规避方式，任选其一：
>
> 1. 改成 **cmd** 窗口执行（`chcp 65001` 后再执行）；
> 2. 用 **拖拽** 文件到 cmd 窗口获取正确路径；
> 3. 在 PowerShell 里先切到目标目录，再执行 `node mhtml2md.js xxx.mhtml`（**不含中文路径参数**）——最省事。

### 2.2 批量转换一个目录

```powershell
node mhtml2md.js --dir "D:\某个目录"
```

生成到同目录；也可以指定输出目录：

```powershell
node mhtml2md.js --dir "D:\某个目录" --out "D:\输出目录"
```

输出示例：

```
[OK] WSL与Ubuntu服务器Git同步方案.mhtml  ->  WSL与Ubuntu服务器Git同步方案.md  (10927 chars)
[FAIL] 某个文件.mhtml  :  未找到 text/html 部件，文件可能不是有效的 .mhtml
```

---

## 三、转换原理（内部做了什么）

### 步骤 1：解析 MHTML 容器

`.mhtml` 是 MIME multipart 格式，正文分部件存储，每个部件带自己的头部：

```
Content-Type: text/html
Content-Transfer-Encoding: quoted-printable
```

工具在字节层面定位 `text/html` 部件，截到下一个 `--MultipartBoundary...--` 为止。

### 步骤 2：解码传输编码

浏览器保存时通常用 `quoted-printable`（`=XX` 表示一个字节，`=\r\n` 表示软换行），
部分用 `base64`。

> **关键点**：mhtml 正文是**按字节**存储的。
> 必须先在「字节层」完成解码，再整体按 UTF-8 转成字符串。
> 如果先按 latin1 转字符串再解码，中文会全部变成乱码（`æŽç¡Œ` 这种）。
> —— 这是手写这个工具最容易踩的坑。

### 步骤 3：DOM 解析

用 `linkedom` 的 `parseHTML()` 解析成 DOM 树（不执行 JS，秒级完成）。

### 步骤 4：识别页面类型

- 含 `.agent-chat__list__item` → **AI 对话页**，走对话专用提取逻辑；
- 否则 → **普通网页**，自动定位正文容器（取「文本量最大且最内层」的元素，避开导航栏/页脚）再提取。

### 步骤 5：DOM → Markdown 映射规则

| 网页元素 | Markdown 输出 |
|---|---|
| `<h1>`~`<h6>` | `#` ~ `######` |
| `<pre><code>` | ```` ```语言 ```` 围栏代码块，保留原始缩进；语言取自代码块头部标签 |
| `<ol>` / `<ul>` | 有序 / 无序列表，嵌套按 2 空格缩进 |
| `<table>` | GFM 管道表格，首行下方自动补 `\|---\|` 分隔行 |
| `<blockquote>` | `> ` 前缀 |
| `<hr>` | `---` |
| `<strong>` / `<em>` | `**粗体**` / `*斜体*` |
| `<code>`（行内） | `` `代码` `` |
| `<br>` | 换行 |
| 用户提问区 | `**问：** 提问内容` |
| 「深度思考」折叠块 | `<details><summary>…</summary>…</details>` |
| 搜索引用的文献列表 | `- 1. 标题` 列表 |
| 追问按钮组 | `- 按钮文字` 列表 |
| 隐藏元素（`display:none` / `aria-hidden`） | 丢弃 |
| 零宽字符 `\u200b` | 剔除 |

### 步骤 6：整页组装

```
# 页面标题
---
**问：** 第一个问题
<details>深度思考</details>
AI 回答正文
---
**问：** 第二个问题
AI 回答正文
```

---

## 四、作为库调用（进阶）

```javascript
const { extractHtml, htmlToMarkdown } = require('./mhtml2md.js');
const fs = require('fs');

const html = extractHtml(fs.readFileSync('xxx.mhtml'));  // -> UTF-8 HTML 字符串
const md   = htmlToMarkdown(html);                        // -> Markdown 字符串
fs.writeFileSync('xxx.md', md, 'utf8');
```

---

## 五、验证

`npm test` 会遍历 `D:\ziliao` 下所有 `.mhtml`（488 个），统计：

- 转换成功率
- 空输出数
- 中文乱码数（latin1 误解码特征检测）

当前结果：**488/488 成功，0 失败，0 空输出，0 乱码**。

---

## 六、已知限制

1. **只支持 `.mhtml`**。单文件 HTML（`.html`）需先用 Chrome「另存为 → 网页，单文件」。
2. **不执行 JS**。纯前端渲染（无 SSR）的页面会拿不到内容；
   腾讯元宝/千问的分享页是 SSR 的，所以没问题。
3. **需要登录才能访问的页面**，请先用浏览器登录并「另存为」后再转换（工具只读本地文件，不联网）。
4. 复杂排版（多栏、绝对定位、Canvas 图表）的网页会被简化成线性文本。
