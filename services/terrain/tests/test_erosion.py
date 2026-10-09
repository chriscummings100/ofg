"""Real optional Fortran solver and spawned progressive-control tests, independent of graphics."""
import json
import time

import numpy as np
import pytest

pytest.importorskip('fastscapelib_fortran')
from terrain_service.content import Revision, adopt_revision, publish_revision
from terrain_service.erosion import ErosionParameters, FastScapeSimulation
from terrain_service.island import IslandParameters, generate_island
from terrain_service.jobs import JobManager
from terrain_service.protocol import decode_tile, encode_tile


def recipe(**values):
    """Supply a syntactically valid initial identity for direct numerical fixtures."""
    return ErosionParameters(initial_revision='1'*32, **values)


def test_zero_forcing_and_fixed_boundaries():
    """A sloped terrestrial grid with every evolution term disabled stays unchanged."""
    h = np.tile(np.linspace(100, 0, 33), (33, 1))
    h[[0,-1],0], h[[0,-1],-1] = h[[0,-1],1], h[[0,-1],-2]
    p = recipe(uplift_m_per_year=0, incision=0, sediment_incision=0, diffusivity=0,
               deposition=0, marine_diffusivity=0)
    with FastScapeSimulation(h, h, 32, -100, p) as sim:
        for _ in range(3): sim.advance()
        np.testing.assert_allclose(sim.surface, h, atol=1e-9, rtol=0)
        assert sim.step == 3
        assert np.isfinite(sim.area).all() and sim.area.max() > 32**2
        with pytest.raises(RuntimeError, match='only one'):
            FastScapeSimulation(h, h, 32, -100, p)


def test_downhill_incision_boundaries_and_timestep_refinement():
    """Routed incision lowers a slope, preserves fixed edges and converges under timestep halving."""
    x, z = np.meshgrid(np.arange(65), np.arange(65))
    h = 400 - 4*x + .01*(z-32)**2
    h[[0,-1],0], h[[0,-1],-1] = h[[0,-1],1], h[[0,-1],-2]
    results = []
    for dt in [500, 250]:
        p = recipe(timestep_years=dt, uplift_m_per_year=0, diffusivity=.1,
                   deposition=0, marine_diffusivity=0)
        with FastScapeSimulation(h, h, 32, -100, p) as sim:
            for _ in range(int(5000/dt)): sim.advance()
            assert sim.surface[32,32] < h[32,32]
            np.testing.assert_allclose(sim.surface[[0,-1],:], h[[0,-1],:], atol=1e-9, rtol=0)
            np.testing.assert_allclose(sim.surface[:,[0,-1]], h[:,[0,-1]], atol=1e-9, rtol=0)
            assert np.all(sim.bedrock <= sim.surface+1e-9)
            catchments = np.zeros(h.size, dtype='f8')
            sim.solver.fastscape_copy_catchment(catchments)
            catchments = catchments.reshape(h.shape)
            outlets = np.concatenate([catchments[0], catchments[-1], catchments[:,0], catchments[:,-1]])
            assert np.isin(catchments, outlets).all()
            results.append(sim.surface.copy())
    assert np.sqrt(np.mean((results[0]-results[1])**2)) < .01*np.ptp(h)


def test_unforced_island_and_marine_floor():
    """Zero continental/marine rates leave the actual island and submerged source boundary unchanged."""
    h = generate_island(IslandParameters(source_intervals=256)).heights.astype('f8')
    p = recipe(uplift_m_per_year=0, incision=0, sediment_incision=0, diffusivity=0,
               deposition=0, marine_diffusivity=0)
    with FastScapeSimulation(h, h, 32, 0, p) as sim:
        for _ in range(5): sim.advance()
        np.testing.assert_allclose(sim.surface, h, atol=1e-9, rtol=0)


def test_paused_exports_do_not_change_numerical_state(tmp_path):
    """Production's fresh worker per experiment makes paused and uninterrupted final exports identical."""
    params = IslandParameters(source_intervals=256)
    initial = publish_revision(tmp_path, 'demo', generate_island(params))
    results = []
    manager = JobManager(tmp_path)
    try:
        for paused in [False, True]:
            p = ErosionParameters(initial_revision=initial, steps=3, start_paused=paused,
                                  preview_steps=1 if paused else 3, preview_seconds=.1)
            job = manager.submit('demo', str(paused), params, p)
            if paused:
                for step in range(3):
                    wait_status(manager, job['id'], lambda s:s['state']=='paused' and s.get('step')==step)
                    manager.control(job['id'], 'step')
            final = wait_status(manager, job['id'], lambda s:s['state']=='completed')
            revision = Revision(tmp_path, 'demo', final['revision'])
            results.append([np.array(revision.fields[k]) for k in ['surface','bedrock']])
    finally:
        manager.close()
    for left, right in zip(*results):
        np.testing.assert_array_equal(left, right)


