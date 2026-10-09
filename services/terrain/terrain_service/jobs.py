"""One spawned generator at a time, durable operation identities and atomic publication adoption."""

from dataclasses import dataclass
import json
import multiprocessing
from pathlib import Path
import shutil
import threading
import uuid

from .content import Revision, adopt_revision, atomic_json, publish_revision, validate_name
from .erosion import ErosionParameters, available, run_erosion
from .island import IslandParameters, generate_island


def ensure_output_space(root: Path, limit: int, required: int) -> None:
    """Check each publication against the output budget and physical free space."""
    used = sum(path.stat().st_size for path in root.rglob('*') if path.is_file())
    if used + required > limit or shutil.disk_usage(root).free < required:
        raise RuntimeError('Terrain output storage budget exhausted; clean up while idle')


def generate_job(root: str, island: str, parameters: dict, cancelled, result, gate=None,
                 erosion=None, output_limit=4 << 30) -> None:
    """Own generation in a child process; return small status messages rather than terrain arrays."""
    try:
        if gate is not None:
            gate.wait()  # Explicit integration-test gate, never a production delay.
        if erosion is not None:
            run_erosion(Path(root), island, parameters, erosion, cancelled, result, output_limit)
            return
        raster = generate_island(IslandParameters.model_validate(parameters), cancelled)
        revision = publish_revision(Path(root), island, raster, cancelled)
        result.send({'state': 'completed', 'revision': revision})
    except InterruptedError as error:
        result.send({'state': 'cancelled', 'error': str(error)})
    except Exception as error:
        result.send({'state': 'failed', 'error': f'{type(error).__name__}: {error}'})
    finally:
        result.close()


@dataclass
class ActiveJob:
    """Parent-owned process controls and small IPC endpoint for the sole mutable simulation."""

    identifier: str
    process: object
    cancelled: object
    result: object


