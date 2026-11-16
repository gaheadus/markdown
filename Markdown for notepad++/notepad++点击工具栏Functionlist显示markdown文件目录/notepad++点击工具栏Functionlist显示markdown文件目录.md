# 排查 Markdown 函数列表配置

> 使用"C:\Program Files\Notepad++\notepad++.exe"软件“D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md”文件，增加"C:\Program Files\Notepad++\functionList\markdown.xml"文件，修改"C:\Program Files\Notepad++\functionList\overrideMap.xml"文件、增加“\<association id="markdown.xml" userDefinedLangName="Markdown" />”这一行。关闭notepad++.exe、重新打开，点击菜单栏按钮“函数列表”，发现并没有在右侧显示“OD600 wifi AP绑定方式分析.md”这个文件的标题/目录，请检查哪里配置出错了？

<details><summary>6 previous messages</summary>

> 我先核对 Notepad++ 的版本、实际加载的配置目录，以及 `markdown.xml` / `overrideMap.xml` 的内容和目标文档标题语法。重点会判断：配置是否放错目录、关联字段是否用错，以及解析正则是否匹配 Markdown 标题。整个检查先只读，不修改文件。
>
> <details><summary>Ran 2 commands</summary>
>
> - Ran `$paths = @(
>   'C:\Program Files\Notepad++\notepad++.exe',
>   'C:\Program Files\Notepad++\functionList\markdown.xml',
>   'C:\Program Files\Notepad++\functionList\overrideMap.xml',
>   'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md'
> )
> Get-Item -LiteralPath $paths | Select-Object FullName, Length, LastWriteTime
> (Get-Item -LiteralPath 'C:\Program Files\Notepad++\notepad++.exe').VersionInfo | Select-Object FileVersion, ProductVersion
> Get-Process -Name notepad++ -ErrorAction SilentlyContinue | Select-Object Id, Path, StartTime`
> - Ran `$exe = Get-Item -LiteralPath 'C:\Program Files\Notepad++\notepad++.exe'
> "FileVersion=$($exe.VersionInfo.FileVersion)"
> "ProductVersion=$($exe.VersionInfo.ProductVersion)"
> "--- markdown.xml ---"
> Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\markdown.xml' -Raw
> "--- overrideMap relevant lines ---"
> Select-String -LiteralPath 'C:\Program Files\Notepad++\functionList\overrideMap.xml' -Pattern 'markdown|association' -Context 2,2
> "--- document headings ---"
> Select-String -LiteralPath 'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md' -Pattern '^\s{0,3}#{1,6}\s+|^\s*(=+|-+)\s*$' | Select-Object -First 80 LineNumber, Line`
>
> </details>
>
> 已发现一个明确问题：这份 `.md` 在 Notepad++ 中通常属于内置 Markdown 语言，但新增行使用的是“用户自定义语言（UDL）”关联；只有真的存在名为 `Markdown` 的 UDL 时才会命中。文档本身有大量标准 `# / ##` 标题，不是“文档没标题”。我再确认本机实际配置目录及该版本对应的 Markdown `langID`，避免只给出猜测值。
>
> <details><summary>Ran 3 commands</summary>
>
> - Ran `$candidateRoots = @(
>   'C:\Program Files\Notepad++',
>   "$env:APPDATA\Notepad++",
>   "$env:LOCALAPPDATA\Notepad++"
> )
> foreach ($root in $candidateRoots) {
>   "--- ROOT: $root ---"
>   if (Test-Path -LiteralPath $root) {
>     Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
>       Where-Object { $_.Name -in @('overrideMap.xml','markdown.xml','langs.xml','config.xml','userDefineLang.xml') -or $_.FullName -match 'functionList' } |
>       Select-Object FullName, Length, LastWriteTime
>   } else { 'NOT FOUND' }
> }
> "--- doLocalConf ---"
> Get-Item -LiteralPath 'C:\Program Files\Notepad++\doLocalConf.xml' -ErrorAction SilentlyContinue | Select-Object FullName,Length,LastWriteTime
> "--- Markdown language entries ---"
> $languageFiles = @('C:\Program Files\Notepad++\langs.model.xml', "$env:APPDATA\Notepad++\langs.xml", "$env:LOCALAPPDATA\Notepad++\langs.xml")
> foreach ($f in $languageFiles) { if (Test-Path -LiteralPath $f) { "FILE: $f"; Select-String -LiteralPath $f -Pattern 'Markdown|markdown|\.md' -Context 0,1 } }
> "--- UDL names ---"
> $udlFiles = @('C:\Program Files\Notepad++\userDefineLang.xml', "$env:APPDATA\Notepad++\userDefineLang.xml", "$env:LOCALAPPDATA\Notepad++\userDefineLang.xml")
> foreach ($f in $udlFiles) { if (Test-Path -LiteralPath $f) { "FILE: $f"; Select-String -LiteralPath $f -Pattern '<UserLang|Markdown' } }`
> - Ran `"--- userDefineLang.xml ---"
> Get-Content -LiteralPath "$env:APPDATA\Notepad++\userDefineLang.xml" -Raw
> "--- sample function-list parser roots ---"
> Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\python.xml' -TotalCount 80
> "--- language names/extensions containing md ---"
> [xml]$langs = Get-Content -LiteralPath "$env:APPDATA\Notepad++\langs.xml" -Raw
> $langs.NotepadPlus.Languages.Language | Where-Object { $_.name -match 'mark|md' -or $_.ext -match '(^|\s)(md|markdown)(\s|$)' } | Select-Object name, ext
> "--- files referencing target document ---"
> $npRoot = "$env:APPDATA\Notepad++"
> Get-ChildItem -LiteralPath $npRoot -File -ErrorAction SilentlyContinue | ForEach-Object {
>   Select-String -LiteralPath $_.FullName -SimpleMatch 'OD600 wifi AP绑定方式分析.md' -ErrorAction SilentlyContinue
> } | Select-Object Path,LineNumber,Line`
> - Ran `[xml]$session = Get-Content -LiteralPath "$env:APPDATA\Notepad++\session.xml" -Raw
> $target = $session.NotepadPlus.Session.mainView.File | Where-Object { $_.filename -eq 'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md' }
> if (-not $target) { $target = $session.NotepadPlus.Session.subView.File | Where-Object { $_.filename -eq 'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md' } }
> $target.Attributes | ForEach-Object { [pscustomobject]@{Name=$_.Name; Value=$_.Value} }
> "--- parser top-level shapes ---"
> Get-ChildItem -LiteralPath 'C:\Program Files\Notepad++\functionList' -Filter '*.xml' | Where-Object Name -ne 'overrideMap.xml' | ForEach-Object {
>   try {
>     [xml]$x = Get-Content -LiteralPath $_.FullName -Raw
>     $root = $x.DocumentElement.Name
>     $hasFunctionList = $null -ne $x.NotepadPlus.functionList
>     [pscustomobject]@{Name=$_.Name; Root=$root; HasFunctionList=$hasFunctionList}
>   } catch {
>     [pscustomobject]@{Name=$_.Name; Root='XML ERROR'; HasFunctionList=$false}
>   }
> } | Group-Object Root,HasFunctionList | Select-Object Count,Name
> "--- parser direct-child element names ---"
> [xml]$md = Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\markdown.xml' -Raw
> $md.DocumentElement.ChildNodes | Where-Object NodeType -eq Element | Select-Object Name
> "--- known parser using classRange ---"
> Select-String -Path 'C:\Program Files\Notepad++\functionList\*.xml' -Pattern 'classRange\s+mainExpr="\^\$"' | Select-Object Path,LineNumber,Line`
>
> </details>
>
> 进一步确认后，配置目录和关联行本身是生效路径：当前是标准安装模式，函数列表规则从 `C:\Program Files\Notepad++\functionList` 读取；而且本机确实存在名为 `Markdown`、扩展名含 `md` 的 UDL。真正的首要故障在 `markdown.xml`：它不是合法 XML，且文件结构不符合 Notepad++ 函数列表解析器格式。我正在把具体错误点和可用的修正版整理出来。
>
> <details><summary>Ran 6 commands</summary>
>
> - Ran `"--- XML validation ---"
> foreach ($f in @('C:\Program Files\Notepad++\functionList\overrideMap.xml','C:\Program Files\Notepad++\functionList\markdown.xml',"$env:APPDATA\Notepad++\userDefineLang.xml")) {
>   try { [xml](Get-Content -LiteralPath $f -Raw) | Out-Null; "OK: $f" }
>   catch { "INVALID: $f"; $_.Exception.Message }
> }
> "--- exact target session entry ---"
> Select-String -LiteralPath "$env:APPDATA\Notepad++\session.xml" -SimpleMatch 'OD600 wifi AP绑定方式分析.md' | ForEach-Object { $_.Line }
> "--- parser root/children expectations from samples ---"
> foreach ($f in @('python.xml','batch.xml','ini.xml')) {
>   [xml]$x = Get-Content -LiteralPath (Join-Path 'C:\Program Files\Notepad++\functionList' $f) -Raw
>   $parser = $x.NotepadPlus.functionList.parser
>   [pscustomobject]@{
>     File=$f
>     Root=$x.DocumentElement.Name
>     Wrapper=$x.NotepadPlus.functionList.LocalName
>     ParserId=$parser.id
>     ParserChildren=(($parser.ChildNodes | Where-Object NodeType -eq Element | ForEach-Object Name) -join ',')
>   }
> } | Format-Table -AutoSize`
> - Ran `"--- XML validation ---"
> foreach ($f in @('C:\Program Files\Notepad++\functionList\overrideMap.xml','C:\Program Files\Notepad++\functionList\markdown.xml',"$env:APPDATA\Notepad++\userDefineLang.xml")) {
>   try { [xml](Get-Content -LiteralPath $f -Raw) | Out-Null; "OK: $f" }
>   catch { "INVALID: $f"; $_.Exception.Message }
> }
> "--- exact target session entry ---"
> Select-String -LiteralPath "$env:APPDATA\Notepad++\session.xml" -SimpleMatch 'OD600 wifi AP绑定方式分析.md' | ForEach-Object { $_.Line }
> "--- parser root/children expectations from samples ---"
> $rows = foreach ($f in @('python.xml','batch.xml','ini.xml')) {
>   [xml]$x = Get-Content -LiteralPath (Join-Path 'C:\Program Files\Notepad++\functionList' $f) -Raw
>   $parser = $x.NotepadPlus.functionList.parser
>   [pscustomobject]@{
>     File=$f
>     Root=$x.DocumentElement.Name
>     Wrapper=$x.NotepadPlus.functionList.LocalName
>     ParserId=$parser.id
>     ParserChildren=(($parser.ChildNodes | Where-Object NodeType -eq Element | ForEach-Object Name) -join ',')
>   }
> }
> $rows | Format-Table -AutoSize`
> - Ran `$files = Get-ChildItem -LiteralPath 'C:\Program Files\Notepad++\functionList' -Filter '*.xml' | Where-Object Name -notin @('overrideMap.xml','markdown.xml')
> foreach ($f in $files) {
>   try {
>     [xml]$x = Get-Content -LiteralPath $f.FullName -Raw
>     $p = $x.NotepadPlus.functionList.parser
>     if ($p) {
>       $directFunctions = @($p.ChildNodes | Where-Object { $_.NodeType -eq 'Element' -and $_.Name -eq 'function' })
>       if ($directFunctions.Count -gt 1) { [pscustomobject]@{File=$f.Name; Count=$directFunctions.Count} }
>     }
>   } catch {}
> }
> "--- sample direct function parser ---"
> Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\ini.xml' -Raw`
> - Ran `"--- className nested under function? ---"
> Select-String -Path 'C:\Program Files\Notepad++\functionList\*.xml' -Pattern '<className>' -Context 3,1 | Select-Object -First 30 Path,LineNumber,Line,Context
> "--- multiple function nodes examples ---"
> Select-String -Path 'C:\Program Files\Notepad++\functionList\*.xml' -Pattern '^\s*<function\s*$|^\s*<function\s+mainExpr' | Group-Object Path | Sort-Object Count -Descending | Select-Object -First 8 Count,Name
> "--- line-numbered markdown.xml ---"
> $i=0; Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\markdown.xml' | ForEach-Object { $i++; '{0,3}: {1}' -f $i,$_ }`
> - Ran `Get-ChildItem -LiteralPath 'C:\Program Files\Notepad++' -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match 'function.*\.(xsd|xml)$|\.xsd$' } | Select-Object FullName
> "--- direct structure around cpp class range ---"
> Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\cpp.xml' | Select-Object -Skip 48 -First 40`
> - Ran `Get-Content -LiteralPath 'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md' -TotalCount 35
> "--- heading counts ---"
> $content = Get-Content -LiteralPath 'D:\work\IPC\cursor问答、vscode问答、AI问答、问题 记录\OD600 wifi AP绑定方式分析.md'
> 1..6 | ForEach-Object {
>   $level=$_
>   $count = @($content | Where-Object { $_ -match ('^#{' + $level + '}\s+') }).Count
>   [pscustomobject]@{Level=$level;Count=$count}
> } | Format-Table -AutoSize`
>
> </details>
</details>

