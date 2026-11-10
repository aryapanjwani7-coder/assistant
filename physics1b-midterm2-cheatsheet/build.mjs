import fs from 'fs';
import katex from 'katex';
import { chromium } from 'playwright-core';
import * as F from './figs.mjs';

const fs_ = process.argv[2] || '5.7pt';
let html = fs.readFileSync('src.html', 'utf8');
html = html.replace('%%DOPPLER%%', F.dopplerFig()).replace('%%SPHERE%%', F.sphereFig()).replace('%%POT2D%%', F.pot2DFig()).replace('%%CUBE%%', F.cubeFig()).replace('%%DIPOLE%%', F.dipoleFig()).replace('%%PIPES%%', F.pipesFig());
let errs = 0;
const render = (tex, display) => {
  tex = tex.replace(/&gt;/g,'>').replace(/&lt;/g,'<').replace(/&amp;/g,'&');
  try { return katex.renderToString(tex, { displayMode: display, throwOnError: true, strict: false }); }
  catch (e) { errs++; console.error('KaTeX error:', e.message); return `<span style="color:red">${tex}</span>`; }
};
html = html.replace(/\\\[([\s\S]+?)\\\]/g, (_, t) => render(t, true));
html = html.replace(/\\\(([\s\S]+?)\\\)/g, (_, t) => render(t, false));
html = html.replace('<body>', `<body style="--fs:${fs_}">`);
fs.writeFileSync('out.html', html);

const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome' }).catch(async () => chromium.launch());
const page = await browser.newPage({ viewport: { width: 794, height: 1123 }, deviceScaleFactor: 3 });
await page.goto('file://' + process.cwd() + '/out.html');
await page.evaluate(() => document.fonts.ready);
const info = await page.evaluate(() => {
  return [...document.querySelectorAll('.col')].map((c) => {
    const last = c.lastElementChild.getBoundingClientRect();
    const cr = c.getBoundingClientRect(); const sc=0.955;
    return { id: c.id, height: Math.round(cr.height), used: Math.round(c.scrollHeight), slack: Math.round(cr.bottom - last.bottom), secs:[...c.children].map(x=>Math.round(x.getBoundingClientRect().height)+'/'+Math.round(x.scrollHeight)) };
  });
});
console.log(JSON.stringify(info));
await page.pdf({ path: 'cheatsheet.pdf', format: 'A4', printBackground: true, margin: { top: 0, bottom: 0, left: 0, right: 0 } });
await page.screenshot({ path: 'preview.png', fullPage: false });
await browser.close();
console.log('katex errors', errs);
