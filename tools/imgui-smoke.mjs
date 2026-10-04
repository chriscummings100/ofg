// Focused interaction smoke for the shared docking workspace; uses rendered UI input, not value mutation.
import assert from 'node:assert/strict';
import {mkdir, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright-core';
import {PNG} from 'pngjs';

import {startWebServer} from './serve-web.mjs';

const artifacts = fileURLToPath(new URL('../artifacts/imgui/browser/', import.meta.url));
await mkdir(artifacts, {recursive : true});
const report = {
  errors : [],
  messages : [],
  checks : [],
  captures : []
};
const server = await startWebServer(0);
let browser, page;
// Waits on completed frames so event handling does not depend on arbitrary sleeps.
async function frames(count = 3) {
  const before = await page.evaluate(() => Module.frameCount);
  await page.waitForFunction(({before, count}) => Module.failed || Module.frameCount >= before + count, {before, count},
                             {timeout : 60000});
  assert.equal(await page.evaluate(() => Module.failed), false);
}
// Coordinates refer to canvas logical pixels, independent of the HTML status header.
async function click(x, y, options = {}) {
  const box = await page.locator('#canvas').boundingBox();
  await page.mouse.click(box.x + x, box.y + y, options);
  await frames();
}
// Ctrl-click enters numeric text mode; frame boundaries let modifier/edit events settle in UI ownership.
async function number(x, y, value) {
  await page.keyboard.down('Control');
  await frames();
  await click(x, y);
  await page.keyboard.up('Control');
  await frames();
  await page.keyboard.press('Control+a');
  await frames();
  await page.keyboard.type(value);
  await frames();
  await page.keyboard.press('Enter');
  await frames();
}
// Saves pixels and checks opaque, nonempty output; semantic checks are made against observed frame diagnostics.
async function capture(name) {
  const bytes = await page.locator('#canvas').screenshot({path : `${artifacts}/${name}.png`});
  const png = PNG.sync.read(bytes);
  let lit = 0;
  for (let i = 0; i < png.data.length; i += 4) {
    assert.equal(png.data[i + 3], 255);
    if (png.data[i] > 100)
      ++lit;
  }
  assert.ok(lit > 1000, 'Expected visible scene and UI');
  report.captures.push({name, width : png.width, height : png.height});
  return png;
}
// Opens the Window menu and chooses one of its fixed actions.
async function windowAction(y) {
  await click(30, 9);
  await click(65, y);
}
try {
  browser = await chromium.launch({channel : 'chrome', headless : !process.argv.includes('--headed')});
  report.browser = browser.version();
  const context = await browser.newContext({viewport : {width : 1440, height : 940}, deviceScaleFactor : 1});
  page = await context.newPage();
  page.on('console', message => {
    report.messages.push({type : message.type(), text : message.text()});
    if (message.type() === 'error' ||
        (message.type() === 'warning' && !message.text().startsWith('The powerPreference option is currently ignored')))
      report.errors.push(message.text());
  });
  page.on('pageerror', error => report.errors.push(String(error)));
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => Module.failed || Module.uiState && Module.frameCount > 5, null, {timeout : 120000});
  assert.equal(await page.evaluate(() => Module.failed), false);
  const initial = await page.evaluate(() => ({ui : Module.uiState, camera : Module.cameraPosition}));
  const original = await capture('workspace');
  await click(140, 112);
  await page.waitForFunction(() => Module.uiState.selection === 2);
  report.checks.push('named hierarchy selection');
  await number(1260, 95, '2');
  await page.waitForFunction(() => Module.uiState.exposure === 2);
  assert.deepEqual(await page.evaluate(() => Module.cameraPosition), initial.camera);
  assert.equal(await page.evaluate(() => Module.uiState.debugView), 0);
  const brighter = await capture('exposure');
  // UI background stays the same while the rendered scene changes with exposure.
  let sceneDifference = 0;
  const rect = initial.ui.sceneRect;
  for (let y = 80; y < original.height - 10; y += 8)
    for (let x = rect[0] + 20; x < rect[2] - 20; x += 8) {
      const i = (y * original.width + x) * 4;
      sceneDifference += Math.abs(original.data[i] - brighter.data[i]);
    }
  assert.ok(sceneDifference > 10000);
  const uiPixel = (350 * original.width + 1120) * 4;
  assert.deepEqual([...original.data.subarray(uiPixel, uiPixel + 3) ],
                   [...brighter.data.subarray(uiPixel, uiPixel + 3) ]);
  await number(1260, 95, '-1');
  assert.equal(await page.evaluate(() => Module.uiState.exposure), 2);
  await capture('invalid-edit');
  await click(1190, 615);
  await page.waitForFunction(() => Module.uiState.exposure === 1);
  report.checks.push('live exposure, invalid edit rejection, UI colour isolation, settings reset');
  await click(1250, 175);
  await page.keyboard.press('ArrowDown');
  await page.keyboard.press('Enter');
  await frames();
  await page.waitForFunction(() => Module.uiState.debugView === 1);
  await capture('normals');
  await page.keyboard.press('0');
  await frames();
  assert.equal(await page.evaluate(() => Module.uiState.debugView), 1);
  await click(1190, 615);
  await page.waitForFunction(() => Module.uiState.debugView === 0);
  report.checks.push('debug combo and shortcut ownership');
  const box = await page.locator('#canvas').boundingBox();
  const cx = (rect[0] + rect[2]) / 2, cy = (rect[1] + rect[3]) / 2;
  await click(cx, cy);
  await page.keyboard.press('f');
  await page.waitForFunction(() => Module.cameraPosition[2] === -5);
  await page.keyboard.press('r');
  await frames();
  await page.mouse.move(box.x + cx, box.y + cy);
  await page.mouse.down({button : 'right'});
  await page.waitForFunction(() => document.pointerLockElement === Module.canvas);
  await page.keyboard.down('w');
  await page.waitForFunction(z => Module.cameraPosition[2] > z + .1, initial.camera[2]);
  await page.keyboard.up('w');
  await page.mouse.up({button : 'right'});
  await page.waitForFunction(() => !document.pointerLockElement);
  await frames();
  await page.keyboard.press('r');
  await frames();
  await page.mouse.down({button : 'right'});
  await page.waitForFunction(() => document.pointerLockElement === Module.canvas);
  await page.keyboard.press('Escape');
  await page.mouse.up({button : 'right'});
  await page.waitForFunction(() => !document.pointerLockElement);
  await frames();
  await click(1200, 240, {button : 'right'});
  assert.equal(await page.evaluate(() => Boolean(document.pointerLockElement)), false);
  report.checks.push('RMB fly movement, release/Escape, UI does not capture');
  // Close Scene, verify update-only frames, and reopen from Window.
  await windowAction(28);
  await page.waitForFunction(() => Module.uiState.sceneRect.every(v => v === 0));
  await frames();
  await capture('scene-hidden');
  await windowAction(28);
  await page.waitForFunction(() => Module.uiState.sceneRect[2] > 0);
  report.checks.push('close/reopen viewport with updates continuing');
  // Hold the tab through multiple frames so undocking is observable during mouse movement.
  await page.mouse.move(box.x + 1190, box.y + 28);
  await frames();
  await page.mouse.down();
  await frames();
  await page.mouse.move(box.x + 1190, box.y + 90);
  await frames();
  await page.mouse.move(box.x + 800, box.y + 220, {steps : 12});
  await frames();
  await page.mouse.up();
  await frames();
  await capture('floating-settings');
  assert.ok((await page.evaluate(() => Module.uiState.sceneRect[2])) > 1300,
            'Settings should detach and free the right dock');
  report.checks.push('undock floating panel');
  // A floating tool over the scene must not allow fly capture through it.
  await click(800, 260, {button : 'right'});
  assert.equal(await page.evaluate(() => Boolean(document.pointerLockElement)), false);
  // Dock onto the centre target to form tabs with Scene, then return to the initial layout.
  const expanded = await page.evaluate(() => Module.uiState.sceneRect);
  await page.mouse.move(box.x + 800, box.y + 220);
  await frames();
  await page.mouse.down();
  await frames();
  await page.mouse.move(box.x + (expanded[0] + expanded[2]) / 2, box.y + (expanded[1] + expanded[3]) / 2, {steps : 12});
  await frames();
  await capture('docking-preview');
  await page.mouse.up();
  await frames();
  await capture('tabbed-settings');
  await page.waitForFunction(() => Module.uiState.sceneRect.every(value => value === 0));
  report.checks.push('dock/tab interaction and floating-tool capture isolation');
  await windowAction(85);
  await frames();
  await capture('reset-layout');
  // Hide hierarchy, wait for persisted visibility, reload and verify render values reset.
  await windowAction(47);
  await page.waitForFunction(() => localStorage.getItem('ofg.workspace.v1')?.startsWith('OFG-UI-1 1 0 1'));
  await page.reload();
  await page.waitForFunction(() => Module.uiState && Module.frameCount > 5, null, {timeout : 120000});
  assert.equal(await page.evaluate(() => Module.uiState.exposure), 1);
  assert.ok((await page.evaluate(() => localStorage.getItem('ofg.workspace.v1'))).startsWith('OFG-UI-1 1 0 1'));
  await capture('restored-layout');
  report.checks.push('layout/visibility persistence without render settings');
  await windowAction(85);
  await page.setViewportSize({width : 1100, height : 760});
  await frames(5);
  await capture('resized');
  report.checks.push('resize');
  await page.close();
  const dense = await browser.newContext({viewport : {width : 1100, height : 760}, deviceScaleFactor : 2});
  page = await dense.newPage();
  page.on('pageerror', error => report.errors.push(String(error)));
  page.on('console', m => {
    if (m.type() === 'error' ||
        (m.type() === 'warning' && !m.text().startsWith('The powerPreference option is currently ignored')))
      report.errors.push(m.text());
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.waitForFunction(() => Module.uiState && Module.frameCount > 5, null, {timeout : 120000});
  const denseImage = await capture('dpi-2');
  assert.equal(denseImage.width, 2200);
  await click(100, 112);
  await page.waitForFunction(() => Module.uiState.selection === 2);
  const denseRect = await page.evaluate(() => Module.uiState.sceneRect);
  await click((denseRect[0] + denseRect[2]) / 2, (denseRect[1] + denseRect[3]) / 2);
  const denseBox = await page.locator('#canvas').boundingBox();
  await page.mouse.move(denseBox.x + (denseRect[0] + denseRect[2]) / 2, denseBox.y + (denseRect[1] + denseRect[3]) / 2);
  await frames();
  await page.mouse.down({button : 'right'});
  await page.waitForFunction(() => document.pointerLockElement === Module.canvas);
  await page.keyboard.down('w');
  await frames();
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await page.waitForFunction(() => !document.pointerLockElement);
  await frames();
  const stopped = await page.evaluate(() => Module.cameraPosition);
  await frames(6);
  assert.deepEqual(await page.evaluate(() => Module.cameraPosition), stopped);
  await page.keyboard.up('w');
  await page.mouse.up({button : 'right'});
  report.checks.push('DPI 2 rendering/input, blur releases capture and movement');
  assert.deepEqual(report.errors, []);
  report.passed = true;
  console.log('ImGui browser interaction smoke passed.');
} catch (error) {
  report.failure = String(error.stack ?? error);
  if (page)
    await page.screenshot({path : `${artifacts}/failure.png`}).catch(() => {});
  throw error;
} finally {
  await writeFile(`${artifacts}/report.json`, JSON.stringify(report, null, 2));
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