问题出在 `markdown.xml`，不是 `overrideMap.xml`。

检查结果：

- `overrideMap.xml` 中的关联位于注释外，写法正确：
  ```xml
  <association id="markdown.xml" userDefinedLangName="Markdown" />
  ```
- 本机确实存在名为 `Markdown` 的 UDL，并关联了 `md markdown` 扩展名。
- Notepad++ 会话也显示目标文件当前语言为 `lang="Markdown"`，所以关联条件已经满足。
- 目标文档包含 2 个一级标题和 11 个二级标题，文档内容没有问题。

`markdown.xml` 有几个错误：

1. 第 8 行的 `(?<=#+\s)` 包含未转义的 `<`，导致整个文件不是合法 XML。实际错误为“第 8 行，位置 35，无效的特性字符 `<`”。
2. 缺少 Notepad++ 要求的外层结构：
   ```xml
   <NotepadPlus>
       <functionList>
           ...
       </functionList>
   </NotepadPlus>
   ```
3. `classRange mainExpr="^$"` 只匹配空行，里面的标题规则无法扫描整篇文档。
4. `(?<=#+\s)` 还是不定长后向断言，即使写成 `(?&lt;=...)`，正则引擎也可能拒绝。
5. `<function>` 内的 `<className>` 结构也不符合普通函数列表规则。

