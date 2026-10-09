"""Optional FastScape Fortran landscape evolution; one process owns its complete mutable solver state."""
from dataclasses import replace
import importlib.util
import importlib.metadata
import math
import platform
import json
import sys
from pathlib import Path
import time

import numpy as np
from pydantic import BaseModel, ConfigDict, Field, model_validator

from .content import Revision, build_bounds, publish_revision, validate_revision
from .island import IslandParameters, generate_island


class ErosionParameters(BaseModel):
    """Metres/years model inputs; preview cadence is separate from numerical timestep."""
    model_config = ConfigDict(extra='forbid', allow_inf_nan=False)
    initial_revision: str
    timestep_years: float = Field(default=1000, gt=0, le=100000)
    steps: int = Field(default=100, ge=1, le=100000, strict=True)
    uplift_m_per_year: float = Field(default=.001, ge=0, le=.01)
    incision: float = Field(default=2e-5, ge=0, le=.1)
    sediment_incision: float = Field(default=4e-5, ge=0, le=.1)
    area_exponent: float = Field(default=.5, gt=0, le=1)
    slope_exponent: float = Field(default=1, ge=1, le=2)
    diffusivity: float = Field(default=.1, ge=0, le=100)
    deposition: float = Field(default=1, ge=0, le=10)
    marine_diffusivity: float = Field(default=0, ge=0, le=1000)
    precipitation: float = Field(default=1, gt=0, le=10)
    perturbation_m: float = Field(default=1, ge=0, le=10)
    preview_steps: int = Field(default=10, ge=1, le=10000, strict=True)
    preview_seconds: float = Field(default=1, ge=.1, le=60)
    preview_stride: int = Field(default=1, ge=1, le=8, strict=True)
    start_paused: bool = True

    @model_validator(mode='after')
    def validate_run(self):
        """Reject invalid revision paths and preview lattices before starting a worker."""
        validate_revision(self.initial_revision)
        if self.preview_stride & (self.preview_stride - 1):
            raise ValueError('Preview stride must be a power of two')
        return self


def available() -> bool:
    """Advertise the optional binding without importing Fortran into the HTTP process."""
    return importlib.util.find_spec('fastscapelib_fortran') is not None


