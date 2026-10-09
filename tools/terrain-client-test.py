"""Own a real loopback terrain server and run the native HTTP/cache integration cases against it."""
import argparse
import asyncio
from pathlib import Path
import os
import socket
import subprocess
import tempfile
import threading

from fastapi import Request, Response
import uvicorn
from terrain_service.app import create_app
from terrain_service.content import adopt_revision, publish_revision
from terrain_service.island import IslandParameters, generate_island


def main():
    """Use ephemeral ports/directories and explicit startup readiness; stop only the owned server."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--executable', type=Path, default=Path('build/cpu-tests/ofg-terrain-test.exe'))
    parser.add_argument('--browser', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ofg-terrain-client-') as temporary:
        root = Path(temporary)
        revision = publish_revision(root, 'demo', generate_island(IslandParameters()))
        adopt_revision(root, 'demo', revision)
        app = create_app(root, read_only=True)
        online = True
        release = asyncio.Event()

        @app.middleware('http')
        async def interrupt_tiles(request: Request, call_next):
            """Simulate a disconnected content endpoint without changing the client's source identity."""
            if not online and request.url.path.endswith('.bin'):
                return Response(status_code=503)
            return await call_next(request)

        @app.post('/probe/state')
        async def state(request: Request):
            """Apply test-only availability changes acknowledged before the next client operation."""
            nonlocal online
            online = (await request.json())['online']
            return {'online': online}

        @app.get('/probe/oversized')
        async def oversized():
            """Return an advertised payload exceeding the client's explicit byte cap."""
            return Response(b'x' * 2048)

        @app.get('/probe/held')
        async def held():
            """Hold one operation behind an explicit cancellation-test gate."""
            await release.wait()
            return Response(b'done')

        @app.post('/probe/release')
        async def release_held():
            """Release the gate before server shutdown, including when a request was already aborted."""
            release.set()
            return {}

        ready = threading.Event()

        class Server(uvicorn.Server):
            """Publish startup readiness to the process owning the native test invocation."""

            async def startup(self, sockets=None):
                """Signal only after listening and lifespan initialization succeeded."""
                await super().startup(sockets)
                ready.set()

        sock = socket.socket()
        sock.bind(('127.0.0.1', 0))
        server = Server(uvicorn.Config(app, log_level='error', timeout_graceful_shutdown=1))
        thread = threading.Thread(target=server.run, kwargs={'sockets': [sock]})
        thread.start()
        try:
            if not ready.wait(20):
                raise RuntimeError('Terrain integration server did not become ready')
            environment = dict(os.environ, OFG_TERRAIN_TEST_URL=f'http://127.0.0.1:{sock.getsockname()[1]}',
                               OFG_TERRAIN_TEST_MANIFEST=f'/v1/islands/demo/revisions/{revision}/manifest',
                               OFG_TERRAIN_TEST_CACHE=str(root / 'cache'))
            command = ['node', 'tools/terrain-service-smoke.mjs'] if args.browser else [str(args.executable.resolve()), '--test-suite=terrain-http', '--no-colors']
            result = subprocess.run(command, env=environment, timeout=420 if args.browser else 90)
        finally:
            server.should_exit = True
            thread.join(10)
            sock.close()
        if thread.is_alive():
            raise RuntimeError('Owned test server did not stop')
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
