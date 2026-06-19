// Static development server for the OFG browser shell. It mirrors deployment
// isolation headers and serves generated WASM with the correct content type.

import { createReadStream, existsSync, statSync } from "node:fs";
import { createServer } from "node:http";
import { extname, isAbsolute, join, normalize, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const preferredPort = Number.parseInt(process.env.OFG_DEV_PORT ?? "5173", 10);
const host = "127.0.0.1";

const mimeTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "application/javascript; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".wasm", "application/wasm"],
  [".png", "image/png"]
]);

const server = createServer((request, response) => {
  const requestUrl = new URL(request.url ?? "/", `http://${host}`);
  const filePath = resolveRequestPath(requestUrl.pathname);

  applyHeaders(response);

  if (filePath === null || !existsSync(filePath) || !statSync(filePath).isFile()) {
    response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    response.end("Not found");
    return;
  }

  response.writeHead(200, {
    "Content-Type": mimeTypes.get(extname(filePath)) ?? "application/octet-stream"
  });
  createReadStream(filePath).pipe(response);
});

listenOnAvailablePort(preferredPort);

function resolveRequestPath(pathname) {
  const decodedPath = decodePath(pathname);
  if (decodedPath === null) {
    return null;
  }

  const normalizedPath = normalize(decodedPath);
  const relativePath = normalizedPath === sep ? "index.html" : normalizedPath.replace(/^[/\\]+/, "");
  const filePath = resolve(root, relativePath);
  const relativeToRoot = relative(root, filePath);

  if (
    relativeToRoot === "" ||
    isAbsolute(relativeToRoot) ||
    relativeToRoot === ".." ||
    relativeToRoot.startsWith(`..${sep}`)
  ) {
    return null;
  }

  if (statMaybeDirectory(filePath)) {
    return join(filePath, "index.html");
  }

  return filePath;
}

function decodePath(pathname) {
  try {
    return decodeURIComponent(pathname);
  } catch {
    return null;
  }
}

function statMaybeDirectory(path) {
  try {
    return statSync(path).isDirectory();
  } catch {
    return false;
  }
}

function applyHeaders(response) {
  response.setHeader("Cache-Control", "no-store");
  response.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  response.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  response.setHeader("Cross-Origin-Resource-Policy", "same-origin");
}

function listenOnAvailablePort(port) {
  server.once("error", (error) => {
    if (error.code === "EADDRINUSE") {
      listenOnAvailablePort(port + 1);
      return;
    }

    throw error;
  });

  server.listen(port, host, () => {
    console.log(`OFG dev server listening at http://${host}:${port}`);
  });
}