def test_preview_budget_failure_and_invalid_controls_preserve_latest(tmp_path):
    """A preview cannot grow past the disk budget; invalid controls never start a replacement run."""
    params = IslandParameters(source_intervals=256)
    initial = publish_revision(tmp_path, 'demo', generate_island(params))
    adopt_revision(tmp_path, 'demo', initial)
    used = sum(path.stat().st_size for path in tmp_path.rglob('*') if path.is_file())
    manager = JobManager(tmp_path, output_limit=used+257**2*40+(5 << 20))
    try:
        p = ErosionParameters(initial_revision=initial, start_paused=True)
        job = manager.submit('demo', 'budget', params, p)
        # The submission estimate fits, but a second conservative preview reservation will not.
        deadline = time.monotonic()+30
        while time.monotonic()<deadline:
            status = manager.status(job['id'])
            if status['state']=='failed': break
            if status['state']=='paused': manager.control(job['id'],'step')
            if manager.active: manager.active.result.poll(.1)
        assert status['state']=='failed' and 'storage budget' in status['error'], status
        latest = json.loads((tmp_path/'demo/latest.json').read_text())['revision']
        assert (tmp_path/'demo/revisions'/latest/'manifest.json').exists()
        with pytest.raises(RuntimeError, match='active erosion'):
            manager.control(job['id'],'resume')
        with pytest.raises(ValueError):
            ErosionParameters(initial_revision=initial, preview_stride=3)
        with pytest.raises(ValueError, match='match'):
            manager.submit('demo','wrong-source',params.model_copy(update={'plateau_height':41}),p)
    finally:
        manager.close()


def wait_status(manager, identifier, predicate):
    """Wait for IPC readiness rather than sleeping; fail with the last useful job status."""
    deadline = time.monotonic()+40
    while time.monotonic() < deadline:
        status = manager.status(identifier)
        if predicate(status): return status
        assert status['state'] not in {'failed','cancelled'}, status
        if manager.active: manager.active.result.poll(.1)
    pytest.fail(str(status))


def test_progressive_step_pause_resume_cancel_and_saved_preview(tmp_path):
    """Each paused step publishes once, retained solver state resumes, and cancellation keeps latest."""
    params = IslandParameters(source_intervals=256, source_spacing=32)
    initial = publish_revision(tmp_path, 'demo', generate_island(params))
    adopt_revision(tmp_path, 'demo', initial)
    manager = JobManager(tmp_path)
    p = recipe(steps=100, preview_steps=1, preview_seconds=.1)
    p.initial_revision = initial
    try:
        job = manager.submit('demo', 'erosion', params, p)
        paused = wait_status(manager, job['id'], lambda s:s['state']=='paused' and s.get('revision'))
        assert paused['step'] == 0
        assert manager.submit('demo', 'erosion', params, p)['id'] == job['id']
        for step in [1,2]:
            manager.control(job['id'], 'step')
            paused = wait_status(manager, job['id'], lambda s:s['state']=='paused' and s.get('step')==step)
            revision = Revision(tmp_path, 'demo', paused['revision'])
            assert revision.manifest['simulation']['step'] == step
            _, fields = decode_tile(encode_tile(revision, 0,0,4,0,0))
            assert np.isfinite(fields[0]).all()
            assert (revision.fields['validity'] == 5).any()
            assert np.all(revision.fields['water'][revision.fields['validity'] & 2 == 0] == 0)
        manager.control(job['id'], 'resume')
        manager.control(job['id'], 'pause')
        paused = wait_status(manager, job['id'], lambda s:s['state']=='paused')
        latest = json.loads((tmp_path/'demo/latest.json').read_text())
        manager.cancel(job['id'])
        status = wait_status(manager, job['id'], lambda s:s['state']=='cancelled')
        assert status['state'] == 'cancelled'
        assert json.loads((tmp_path/'demo/latest.json').read_text()) == latest
    finally:
        manager.close()


def test_shutdown_drains_worker_with_uncollected_publication(tmp_path):
    """Closing with a pending preview releases the process and pipe without publishing it."""
    params = IslandParameters(source_intervals=256)
    initial = publish_revision(tmp_path, 'demo', generate_island(params))
    adopt_revision(tmp_path, 'demo', initial)
    manager = JobManager(tmp_path)
    job = manager.submit('demo', 'shutdown', params, ErosionParameters(initial_revision=initial))
    assert manager.active.result.poll(30)
    endpoint = manager.active.result
    manager.close()
    assert manager.active is None
    assert endpoint.closed
    assert manager.status(job['id'])['state'] == 'cancelled'
    assert json.loads((tmp_path/'demo/latest.json').read_text())['revision'] == initial
    manager.close()
