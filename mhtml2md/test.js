// 验证脚本：遍历 D:\ziliao 下所有 .mhtml，统计转换成功率 / 空输出 / 中文乱码
// 运行： node test.js
const fs = require('fs');
const path = require('path');
const { extractHtml, htmlToMarkdown } = require('./mhtml2md.js');

const SCAN_ROOT = process.argv[2] || 'D:\\ziliao';
const MAX_DEPTH = 7;

const all = [];
(function walk(dir, depth) {
  if (depth > MAX_DEPTH) return;
  let entries;
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of entries) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, depth + 1);
    else if (/\.mhtml$/i.test(e.name)) all.push(p);
  }
})(SCAN_ROOT, 0);

let ok = 0, fail = 0, empty = 0, mojibake = 0;
const fails = [];
const mojis = [];

for (const p of all) {
  let md;
  try {
    md = htmlToMarkdown(extractHtml(fs.readFileSync(p)));
  } catch (e) {
    fail++; fails.push(p + '  ::  ' + e.message); continue;
  }
  if (!md || md.trim().length < 20) { empty++; fails.push(p + '  ::  EMPTY OUTPUT'); continue; }

  // 乱码检测：latin1 误解码会在标题里产生大量 U+00C0–U+00FF 字符
  const first = md.split('\n')[0];
  let latin1 = 0;
  for (const ch of first) { const c = ch.codePointAt(0); if (c >= 0xC0 && c <= 0xFF) latin1++; }
  if (latin1 > 3) { mojibake++; mojis.push(p + '  ::  ' + first.slice(0, 80)); continue; }

  ok++;
}

console.log('扫描目录 : ' + SCAN_ROOT);
console.log('mhtml 总数: ' + all.length);
console.log('成功      : ' + ok);
console.log('失败      : ' + fail);
console.log('空输出    : ' + empty);
console.log('中文乱码  : ' + mojibake);
if (fails.length) { console.log('\n失败/空:'); fails.forEach((f) => console.log('  ' + f)); }
if (mojis.length) { console.log('\n乱码:'); mojis.slice(0, 20).forEach((f) => console.log('  ' + f)); }

process.exit(fail || empty || mojibake ? 1 : 0);