class FastScapeSimulation:
    """Exclusive owner of the library's module-global context; arrays use [z,x], X varying fastest."""
    _active = False

    def __init__(self, surface, bedrock, spacing: float, sea_level: float, parameters: ErosionParameters):
        """Initialize fixed outer boundaries, terrestrial forcing and explicit marine transport."""
        if FastScapeSimulation._active:
            raise RuntimeError('A process may own only one FastScape simulation')
        import fastscapelib_fortran as solver
        self.solver, self.parameters, self.spacing, self.sea_level = solver, parameters, spacing, sea_level
        self.surface = np.array(surface, dtype='f8', order='C', copy=True)
        self.bedrock = np.array(bedrock, dtype='f8', order='C', copy=True)
        if (self.surface.ndim != 2 or min(self.surface.shape) < 3 or self.surface.shape != self.bedrock.shape
                or not np.isfinite(self.surface).all() or not np.isfinite(self.bedrock).all()
                or np.any(self.bedrock > self.surface) or not math.isfinite(spacing) or spacing <= 0):
            raise ValueError('Invalid FastScape grid, spacing or initial bedrock')
        # Marine.f90 copies each corner from its horizontal neighbour, even for fixed boundaries.
        # The island's constant seabed margin satisfies this; reject incompatible scientific fixtures.
        if any(self.surface[z,x] != self.surface[z,adjacent] for z in [0,-1]
               for x, adjacent in [(0,1),(-1,-2)]):
            raise ValueError('FastScape marine corners must equal their horizontal boundary neighbours')
        self.closed = False
        self.step = 0
        self.step_seconds = []
        self.initial = self.surface.copy()
        self.uplift = np.where(self.surface > sea_level, parameters.uplift_m_per_year, 0.)
        self.uplift[[0,-1],:] = 0
        self.uplift[:,[0,-1]] = 0
        self.area = np.zeros_like(self.surface)
        solver.fastscape_init()
        FastScapeSimulation._active = True
        try:
            nz, nx = self.surface.shape
            solver.fastscape_set_nx_ny(nx, nz)
            solver.fastscape_setup()
            solver.fastscape_set_xl_yl((nx-1)*spacing, (nz-1)*spacing)
            solver.fastscape_set_dt(parameters.timestep_years)
            solver.fastscape_init_h(self.surface.ravel())
            solver.fastscape_set_basement(self.bedrock.ravel())
            solver.fastscape_set_bc(1111)
            solver.fastscape_set_u(self.uplift.ravel())
            solver.fastscape_set_precip(np.full(nx*nz, parameters.precipitation, dtype='f8'))
            self.set_terrestrial_rates()
            # Equal silt/sand diffusivity and zero compaction avoid an uncalibrated grain-size model.
            solver.fastscape_set_marine_parameters(sea_level, 0., 0., 1000., 1000., .5, 100.,
                                                   parameters.marine_diffusivity, parameters.marine_diffusivity)
        except BaseException:
            self.close()
            raise

    def set_terrestrial_rates(self):
        """Mask terrestrial diffusion under the current sea; FastScape itself masks submerged river incision."""
        p = self.parameters
        kd = np.where(self.surface >= self.sea_level, p.diffusivity, 0.)
        kd[[0,-1],:] = 0
        kd[:,[0,-1]] = 0
        self.solver.fastscape_set_erosional_parameters(np.full(self.surface.size, p.incision, dtype='f8'),
            p.sediment_incision, p.area_exponent, p.slope_exponent, kd.ravel(), -1.,
            # -2 selects the upstream single-flow-direction solver. The v2.8.4 multiple-flow path
            # reads uninitialised lake-water scratch on flat fixtures (confirmed with Valgrind).
            p.deposition, p.deposition, -2.)

    def advance(self):
        """Execute exactly one numerical step; controls and exports happen only after this returns."""
        if self.closed:
            raise RuntimeError('FastScape simulation is closed')
        start = time.perf_counter()
        self.set_terrestrial_rates()
        self.solver.fastscape_execute_step()
        self.solver.fastscape_copy_h(self.surface.ravel())
        self.solver.fastscape_copy_basement(self.bedrock.ravel())
        self.solver.fastscape_copy_drainage_area(self.area.ravel())
        if (not np.isfinite(self.surface).all() or not np.isfinite(self.bedrock).all()
                or np.any(self.bedrock > self.surface + 1e-6) or np.max(np.abs(self.surface)) > 10000):
            raise RuntimeError('FastScape returned invalid or unsupported elevations')
        self.step = int(self.solver.fastscape_get_step())
        self.step_seconds.append(time.perf_counter() - start)

    def diagnostics(self):
        """Report model time and net volume change; this is not a closed-domain conservation claim."""
        import psutil
        import resource
        years = self.step * self.parameters.timestep_years
        return dict(step=self.step, years=years, step_seconds=sum(self.step_seconds),
                    last_step_seconds=self.step_seconds[-1] if self.step_seconds else 0,
                    rss_bytes=psutil.Process().memory_info().rss,
                    peak_rss_bytes=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024,
                    net_volume_change_m3=float(np.sum(self.surface-self.initial-self.uplift*years)*self.spacing**2),
                    sediment_volume_m3=float(np.maximum(self.surface-self.bedrock,0).sum()*self.spacing**2))

    def close(self):
        """Release the Fortran context exactly once; paused runs retain it until explicit closure."""
        if not self.closed:
            self.solver.fastscape_destroy()
            self.closed = True
            FastScapeSimulation._active = False

    def __enter__(self):
        """Keep ownership explicit during numerical tests and worker runs."""
        return self

    def __exit__(self, *_):
        """Release solver allocations even after an invalid output or cancelled publication."""
        self.close()


