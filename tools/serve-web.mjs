// Serves allowlisted browser build outputs and fixture assets on loopback with explicit MIME types.
import { createServer } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

const buildDirectory = fileURLToPath(new URL('../build/web/', import.meta.url));
const files = {
    '/': ['index.html', 'text/html'],
    '/index.html': ['index.html', 'text/html'],
    '/index.js': ['index.js', 'text/javascript'],
    '/index.wasm': ['index.wasm', 'application/wasm']
};

// Returns a listening server; callers own close(). Port zero selects an unused port for smoke runs.
export async function startWebServer(port = 8080) {
    await stat(resolve(buildDirectory, 'index.html'));
    // Explicit asset allowlist keeps requests inside the packaged build directory.
    files['/assets/checker.png'] = ['assets/checker.png', 'image/png'];
    files['/assets/checker.jpg'] = ['assets/checker.jpg', 'image/jpeg'];
    const server = createServer(async (request, response) => {
        const entry = files[new URL(request.url, 'http://localhost').pathname];
        if (!entry || !['GET', 'HEAD'].includes(request.method)) {
            response.writeHead(404).end();
            return;
        }
        try {
            const path = resolve(buildDirectory, entry[0]);
            const info = await stat(path);
            response.writeHead(200, {
                'Content-Type': entry[1], 'Content-Length': info.size, 'Cache-Control': 'no-store'
            });
            if (request.method === 'HEAD') response.end();
            else createReadStream(path).on('error', () => response.destroy()).pipe(response);
        } catch {
            response.writeHead(404).end('Build the web target first.');
        }
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', resolve);
    });
    return server;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const server = await startWebServer();
    console.log(`OFG browser build: http://127.0.0.1:${server.address().port}`);
}
