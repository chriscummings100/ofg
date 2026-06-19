import { Window } from "happy-dom";

const windowRef = new Window({
  url: "http://127.0.0.1:5173/"
});

Object.defineProperty(globalThis, "window", {
  configurable: true,
  value: windowRef
});

Object.defineProperty(globalThis, "document", {
  configurable: true,
  value: windowRef.document
});

Object.defineProperty(globalThis, "HTMLElement", {
  configurable: true,
  value: windowRef.HTMLElement
});

Object.defineProperty(globalThis, "HTMLCanvasElement", {
  configurable: true,
  value: windowRef.HTMLCanvasElement
});