def run_erosion(root: Path, island: str, parameters: dict, recipe: dict, cancelled, connection, output_limit: int):
    """Own a progressive solver in the spawned worker; one publication awaits adoption before another begins."""
    from .jobs import ensure_output_space
    p = ErosionParameters.model_validate(recipe)
    source = Revision(root, island, p.initial_revision)
    params = IslandParameters.model_validate(parameters)
    raster = generate_island(params, cancelled)
    h, b = np.array(source.fields['surface'], dtype='f8'), np.array(source.fields['bedrock'], dtype='f8')
    if np.any(source.fields['validity'] & 1 == 0):
        raise ValueError('Erosion requires known initial bedrock')
    # A reproducible metre-scale perturbation breaks the flat delivery fixture's drainage symmetry.
    noise = np.random.default_rng(int(params.seed)).uniform(-p.perturbation_m, p.perturbation_m, h.shape)
    noise *= h > params.sea_level + p.perturbation_m
    noise[[0,-1],:] = 0
    noise[:,[0,-1]] = 0
    h += noise
    b += noise
    stride = p.preview_stride
    if params.source_intervals // stride < 256 or raster.origin_x % (params.source_spacing*stride) or raster.origin_z % (params.source_spacing*stride):
        raise ValueError('Preview stride needs at least 256 intervals and an aligned source origin; use stride 1')
    preview_params = params.model_copy(update={'source_spacing':params.source_spacing*stride,
                                             'source_intervals':params.source_intervals//stride})
    if preview_params.source_spacing > 512:
        raise ValueError('Preview spacing exceeds the terrain format limit')
    paused, single_step, last_publish = p.start_paused, False, -math.inf
    revision = None

    def command(message):
        """Apply only controls observed at a numerical step boundary."""
        nonlocal paused, single_step
        action = message['action']
        if action == 'cancel':
            raise InterruptedError('Erosion cancelled between numerical steps')
        if action == 'pause': paused = True
        if action == 'resume': paused, single_step = False, False
        if action == 'step': paused, single_step = False, True

    with FastScapeSimulation(h, b, params.source_spacing, params.sea_level, p) as simulation:
        def status(state):
            """Return small progress values; full arrays never cross the process pipe."""
            return dict(event='progress', state=state, phase=state, progress=simulation.step/p.steps,
                        revision=revision, **simulation.diagnostics())

        def publish():
            """Export completed state and wait for parent adoption, bounding preview backpressure to one."""
            nonlocal revision, last_publish
            ensure_output_space(root, output_limit, (preview_params.source_intervals+1)**2*40+(4<<20))
            start = time.perf_counter()
            surface = simulation.surface[::stride,::stride].astype('<f4')
            bedrock = np.minimum(simulation.bedrock[::stride,::stride].astype('<f4'), surface)
            wet = surface < params.sea_level
            water = np.where(wet, params.sea_level-surface, 0).astype('<f4')
            # Land water is unknown: routed discharge/area is not water depth. Sea depth remains explicit/static.
            validity = np.where(wet, 7, 5).astype('u1')
            material = np.where(surface-bedrock > .001, 2, 1).astype('<u2')
            snapshot = replace(raster, params=preview_params, heights=surface)
            metadata = dict(initial_revision=p.initial_revision, recipe=p.model_dump(), **simulation.diagnostics(),
                solver='fastscapelib-fortran', solver_version=importlib.metadata.version('fastscapelib-fortran'),
                flow_routing='single direction',
                numpy_version=np.__version__, simulation_spacing=params.source_spacing,
                python_version=platform.python_version(),
                build=json.loads((Path(sys.prefix)/'share/ofg-fastscape-build.json').read_text())
                    if (Path(sys.prefix)/'share/ofg-fastscape-build.json').exists() else {'provenance':'unrecorded'},
                preview_stride=stride, water_convention='static sea; terrestrial water unknown')
            revision = publish_revision(root, island, snapshot, cancelled,
                channels=dict(surface=surface,bedrock=bedrock,water=water,material=material,validity=validity),
                simulation=metadata, drainage=simulation.area[::stride,::stride],
                source_bounds=build_bounds(simulation.surface.astype('<f4'))[int(math.log2(stride)):])
            update = status('paused' if paused else 'running')
            update.update(publication=True, export_seconds=time.perf_counter()-start)
            connection.send(update)
            while True:
                message = connection.recv()
                if cancelled.is_set(): raise InterruptedError('Erosion publication cancelled')
                if message['action'] == 'publication_adopted': break
                command(message)
            last_publish = time.monotonic()

        publish()
        while simulation.step < p.steps:
            if cancelled.is_set(): raise InterruptedError('Erosion cancelled between numerical steps')
            while connection.poll(): command(connection.recv())
            if paused:
                connection.send(status('paused'))
                command(connection.recv())
                continue
            simulation.advance()
            if single_step:
                paused, single_step = True, False
                publish()
            elif simulation.step == p.steps or (simulation.step % p.preview_steps == 0 and
                                                time.monotonic()-last_publish >= p.preview_seconds):
                publish()
        connection.send(dict(state='completed', revision=revision, **simulation.diagnostics()))
