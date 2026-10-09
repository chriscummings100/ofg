"""Real loopback HTTP and spawned-process lifecycle tests; gates replace timing-dependent sleeps."""

from contextlib import contextmanager
import json
import multiprocessing
import socket
import threading

import httpx
import pytest
import uvicorn

from terrain_service.app import create_app
from terrain_service.content import adopt_revision, atomic_json, publish_revision
from terrain_service.island import IslandParameters, generate_island
from terrain_service.jobs import JobManager, generate_job
from terrain_service.protocol import decode_tile


@contextmanager
def loopback(app):
    """Run a real HTTP server on an owned ephemeral socket and join it on every exit path."""
    ready = threading.Event()

    class Server(uvicorn.Server):
        """Signal actual application/server startup rather than polling with sleeps."""

        async def startup(self, sockets=None):
            """Publish readiness after uvicorn has installed the listening socket."""
            await super().startup(sockets)
            ready.set()

    sock = socket.socket()
    sock.bind(('127.0.0.1', 0))
    server = Server(uvicorn.Config(app, log_level='error', lifespan='on'))
    thread = threading.Thread(target=server.run, kwargs={'sockets': [sock]})
    thread.start()
    try:
        assert ready.wait(20), 'HTTP server failed to start'
        with httpx.Client(base_url=f'http://127.0.0.1:{sock.getsockname()[1]}', timeout=20) as client:
            yield client
    finally:
        server.should_exit = True
        thread.join(20)
        sock.close()
        assert not thread.is_alive()


def test_real_http_remains_responsive_during_generation_and_replays_saved_data(tmp_path):
    """Holding the generator cannot block health/content reads; saved serving returns identical bytes."""
    parameters = IslandParameters()
    revision = publish_revision(tmp_path, 'demo', generate_island(parameters))
    adopt_revision(tmp_path, 'demo', revision)
    gate = multiprocessing.get_context('spawn').Event()
    app = create_app(tmp_path, gate=gate)
    tile_url = f'/v1/islands/demo/revisions/{revision}/terrain/0/0/0/0/0.bin'
    body = {'island': 'demo', 'operation_id': 'test-operation', 'parameters': parameters.model_dump()}
    with loopback(app) as client:
        response = client.post('/v1/jobs', json=body)
        assert response.status_code == 202
        job = response.json()
        assert client.get('/v1/health').status_code == 200
        assert client.get(f"/v1/jobs/{job['id']}").json()['state'] == 'running'
        assert client.get('/v1/islands/demo/manifest').json()['revision'] == revision
        response = client.get(tile_url)
        assert response.status_code == 200 and 'immutable' in response.headers['cache-control']
        tile = response.content
        decode_tile(tile)
        assert client.post('/v1/jobs', json=body).json()['id'] == job['id']
        other = dict(body, operation_id='other-operation')
        assert client.post('/v1/jobs', json=other).status_code == 409
        changed = dict(body, parameters=dict(parameters.model_dump(), plateau_height=45))
        assert client.post('/v1/jobs', json=changed).status_code == 409
        gate.set()
        process = app.state.jobs.active.process
        process.join(30)
        assert not app.state.jobs.active or not app.state.jobs.active.process.is_alive()
        status = client.get(f"/v1/jobs/{job['id']}").json()
        assert status['state'] == 'completed', status
        assert client.get('/v1/islands/demo/manifest').json()['revision'] == status['revision']
        assert client.get(tile_url).content == tile
        assert client.get('/v1/islands/missing/manifest').status_code == 404
        assert client.get(tile_url.replace('/0/0/0/0/0.bin', '/0/0/999/0/0.bin')).status_code == 422
        assert client.get('/v1/jobs/missing').status_code == 404
        assert client.post('/v1/jobs', content=b'x' * (256 * 1024 + 1)).status_code == 413
        assert client.post('/v1/jobs', json=dict(body, parameters={'jitter': 999999})).status_code == 422
    with loopback(create_app(tmp_path, read_only=True)) as client:
        assert client.get('/v1/health').json()['read_only']
        assert client.get(tile_url).content == tile
        assert client.post('/v1/jobs', json=body).status_code == 404


