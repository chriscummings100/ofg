import assert from "node:assert/strict";
import { Window } from "happy-dom";
import {
  clampDevicePixelRatio,
  createCanvasHost,
  type BrowserWindowLike,
  type CssSize
} from "../../src/app/canvasHost.js";

describe("canvas host", () => {
  it("creates a canvas when the page does not provide one", () => {
    const testWindow = createWindow(800, 450, 1);
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => ({ width: 800, height: 450 })
    });

    assert.equal(host.canvas.id, "ofg-canvas");
    assert.equal(testWindow.document.querySelectorAll("canvas").length, 1);
    assert.deepEqual(sizeWithoutChanged(host.size), {
      cssWidth: 800,
      cssHeight: 450,
      physicalWidth: 800,
      physicalHeight: 450,
      devicePixelRatio: 1
    });
  });

  it("uses an existing canvas by id", () => {
    const testWindow = createWindow(640, 360, 1);
    const existing = testWindow.document.createElement("canvas");
    existing.id = "ofg-canvas";
    testWindow.document.body.appendChild(existing);

    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => ({ width: 640, height: 360 })
    });

    assert.equal(host.canvas, existing);
    assert.equal(testWindow.document.querySelectorAll("canvas").length, 1);
  });

  it("clamps device pixel ratio for predictable backing sizes", () => {
    assert.equal(clampDevicePixelRatio(undefined), 1);
    assert.equal(clampDevicePixelRatio(0.5), 1);
    assert.equal(clampDevicePixelRatio(1), 1);
    assert.equal(clampDevicePixelRatio(1.5), 1.5);
    assert.equal(clampDevicePixelRatio(2), 2);
    assert.equal(clampDevicePixelRatio(3.25), 2);
  });

  it("resizes only when physical size or clamped dpr changes", () => {
    const testWindow = createWindow(300, 200, 1.5);
    let cssSize: CssSize = { width: 300, height: 200 };
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => cssSize
    });

    assert.equal(host.size.physicalWidth, 450);
    assert.equal(host.size.physicalHeight, 300);
    assert.equal(host.size.changed, true);

    const unchanged = host.resize();
    assert.equal(unchanged.changed, false);

    cssSize = { width: 301, height: 200 };
    const changed = host.resize();
    assert.equal(changed.changed, true);
    assert.equal(changed.physicalWidth, 451);
    assert.equal(changed.physicalHeight, 300);
  });

  it("does not consume browser resize changes before the render loop observes them", () => {
    const testWindow = createWindow(300, 200, 1);
    let cssSize: CssSize = { width: 300, height: 200 };
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => cssSize
    });

    cssSize = { width: 420, height: 240 };
    testWindow.dispatchEvent(new testWindow.Event("resize"));

    const changed = host.resize();
    assert.equal(changed.changed, true);
    assert.equal(changed.physicalWidth, 420);
    assert.equal(changed.physicalHeight, 240);
  });

  it("floors after applying device pixel ratio", () => {
    const testWindow = createWindow(300, 200, 1.5);
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => ({ width: 300.75, height: 200.25 })
    });

    assert.equal(host.size.cssWidth, 300.75);
    assert.equal(host.size.cssHeight, 200.25);
    assert.equal(host.size.physicalWidth, 451);
    assert.equal(host.size.physicalHeight, 300);
  });

  it("preserves zero-sized canvas axes for the runtime facade", () => {
    const testWindow = createWindow(1024, 768, 2);
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow,
      getCssSize: () => ({ width: 0, height: 320 })
    });

    assert.equal(host.size.cssWidth, 0);
    assert.equal(host.size.cssHeight, 320);
    assert.equal(host.size.physicalWidth, 0);
    assert.equal(host.size.physicalHeight, 640);
  });

  it("throws when the configured canvas id belongs to another element", () => {
    const testWindow = createWindow(640, 360, 1);
    const existing = testWindow.document.createElement("div");
    existing.id = "ofg-canvas";
    testWindow.document.body.appendChild(existing);

    assert.throws(
      () =>
        createCanvasHost({
          document: browserDocument(testWindow),
          window: testWindow,
          getCssSize: () => ({ width: 640, height: 360 })
        }),
      /exists but is not a canvas/
    );
  });

  it("falls back to the viewport when layout bounds are unavailable", () => {
    const testWindow = createWindow(1024, 768, 2);
    const host = createCanvasHost({
      document: browserDocument(testWindow),
      window: testWindow
    });

    assert.equal(host.size.cssWidth, 1024);
    assert.equal(host.size.cssHeight, 768);
    assert.equal(host.size.physicalWidth, 2048);
    assert.equal(host.size.physicalHeight, 1536);
  });
});

function createWindow(
  width: number,
  height: number,
  devicePixelRatio: number
): Window & BrowserWindowLike {
  const testWindow = new Window({ url: "http://127.0.0.1:5173/" });
  Object.defineProperty(testWindow, "innerWidth", {
    configurable: true,
    value: width
  });
  Object.defineProperty(testWindow, "innerHeight", {
    configurable: true,
    value: height
  });
  Object.defineProperty(testWindow, "devicePixelRatio", {
    configurable: true,
    value: devicePixelRatio
  });
  return testWindow as Window & BrowserWindowLike;
}

function browserDocument(testWindow: Window): Document {
  return testWindow.document as unknown as Document;
}

function sizeWithoutChanged(size: {
  readonly cssWidth: number;
  readonly cssHeight: number;
  readonly physicalWidth: number;
  readonly physicalHeight: number;
  readonly devicePixelRatio: number;
}): Omit<typeof size, "changed"> {
  return {
    cssWidth: size.cssWidth,
    cssHeight: size.cssHeight,
    physicalWidth: size.physicalWidth,
    physicalHeight: size.physicalHeight,
    devicePixelRatio: size.devicePixelRatio
  };
}
