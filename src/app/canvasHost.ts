// Browser canvas host for OFG. It owns DOM canvas lookup/creation and sizing,
// but deliberately does not own rendering or game simulation.

export interface CanvasSize {
  readonly cssWidth: number;
  readonly cssHeight: number;
  readonly physicalWidth: number;
  readonly physicalHeight: number;
  readonly devicePixelRatio: number;
  readonly changed: boolean;
}

export interface CanvasHost {
  readonly canvas: HTMLCanvasElement;
  readonly size: CanvasSize;
  resize(): CanvasSize;
  dispose(): void;
}

export interface CssSize {
  readonly width: number;
  readonly height: number;
}

export interface CanvasHostOptions {
  readonly document?: Document;
  readonly window?: BrowserWindowLike;
  readonly canvasId?: string;
  readonly parent?: HTMLElement;
  readonly getCssSize?: (canvas: HTMLCanvasElement) => CssSize;
}

export interface BrowserWindowLike {
  readonly innerWidth: number;
  readonly innerHeight: number;
  readonly devicePixelRatio?: number;
  addEventListener?(type: "resize", listener: () => void): void;
  removeEventListener?(type: "resize", listener: () => void): void;
}

interface BrowserDomRectLike {
  readonly width: number;
  readonly height: number;
}

const DEFAULT_CANVAS_ID = "ofg-canvas";
const MIN_DEVICE_PIXEL_RATIO = 1;
const MAX_DEVICE_PIXEL_RATIO = 2;

const ZERO_SIZE: CanvasSize = {
  cssWidth: 0,
  cssHeight: 0,
  physicalWidth: 0,
  physicalHeight: 0,
  devicePixelRatio: 1,
  changed: false
};

export function createCanvasHost(options: CanvasHostOptions = {}): CanvasHost {
  const documentRef = options.document ?? globalThis.document;
  const windowRef = options.window ?? globalThis.window;
  if (documentRef === undefined || windowRef === undefined) {
    throw new Error("Canvas host requires a browser-like document and window.");
  }

  const canvas = findOrCreateCanvas(documentRef, options);
  const host = new BrowserCanvasHost(canvas, windowRef, options.getCssSize);
  host.resize();
  return host;
}

export function clampDevicePixelRatio(rawValue: number | undefined): number {
  const finiteValue = rawValue !== undefined && Number.isFinite(rawValue) ? rawValue : 1;
  return Math.min(
    MAX_DEVICE_PIXEL_RATIO,
    Math.max(MIN_DEVICE_PIXEL_RATIO, finiteValue)
  );
}

class BrowserCanvasHost implements CanvasHost {
  readonly canvas: HTMLCanvasElement;

  #size: CanvasSize = ZERO_SIZE;
  readonly #window: BrowserWindowLike;
  readonly #getCssSize: ((canvas: HTMLCanvasElement) => CssSize) | undefined;

  constructor(
    canvas: HTMLCanvasElement,
    windowRef: BrowserWindowLike,
    getCssSize?: (canvas: HTMLCanvasElement) => CssSize
  ) {
    this.canvas = canvas;
    this.#window = windowRef;
    this.#getCssSize = getCssSize;
  }

  get size(): CanvasSize {
    return this.#size;
  }

  resize(): CanvasSize {
    const cssSize = readCssSize(this.canvas, this.#window, this.#getCssSize);
    const devicePixelRatio = clampDevicePixelRatio(this.#window.devicePixelRatio);
    const nextSize = buildCanvasSize(cssSize, devicePixelRatio, this.#size);

    if (nextSize.changed) {
      this.canvas.width = nextSize.physicalWidth;
      this.canvas.height = nextSize.physicalHeight;
    }

    this.#size = nextSize;
    return nextSize;
  }

  dispose(): void {
  }
}

function findOrCreateCanvas(
  documentRef: Document,
  options: CanvasHostOptions
): HTMLCanvasElement {
  const canvasId = options.canvasId ?? DEFAULT_CANVAS_ID;
  const existing = documentRef.getElementById(canvasId);
  if (isCanvasElement(existing, documentRef)) {
    return existing;
  }
  if (existing !== null) {
    throw new Error(`Element #${canvasId} exists but is not a canvas.`);
  }

  const canvas = documentRef.createElement("canvas");
  canvas.id = canvasId;
  (options.parent ?? documentRef.body).appendChild(canvas);
  return canvas;
}

function isCanvasElement(
  element: HTMLElement | null,
  documentRef: Document
): element is HTMLCanvasElement {
  const canvasConstructor = documentRef.defaultView?.HTMLCanvasElement;
  if (canvasConstructor !== undefined && element instanceof canvasConstructor) {
    return true;
  }

  return element?.tagName.toLowerCase() === "canvas";
}

function readCssSize(
  canvas: HTMLCanvasElement,
  windowRef: BrowserWindowLike,
  getCssSize: ((canvas: HTMLCanvasElement) => CssSize) | undefined
): CssSize {
  if (getCssSize !== undefined) {
    return normalizeCssSize(getCssSize(canvas));
  }

  const bounds = canvas.getBoundingClientRect();
  if (hasLayoutBounds(bounds)) {
    return normalizeCssSize({ width: bounds.width, height: bounds.height });
  }

  return normalizeCssSize({
    width: windowRef.innerWidth,
    height: windowRef.innerHeight
  });
}

function normalizeCssSize(size: CssSize): CssSize {
  return {
    width: Number.isFinite(size.width) ? Math.max(0, size.width) : 0,
    height: Number.isFinite(size.height) ? Math.max(0, size.height) : 0
  };
}

function hasLayoutBounds(bounds: BrowserDomRectLike): boolean {
  return bounds.width !== 0 || bounds.height !== 0;
}

function buildCanvasSize(
  cssSize: CssSize,
  devicePixelRatio: number,
  previous: CanvasSize
): CanvasSize {
  const physicalWidth = Math.floor(cssSize.width * devicePixelRatio);
  const physicalHeight = Math.floor(cssSize.height * devicePixelRatio);
  const changed =
    previous.physicalWidth !== physicalWidth ||
    previous.physicalHeight !== physicalHeight ||
    previous.devicePixelRatio !== devicePixelRatio;

  return {
    cssWidth: cssSize.width,
    cssHeight: cssSize.height,
    physicalWidth,
    physicalHeight,
    devicePixelRatio,
    changed
  };
}
