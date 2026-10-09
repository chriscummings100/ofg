"""Bounded read-only terrain HTTP endpoints and a separate laboratory generation control API."""

import asyncio
from contextlib import asynccontextmanager
import json
from pathlib import Path

from fastapi import FastAPI, Request, Response
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict

from .content import Revision, validate_name, validate_revision
from .island import IslandParameters
from .jobs import JobManager
from .protocol import encode_tile


class JobInput(BaseModel):
    """One immutable generation request, distinct from editor draft parameters."""

    model_config = ConfigDict(extra='forbid')
    island: str
    operation_id: str
    parameters: IslandParameters


def create_app(root: Path, read_only: bool = False, output_limit: int = 4 << 30, gate=None) -> FastAPI:
    """Create an isolated service; read-only mode creates no generator/job state."""
    root = root.resolve()
    manager = None if read_only else JobManager(root, output_limit, gate)

    async def collect():
        """Collect completed jobs independently of whether an editor is polling."""
        while True:
            await asyncio.to_thread(manager.refresh)
            await asyncio.sleep(.1)

    @asynccontextmanager
    async def lifespan(app):
        """Bound collector and worker lifetime to the HTTP server's lifetime."""
        collector = asyncio.create_task(collect()) if manager else None
        yield
        if collector:
            collector.cancel()
            try:
                await collector
            except asyncio.CancelledError:
                pass
            await asyncio.to_thread(manager.close)

    app = FastAPI(title='OFG terrain service', lifespan=lifespan)
    app.state.jobs = manager

    @app.middleware('http')
    async def bounded_controls(request: Request, call_next):
        """Bound control-body buffering and bypass HTTP caches for mutable control/manifest reads."""
        if request.method == 'POST':
            body = bytearray()
            async for part in request.stream():
                body.extend(part)
                if len(body) > 256 << 10:
                    return JSONResponse({'error': 'Control body exceeds 256 KiB'}, status_code=413)
            request._body = bytes(body)
        response = await call_next(request)
        if '/revisions/' not in request.url.path or response.status_code != 200:
            response.headers['Cache-Control'] = 'no-store'
        else:
            response.headers['Cache-Control'] = 'public, max-age=31536000, immutable'
        return response

    @app.exception_handler(ValueError)
    async def invalid(request, error):
        """Expose invalid requests as errors, never as empty terrain."""
        return JSONResponse({'error': str(error)}, status_code=422)

    @app.exception_handler(FileNotFoundError)
    async def missing(request, error):
        """Report unknown immutable content without substituting latest."""
        return JSONResponse({'error': 'Unknown island or revision'}, status_code=404)

    @app.exception_handler(KeyError)
    async def unknown_job(request, error):
        """Map unknown job identities to a useful HTTP response."""
        return JSONResponse({'error': 'Unknown job'}, status_code=404)

    @app.exception_handler(RuntimeError)
    async def conflict(request, error):
        """Report conflicting controls or explicit storage backpressure."""
        return JSONResponse({'error': str(error)}, status_code=409)

    @app.get('/v1/health')
    def health():
        """Report capabilities without contacting the generator worker."""
        return {'version': 1, 'generators': [] if read_only else ['flat-island'], 'read_only': read_only}

    @app.get('/v1/islands/{island}/manifest')
    def latest(island: str):
        """Resolve latest only after an atomic completed-publication pointer update."""
        validate_name(island)
        pointer = json.loads((root / island / 'latest.json').read_text(encoding='utf-8'))
        revision = validate_revision(pointer['revision'])
        return {'revision': revision, 'manifest_url': f'/v1/islands/{island}/revisions/{revision}/manifest'}

    @app.get('/v1/islands/{island}/revisions/{revision}/manifest')
    def manifest(island: str, revision: str):
        """Serve immutable metadata without loading source arrays."""
        directory = root / validate_name(island) / 'revisions' / validate_revision(revision)
        return json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))

    @app.get('/v1/islands/{island}/revisions/{revision}/terrain/{rx}/{rz}/{depth}/{x}/{z}.bin')
    def tile(island: str, revision: str, rx: int, rz: int, depth: int, x: int, z: int):
        """Sample an immutable field in the HTTP thread pool, preserving event-loop responsiveness."""
        body = encode_tile(Revision(root, island, revision), rx, rz, depth, x, z)
        return Response(body, media_type='application/octet-stream')

    if manager:
        @app.post('/v1/jobs', status_code=202)
        def submit(body: JobInput):
            """Start an idempotently addressed generation operation."""
            return manager.submit(body.island, body.operation_id, body.parameters)

        @app.get('/v1/jobs/{identifier}')
        def status(identifier: str):
            """Observe progress independently from viewer adoption."""
            return manager.status(identifier)

        @app.post('/v1/jobs/{identifier}/cancel')
        def cancel(identifier: str):
            """Request cancellation without deleting the previous publication."""
            return manager.cancel(identifier)

    return app

