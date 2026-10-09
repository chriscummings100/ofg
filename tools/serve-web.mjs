// Serves allowlisted browser build outputs and fixture assets on loopback with explicit MIME types.
import { createServer, request as httpRequest } from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

const buildDirectory = fileURLToPath(new URL('../build/web/', import.meta.url));
const files = {
    '/terrain-workers-proof.js': ['terrain-workers-proof.js', 'text/javascript'],
    '/terrain-workers-proof.wasm': ['terrain-workers-proof.wasm', 'application/wasm'],
    '/': ['index.html', 'text/html'],
    '/index.html': ['index.html', 'text/html'],
    '/index.js': ['index.js', 'text/javascript'],
    '/index.wasm': ['index.wasm', 'application/wasm']
};

// Returns a listening server; callers own close(). Port zero selects an unused port for smoke runs.
export async function startWebServer(port = 8080, terrainPort = 8765) {
    await stat(resolve(buildDirectory, 'index.html'));
    // Explicit asset allowlist keeps requests inside the packaged build directory.
    files['/assets/checker.png'] = ['assets/checker.png', 'image/png'];
    files['/assets/checker.jpg'] = ['assets/checker.jpg', 'image/jpeg'];
    for (const name of ['quaternius-superhero-male', 'quaternius-ual1-standard']) {
        const path = `assets/models/character/${name}.glb`;
        files['/' + path] = [path, 'model/gltf-binary'];
    }
    for (const extension of ['gltf', 'glb', 'bin']) {
        files['/assets/models/laboratory.' + extension] = ['assets/models/laboratory.' + extension, extension === 'gltf' ? 'model/gltf+json' : 'application/octet-stream'];
    }
    const server = createServer(async (request, response) => {
        response.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
        response.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
        const path = new URL(request.url, 'http://localhost').pathname;
        if (path.startsWith('/v1/') && ['GET', 'POST'].includes(request.method)) {
            const upstream = httpRequest({ hostname: '127.0.0.1', port: terrainPort, path, method: request.method,
                headers: { 'Content-Type': 'application/json', 'Cache-Control': request.headers['cache-control'] || '' } }, incoming => {
                response.writeHead(incoming.statusCode, { 'Content-Type': incoming.headers['content-type'] || 'application/octet-stream',
                    'Cache-Control': incoming.headers['cache-control'] || 'no-store' });
                incoming.on('error', () => response.destroy()).pipe(response);
            });
            upstream.on('error', () => { if (!response.headersSent) response.writeHead(502); response.end('Terrain service unavailable'); });
            response.on('close', () => upstream.destroy());
            request.pipe(upstream);
            return;
        }
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