针对当前文档，可将 `markdown.xml` 完整替换为：

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<NotepadPlus>
    <functionList>
        <parser
            id="markdown"
            displayName="Markdown Headings"
            commentExpr=""
        >
            <function
                mainExpr="^\h{0,3}#{1,6}\h+\K.*?(?=\h*#*\h*$)"
            >
                <functionName>
                    <nameExpr expr=".*" />
                </functionName>
            </function>
        </parser>
    </functionList>
</NotepadPlus>
```

保存后确保所有 `notepad++.exe` 进程完全退出，再重新打开目标 `.md` 文件和“函数列表”。该修正版会显示文档中的 `#` 到 `######` 标题；当前文件的 13 个标题应该全部列出。

本次仅进行了只读检查，没有修改配置文件。


# 总结修改点

> 验证修改生效了。
> 请总结以上生效的配置中修改了哪些文件、修改内容是什么？

<details><summary>2 previous messages</summary>

> 我会按当前磁盘上的最终配置再核对一次，确保总结的是实际生效内容，而不是之前那份故障配置。
>
> <details><summary>Ran a command</summary>
>
> - Ran `"--- markdown.xml ---"
> $i=0; Get-Content -LiteralPath 'C:\Program Files\Notepad++\functionList\markdown.xml' | ForEach-Object { $i++; '{0,3}: {1}' -f $i,$_ }
> "--- overrideMap markdown association ---"
> Select-String -LiteralPath 'C:\Program Files\Notepad++\functionList\overrideMap.xml' -Pattern 'markdown.xml' -Context 2,2
> "--- Markdown UDL declaration ---"
> Select-String -LiteralPath "$env:APPDATA\Notepad++\userDefineLang.xml" -Pattern '<UserLang name="Markdown"'`
>
> </details>
</details>

