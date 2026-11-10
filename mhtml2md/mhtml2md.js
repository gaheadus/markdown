#!/usr/bin/env node
/**
 * mhtml2md —— 把浏览器保存的 .mhtml（腾讯元宝 / 千问等 AI 对话页）转换为 Markdown
 *
 * 用法：
 *   node mhtml2md.js <输入.mhtml> [输出.md]
 *   node mhtml2md.js --dir <目录> [--out <输出目录>]
 *
 * 依赖：linkedom（npm i linkedom）
 */

'use strict';

const fs = require('fs');
const path = require('path');
const { parseHTML } = require('linkedom');

// ---------------------------------------------------------------- MHTML 解码

/** quoted-printable 解码，输出仍为 latin1 字节串（后续统一按 UTF-8 转换） */
function decodeQuotedPrintableBytes(raw) {
  const hex = '0123456789ABCDEF';
  const bytes = [];
  for (let i = 0; i < raw.length; i++) {
    const ch = raw[i];
    if (ch !== '=') { bytes.push(raw.charCodeAt(i) & 0xff); continue; }
    // 软换行：= CRLF / = LF
    if (raw[i + 1] === '\r' && raw[i + 2] === '\n') { i += 2; continue; }
    if (raw[i + 1] === '\n') { continue; }
    const h1 = raw[i + 1];
    const h2 = raw[i + 2];
    if (hex.includes(h1) && hex.includes(h2)) {
      bytes.push(parseInt(h1 + h2, 16));
      i += 2;
      continue;
    }
    bytes.push(0x3d); // '='
  }
  return Buffer.from(bytes);
}

/**
 * 从 mhtml 原始字节中取出 text/html 部件并解码为 HTML 字符串（UTF-8）
 *
 * 关键点：mhtml 正文按「字节」存储，必须先在字节层完成 quoted-printable / base64
 * 解码，再整体按 UTF-8 转字符串。若先按 latin1 转字符串再解码，中文会乱码。
 */
function extractHtml(mhtmlBuffer) {
  const mhtmlText = mhtmlBuffer.toString('latin1');

  const re = /Content-Type:\s*text\/html[^\r\n]*\r?\n([^\r\n]*\r?\n)*?\r?\n/i;
  const m = re.exec(mhtmlText);
  if (!m) throw new Error('未找到 text/html 部件，文件可能不是有效的 .mhtml');

  const rawText = mhtmlText.slice(m.index + m[0].length);

  // 截到下一个 boundary
  const boundary = /--MultipartBoundary[^\r\n]*--/i.exec(rawText);
  const raw = boundary ? rawText.slice(0, boundary.index) : rawText;

  // 传输编码
  const enc = /Content-Transfer-Encoding:\s*([^\r\n]+)/i.exec(
    mhtmlText.slice(Math.max(0, m.index - 400), m.index + 400)
  );
  const encoding = enc ? enc[1].trim().toLowerCase() : 'quoted-printable';

  const bytes = encoding === 'base64'
    ? Buffer.from(raw.replace(/\s+/g, ''), 'base64')
    : decodeQuotedPrintableBytes(raw);

  return bytes.toString('utf8');
}

// ------------------------------------------------------------ innerText 垫片

const BLOCK_TAGS = new Set([
  'ADDRESS', 'ARTICLE', 'ASIDE', 'BLOCKQUOTE', 'DETAILS', 'DIALOG', 'DD', 'DIV',
  'DL', 'DT', 'FIELDSET', 'FIGCAPTION', 'FIGURE', 'FOOTER', 'FORM', 'H1', 'H2',
  'H3', 'H4', 'H5', 'H6', 'HEADER', 'HGROUP', 'HR', 'LI', 'MAIN', 'NAV', 'OL',
  'P', 'PRE', 'SECTION', 'TABLE', 'TBODY', 'TD', 'TFOOT', 'TH', 'THEAD', 'TR', 'UL',
]);
const SKIP_TAGS = new Set(['SCRIPT', 'STYLE', 'TEMPLATE', 'NOSCRIPT', 'HEAD', 'SVG']);