class JobManager:
    """Serialize job transitions; content reads never require this lock or the generator process."""

    def __init__(self, root: Path, output_limit: int = 4 << 30, gate=None):
        """Recover durable job identities; interrupted runs fail without changing published content."""
        self.root, self.output_limit, self.gate = root, output_limit, gate
        self.directory = root / 'jobs'
        self.directory.mkdir(parents=True, exist_ok=True)
        self.context = multiprocessing.get_context('spawn')
        self.lock = threading.RLock()
        self.active = None
        self.jobs = {}
        self.operations = {}
        for path in self.directory.glob('*.json'):
            job = json.loads(path.read_text(encoding='utf-8'))
            if job['state'] in {'queued', 'running', 'paused', 'pausing', 'cancelling'}:
                job.update(state='failed', error='Service restarted before the job completed')
                atomic_json(path, job)
            self.jobs[job['id']] = job
            self.operations[job['operation_id']] = job['id']

    def save(self, job: dict) -> dict:
        """Persist a transition before returning its detached public status."""
        atomic_json(self.directory / f"{job['id']}.json", job)
        return dict(job)

    def refresh(self) -> None:
        """Collect a bounded status message; acknowledge publications before the worker can export again."""
        with self.lock:
            active = self.active
            if active is None:
                return
            outcome = None
            if active.result.poll():
                try:
                    outcome = active.result.recv()
                except EOFError:
                    outcome = {'state': 'failed', 'error': 'Generator closed its result channel'}
            elif not active.process.is_alive():
                outcome = {'state': 'failed', 'error': f'Generator exited with code {active.process.exitcode}'}
            if outcome is None:
                return
            job = self.jobs[active.identifier]
            if outcome.get('event') == 'progress':
                if active.cancelled.is_set():
                    return
                if outcome.pop('publication', False):
                    try:
                        adopt_revision(self.root, job['island'], outcome['revision'])
                    except Exception as error:
                        job.update(state='cancelling', error=f'Publication failed: {error}')
                        active.cancelled.set()
                        active.result.send({'action': 'cancel'})
                        self.save(job)
                        return
                    active.result.send({'action': 'publication_adopted'})
                outcome.pop('event')
                job.update(outcome)
                self.save(job)
                return
            if active.cancelled.is_set():
                outcome = {'state': 'cancelled'}
            if outcome['state'] == 'completed':
                try:
                    adopt_revision(self.root, job['island'], outcome['revision'])
                except Exception as error:
                    outcome = {'state': 'failed', 'error': f'Publication failed: {error}'}
            job.update(outcome, phase=outcome['state'], progress=1 if outcome['state'] == 'completed' else 0)
            self.save(job)
            active.result.close()
            active.process.join(timeout=5)
            if active.process.is_alive():
                active.process.terminate()
                active.process.join()
            self.active = None

    def submit(self, island: str, operation_id: str, params: IslandParameters,
               erosion: ErosionParameters | None = None) -> dict:
        """Start one job or return the original identical operation, refusing conflicting work."""
        with self.lock:
            self.refresh()
            validate_name(island)
            validate_name(operation_id)
            if island == 'jobs':
                raise ValueError('Island name is reserved')
            parameters = params.model_dump()
            recipe = erosion.model_dump() if erosion else None
            if operation_id in self.operations:
                old = self.jobs[self.operations[operation_id]]
                if old['island'] != island or old['parameters'] != parameters or old.get('erosion') != recipe:
                    raise RuntimeError('Operation ID already belongs to different input')
                return dict(old)
            if self.active is not None:
                raise RuntimeError('A generation job is already active')
            if erosion:
                if not available():
                    raise RuntimeError('FastScape is unavailable; use the documented erosion service environment')
                initial = Revision(self.root, island, erosion.initial_revision)
                if initial.manifest['parameters'] != parameters:
                    raise ValueError('Erosion parameters must match the selected initial revision')
            # Reserve a conservative upper bound including rasters, min/max hierarchy and diagnostics.
            required = (params.source_intervals + 1) ** 2 * 32 + (4 << 20)
            ensure_output_space(self.root, self.output_limit, required)
            identifier = uuid.uuid4().hex
            job = {'id': identifier, 'operation_id': operation_id, 'island': island, 'parameters': parameters,
                   'erosion': recipe, 'state': 'queued', 'phase': 'queued', 'progress': 0,
                   'revision': None, 'error': None}
            self.jobs[identifier], self.operations[operation_id] = job, identifier
            self.save(job)
            reader, writer = self.context.Pipe(duplex=True)
            cancelled = self.context.Event()
            process = self.context.Process(target=generate_job, args=(str(self.root), island, parameters,
                                                                     cancelled, writer, self.gate, recipe,
                                                                     self.output_limit))
            try:
                process.start()
            except Exception as error:
                reader.close()
                writer.close()
                job.update(state='failed', phase='failed', error=f'Cannot start generator: {error}')
                return self.save(job)
            writer.close()
            self.active = ActiveJob(identifier, process, cancelled, reader)
            job.update(state='running', phase='generating')
            return self.save(job)

    def status(self, identifier: str) -> dict:
        """Return a detached current status, collecting finished work before reading it."""
        with self.lock:
            self.refresh()
            return dict(self.jobs[identifier])

    def cancel(self, identifier: str) -> dict:
        """Request cooperative cancellation; completed immutable revisions are never rolled back."""
        with self.lock:
            self.refresh()
            job = self.jobs[identifier]
            if self.active is not None and self.active.identifier == identifier:
                self.active.cancelled.set()
                if job.get('erosion'):
                    self.active.result.send({'action': 'cancel'})
                job.update(state='cancelling', phase='cancelling')
                self.save(job)
            return dict(job)

    def control(self, identifier: str, action: str) -> dict:
        """Deliver pause/resume/single-step at the next solver boundary; status is the acknowledgement."""
        with self.lock:
            self.refresh()
            job = self.jobs[identifier]
            if (action not in {'pause', 'resume', 'step'} or not job.get('erosion') or
                    self.active is None or self.active.identifier != identifier or
                    job['state'] == 'cancelling'):
                raise RuntimeError('Control requires an active erosion job')
            if action == 'step' and job['state'] != 'paused':
                raise RuntimeError('Single step requires an acknowledged paused simulation')
            self.active.result.send({'action': action})
            job.update(state='pausing' if action == 'pause' else 'running', phase=action+' requested')
            return self.save(job)

    def close(self) -> None:
        """Stop only the owned worker, retaining completed data and recording interrupted work."""
        with self.lock:
            if self.active is not None:
                self.active.cancelled.set()
                if self.jobs[self.active.identifier].get('erosion'):
                    try:
                        self.active.result.send({'action': 'cancel'})
                    except (BrokenPipeError, EOFError, OSError):
                        pass  # A crashed worker has no receiver; shutdown still owns its handles.
                if self.gate is not None:
                    self.gate.set()
                self.active.process.join(timeout=5)
                if self.active.process.is_alive():
                    self.active.process.terminate()
                    self.active.process.join()
                # Progress can remain queued ahead of the terminal reply. Shutdown deliberately
                # discards it rather than adopting another preview or leaving the endpoint owned.
                job = self.jobs[self.active.identifier]
                job.update(state='cancelled', phase='cancelled', progress=0)
                self.save(job)
                self.active.result.close()
                self.active.process.close()
                self.active = None