生效配置的完整链路涉及 3 个文件，其中函数列表功能直接修改了前两个。

1. `C:\Program Files\Notepad++\functionList\markdown.xml`

新增/重写 Markdown 标题解析器：

- 补全合法 XML 外层结构：`NotepadPlus → functionList → parser`。
- 定义解析器 ID 为 `markdown`。
- 使用正则匹配 Markdown 的 `#`～`######` 标题。
- 去掉标题前的 `#`、空格及末尾可选的 `#`，在函数列表中只显示标题文字。

核心规则：

```xml
<function mainExpr="^\h{0,3}#{1,6}\h+\K.*?(?=\h*#*\h*$)">
    <functionName>
        <nameExpr expr=".*" />
    </functionName>
</function>
```

2. `C:\Program Files\Notepad++\functionList\overrideMap.xml`

在 `<associationMap>` 内增加：

```xml
<association id="markdown.xml" userDefinedLangName="Markdown" />
```

作用是：当当前文档语言为名为 `Markdown` 的用户自定义语言时，加载 `markdown.xml` 函数列表解析器。

3. `C:\Users\6959\AppData\Roaming\Notepad++\userDefineLang.xml`

其中存在 Markdown 用户自定义语言：

```xml
<UserLang name="Markdown" ext="md markdown" udlVersion="2.1">
```

作用是：

- 定义名为 `Markdown` 的 UDL；
- 将 `.md` 和 `.markdown` 扩展名识别为该语言；
- 与 `overrideMap.xml` 中的 `userDefinedLangName="Markdown"` 精确对应。

最后，完全退出并重新启动 Notepad++，使函数列表配置重新加载。