/** 近似浏览器 innerText：跳过隐藏元素、<br> 换行、块级元素后换行 */
function innerTextOf(el) {
  let out = '';
  const walk = (node) => {
    for (const child of node.childNodes) {
      if (child.nodeType === 3) { out += child.nodeValue; continue; }
      if (child.nodeType !== 1) continue;
      const tag = child.tagName;
      if (!tag || SKIP_TAGS.has(tag)) continue;
      const cl = String(child.className || '');
      if (cl.includes('__measure')) continue;
      if (child.getAttribute('aria-hidden') === 'true') continue;
      if (child.getAttribute('hidden') !== null) continue;
      if ((child.getAttribute('style') || '').includes('display:none')) continue;
      if (tag === 'BR') { out += '\n'; continue; }
      walk(child);
      if (BLOCK_TAGS.has(tag)) out += '\n';
    }
  };
  walk(el);
  return out
    .replace(/\u200b/g, '')
    .replace(/[ \t]+\n/g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    .replace(/^\n+|\n+$/g, '');
}

// -------------------------------------------------------------- DOM -> MD

const MD_LANG_MAP = { '纯文本': 'Plain Text', 'Pure Text': 'Plain Text' };

function buildConverter() {
  const conv = (root) => {
    const out = [];

    const inline = (n) => {
      let s = '';
      for (const c of n.childNodes) {
        if (c.nodeType === 3) { s += c.nodeValue; continue; }
        if (c.nodeType !== 1) continue;
        const tag = c.tagName;
        if (!tag) continue;
        const cl = String(c.className || '');
        if (tag === 'CODE' && cl.includes('code__inline')) { s += innerTextOf(c); continue; }
        if (tag === 'STRONG' || tag === 'B') { s += '**' + inline(c) + '**'; continue; }
        if (tag === 'EM' || tag === 'I') { s += '*' + inline(c) + '*'; continue; }
        if (tag === 'BR') { s += '\n'; continue; }
        if (tag === 'IMG' && !cl.includes('ref-list')) {
          s += '![' + (c.getAttribute('alt') || '') + '](' + (c.getAttribute('src') || '') + ')';
          continue;
        }
        if (cl.includes('ref-list')) continue;
        if (cl.includes('follow-up-form')) continue;
        s += inline(c);
      }
      return s;
    };

    const followUpOptions = (el) => [...el.querySelectorAll('.hyc-follow-up-form__options .hyc-follow-up-form__option-text')]
      .map((o) => innerTextOf(o).trim()).filter(Boolean);

    const block = (n) => {
      for (const c of n.childNodes) {
        // 裸文本节点（普通网页常见）
        if (c.nodeType === 3) {
          const t = c.nodeValue.trim();
          if (t) out.push(t);
          continue;
        }
        if (c.nodeType !== 1) continue;
        const tag = c.tagName;
        if (!tag) continue;
        const cl = String(c.className || '');

        if (cl.includes('ref-list')) continue;

        // 追问按钮组 -> 列表
        if (cl.includes('follow-up-form')) {
          const opts = followUpOptions(c);
          if (opts.length) out.push(opts.map((o) => '- ' + o).join('\n'));
          continue;
        }

        if (tag === 'DIV' && cl.includes('ybc-p')) {
          const t = inline(c).trim();
          if (t) out.push(t);
          const opts = followUpOptions(c);
          if (opts.length) out.push(opts.map((o) => '- ' + o).join('\n'));
          continue;
        }

        if (/^H[1-6]$/.test(tag)) {
          out.push('#'.repeat(Number(tag[1])) + ' ' + inline(c).trim());
          continue;
        }

        if (tag === 'PRE') {
          const code = c.querySelector('code');
          const langEl = c.querySelector('.hyc-common-markdown__code__langComponent__text');
          let lang = langEl ? innerTextOf(langEl).trim() : '';
          if (MD_LANG_MAP[lang]) lang = MD_LANG_MAP[lang];
          const txt = (code ? innerTextOf(code) : innerTextOf(c)).replace(/[\s\u00a0]+$/, '');
          out.push('```' + lang + '\n' + txt + '\n```');
          continue;
        }

        if (tag === 'HR') { out.push('---'); continue; }

        if (tag === 'UL' || tag === 'OL') {
          for (const li of c.children) {
            if (li.tagName !== 'LI') continue;
            const content = li.querySelector('.ybc-li-component_content');
            const ph = li.querySelector('.ybc-li-component__dot-placeholder');
            let marker = tag === 'OL' ? ((ph ? innerTextOf(ph).trim() : '') || '- ') : '- ';
            if (!marker) marker = '- ';
            if (content) {
              const save = out.length;
              block(content);
              const sub = out.splice(save);
              if (sub.length === 0) out.push(marker.trimEnd());
              else {
                out.push(marker + sub[0]);
                for (const x of sub.slice(1)) out.push('  ' + x);
              }
            } else {
              out.push(marker + inline(li).trim());
            }
          }
          continue;
        }

        if (tag === 'BLOCKQUOTE') {
          const save = out.length;
          block(c);
          const sub = out.splice(save);
          for (const x of sub) out.push('> ' + x);
          continue;
        }

        if (tag === 'TABLE') {
          const rows = [...c.querySelectorAll('tr')];
          rows.forEach((tr, ri) => {
            const cells = [...tr.children].map((td) => inline(td).trim().replace(/\n/g, ' '));
            out.push('| ' + cells.join(' | ') + ' |');
            if (ri === 0) out.push('| ' + cells.map(() => '---').join(' | ') + ' |');
          });
          continue;
        }

        if (tag === 'P') { const t = inline(c).trim(); if (t) out.push(t); continue; }

        block(c);
      }
    };

    block(root);
    return out.filter((x) => x !== '').join('\n\n');
  };

  return conv;
}

// ------------------------------------------------------------ 通用正文提取

/** 从任意网页提取「可读正文」作为 Markdown（适用于非 AI 对话页） */
function htmlToPlainMarkdown(html) {
  const { document } = parseHTML(html);
  const conv = buildConverter();

  const og = document.querySelector('meta[property="og:title"]');
  const titleEl = document.querySelector('title');
  const title = (og ? og.getAttribute('content') : '') || (titleEl ? innerTextOf(titleEl) : '') || '';

  // 选出「文本量最大且最内层」的元素作为正文容器：
  // 先求全文最大文本量，再在 >= 90% 最大值的元素中取深度最大的（最内层），
  // 这样能避开外层壳（含导航/页脚）而命中真正的正文块。
  const allEls = [...document.querySelectorAll('div,section,article,main,td')];
  let root = null;
  if (allEls.length) {
    const depthOf = (e) => { let d = 0, n = e; while ((n = n.parentElement)) d++; return d; };
    const lens = allEls.map((e) => innerTextOf(e).length);
    const maxLen = Math.max(...lens);
    if (maxLen > 200) {
      const best = allEls
        .map((e, i) => ({ e, len: lens[i], depth: depthOf(e) }))
        .filter((x) => x.len >= maxLen * 0.9)
        .sort((a, b) => b.depth - a.depth)[0];
      if (best) root = best.e;
    }
  }
  if (!root) root = document.body;

  const parts = [];
  if (title.trim()) parts.push('# ' + title.trim());
  const body = conv(root);
  if (body) parts.push(body);
  return parts.join('\n\n');
}

// ------------------------------------------------------------ 对话页提取

function htmlToMarkdown(html) {
  const { document } = parseHTML(html);

  const og = document.querySelector('meta[property="og:title"]');
  const titleEl = document.querySelector('title');
  const title = (og ? og.getAttribute('content') : '') || (titleEl ? innerTextOf(titleEl) : '') || '';

  const conv = buildConverter();
  const items = [...document.querySelectorAll('.agent-chat__list__item')];
  const parts = [];

  parts.push('# ' + title.trim());

  for (const it of items) {
    const speaker = it.getAttribute('data-conv-speaker');

    if (speaker === 'human') {
      const q = it.querySelector('.hyc-content-text');
      const qt = q ? innerTextOf(q).trim() : '';
      if (qt) parts.push('---\n\n**问：** ' + qt);
      continue;
    }

    // 深度思考
    const think = it.querySelector('.hyc-component-deepsearch-cot__think');
    if (think) {
      const tEl = think.querySelector('.hyc-component-deepsearch-cot__think__header__title');
      const tt = tEl ? innerTextOf(tEl).trim() : '';
      const ttxt = think.querySelector('.hyc-component-deepsearch-cot__think__content__item-text');
      const docs = [...think.querySelectorAll('.hyc-component-deepsearch-cot__think__content__item__doc')]
        .map((d) => {
          const num = d.querySelector('.hyc-component-deepsearch-cot__think__content__item__doc__num');
          const ttl = d.querySelector('.hyc-component-deepsearch-cot__think__content__item__doc__title__text');
          return ((num ? innerTextOf(num).trim() : '') + ' ' + (ttl ? innerTextOf(ttl).trim() : '')).trim();
        });
      let inner = '';
      if (ttxt) inner = conv(ttxt);
      if (docs.length) inner += (inner ? '\n\n' : '') + docs.map((d) => '- ' + d).join('\n');
      if (inner) parts.push('<details>\n<summary>' + tt + '</summary>\n\n' + inner + '\n\n</details>');
    }

    // 正文
    const mds = [...it.querySelectorAll('.hyc-content-md')]
      .filter((m) => !m.closest('.hyc-component-deepsearch-cot__think__content__item-text'));
    const bodies = mds.map((m) => conv(m)).filter(Boolean);
    if (bodies.length) parts.push(bodies.join('\n\n'));
  }

  if (items.length === 0) return htmlToPlainMarkdown(html);
  return parts.join('\n\n');
}

// ------------------------------------------------------------------ CLI

function convertFile(input, output) {
  const html = extractHtml(fs.readFileSync(input));
  const md = htmlToMarkdown(html);
  fs.writeFileSync(output, md, 'utf8');
  return md.length;
}

function main() {
  const args = process.argv.slice(2);
  if (!args.length) {
    console.error('用法: node mhtml2md.js <输入.mhtml> [输出.md]');
    console.error('      node mhtml2md.js --dir <目录> [--out <输出目录>]');
    process.exit(1);
  }

  if (args[0] === '--dir') {
    const dir = args[1];
    const outDirIdx = args.indexOf('--out');
    const outDir = outDirIdx > -1 ? args[outDirIdx + 1] : dir;
    const files = fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith('.mhtml'));
    if (!files.length) { console.error('目录内没有 .mhtml 文件'); process.exit(1); }
    fs.mkdirSync(outDir, { recursive: true });
    for (const f of files) {
      const out = path.join(outDir, f.replace(/\.mhtml$/i, '.md'));
      try {
        const n = convertFile(path.join(dir, f), out);
        console.log('[OK] ' + f + '  ->  ' + path.basename(out) + '  (' + n + ' chars)');
      } catch (e) {
        console.error('[FAIL] ' + f + ' : ' + e.message);
      }
    }
    return;
  }

  const input = args[0];
  const output = args[1] || input.replace(/\.mhtml$/i, '.md');
  const n = convertFile(input, output);
  console.log('[OK] ' + path.basename(input) + ' -> ' + output + '  (' + n + ' chars)');
}

if (require.main === module) main();

module.exports = { extractHtml, htmlToMarkdown, htmlToPlainMarkdown, convertFile, innerTextOf };