def test_cancel_worker_crash_restart_and_storage_backpressure(tmp_path):
    """Lifecycle failure paths retain prior content and durable idempotency without a job queue."""
    gate = multiprocessing.get_context('spawn').Event()
    manager = JobManager(tmp_path, gate=gate)
    p = IslandParameters()
    job = manager.submit('demo', 'cancel-me', p)
    assert manager.cancel(job['id'])['state'] == 'cancelling'
    gate.set()
    manager.active.process.join(30)
    assert manager.status(job['id'])['state'] == 'cancelled'
    assert not (tmp_path / 'demo/latest.json').exists()
    assert manager.submit('demo', 'cancel-me', p)['id'] == job['id']
    gate.clear()
    crashed = manager.submit('demo', 'crash-me', p)
    manager.active.process.terminate()
    manager.active.process.join(10)
    assert manager.status(crashed['id'])['state'] == 'failed'
    assert not (tmp_path / 'demo/latest.json').exists()
    interrupted = dict(crashed, id='interrupted', operation_id='interrupted', state='running')
    atomic_json(tmp_path / 'jobs/interrupted.json', interrupted)
    recovered = JobManager(tmp_path, output_limit=1)
    assert recovered.status('interrupted')['state'] == 'failed'
    assert recovered.submit('demo', 'cancel-me', p)['id'] == job['id']
    with pytest.raises(RuntimeError, match='storage budget'):
        recovered.submit('demo', 'no-space', p)
    with pytest.raises(ValueError):
        recovered.submit('jobs', 'reserved', p)
    manager.close()
    recovered.close()


def test_worker_function_failure_messages_and_shutdown(tmp_path):
    """Worker error messages are explicit and shutdown cannot leave a held process behind."""
    context = multiprocessing.get_context('spawn')
    cancelled = context.Event()
    cancelled.set()
    reader, writer = context.Pipe(duplex=False)
    generate_job(str(tmp_path), 'demo', IslandParameters().model_dump(), cancelled, writer)
    assert reader.recv()['state'] == 'cancelled'
    reader.close()
    reader, writer = context.Pipe(duplex=False)
    generate_job(str(tmp_path), 'demo', {'jitter': 999999}, context.Event(), writer)
    assert reader.recv()['state'] == 'failed'
    reader.close()
    gate = context.Event()
    manager = JobManager(tmp_path, gate=gate)
    job = manager.submit('demo', 'shutdown', IslandParameters())
    process = manager.active.process
    manager.close()
    assert not process.is_alive()
    assert manager.status(job['id'])['state'] == 'cancelled'


def test_command_line_generation(tmp_path, monkeypatch, capsys):
    """The documented offline command creates a complete revision and diagnostic."""
    from terrain_service.__main__ import main
    monkeypatch.setattr('sys.argv', ['terrain_service', 'generate', '--data-dir', str(tmp_path)])
    main()
    result = json.loads(capsys.readouterr().out)
    assert (tmp_path / 'demo/revisions' / result['revision'] / 'diagnostic.png').is_file()
    assert json.loads((tmp_path / 'demo/latest.json').read_text())['revision'] == result['revision']


def test_publication_failure_does_not_advance_latest(tmp_path, monkeypatch):
    """A failed latest-pointer update produces a failed job with the old pointer intact."""
    manager = JobManager(tmp_path)
    job = manager.submit('demo', 'publish-fail', IslandParameters())
    manager.active.process.join(30)

    def fail_adoption(*args):
        """Model a filesystem failure exactly at the publication boundary."""
        raise OSError('disk unavailable')

    monkeypatch.setattr('terrain_service.jobs.adopt_revision', fail_adoption)
    assert manager.status(job['id'])['state'] == 'failed'
    assert not (tmp_path / 'demo/latest.json').exists()
    manager.close()
