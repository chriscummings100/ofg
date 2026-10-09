"""Reproducible CPU erosion comparisons and separated solver/export costs; run in the pinned Linux environment."""
import argparse
from dataclasses import replace
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image

from terrain_service.content import publish_revision
from terrain_service.erosion import ErosionParameters, FastScapeSimulation
from terrain_service.island import IslandParameters, generate_island


def main():
    """Save fixed-seed comparisons and measured grid scaling without extrapolating to a full-size island."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=Path('artifacts/terrain-service/stage-4/experiments'))
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--timestep', type=float, default=100)
    parser.add_argument('--steps', type=int, default=100)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    reports, results = [], {}
    cases = [(str(n), n, {}) for n in [256,512,1024,2048]] if args.benchmark else [
        ('baseline',256,{}), ('no-uplift',256,{'uplift_m_per_year':0}),
        ('wet',256,{'precipitation':2}), ('no-fluvial-deposition',256,{'deposition':0}),
        ('half-step',256,{'timestep_years':args.timestep/2, 'steps':args.steps*2}),
        ('quarter-step',256,{'timestep_years':args.timestep/4, 'steps':args.steps*4})]
    for label, n, changes in cases:
        start = time.perf_counter()
        params = IslandParameters(source_intervals=n, source_spacing=32)
        raster = generate_island(params)
        h = raster.heights.astype('f8')
        h += np.random.default_rng(1).uniform(-1,1,h.shape)*(h>1)
        p = ErosionParameters(initial_revision='1'*32, steps=5 if args.benchmark else args.steps,
                              timestep_years=args.timestep)
        p = p.model_copy(update=changes)
        with FastScapeSimulation(h, h, 32, 0, p) as sim:
            initialized = time.perf_counter()
            for _ in range(p.steps): sim.advance()
            diagnostics = sim.diagnostics()
            final = sim.surface.copy()
            results[label] = final
            dz, dx = np.gradient(final, 32)
            diagnostics.update(label=label, samples=n+1, recipe=p.model_dump(),
                               setup_seconds=initialized-start, total_before_export=time.perf_counter()-start,
                               mean_land_slope=float(np.hypot(dx,dz)[final>0].mean()),
                               maximum_drainage=float(sim.area.max()),
                               coastline_samples=int(np.count_nonzero((final[:-1,:]>0)!=(final[1:,:]>0))))
            # Separate export from numerical work; the complete publication is retained for independent viewing.
            export_start = time.perf_counter()
            surface = final.astype('<f4')
            bedrock = np.minimum(sim.bedrock.astype('<f4'), surface)
            channels = dict(surface=surface, bedrock=bedrock, water=np.maximum(-surface,0).astype('<f4'),
                            material=np.where(surface-bedrock>.001,2,1).astype('<u2'),
                            validity=np.where(surface<0,7,5).astype('u1'))
            revision = publish_revision(args.output/'content', label, replace(raster, heights=surface),
                                        channels=channels, simulation=diagnostics, drainage=sim.area)
            diagnostics.update(export_seconds=time.perf_counter()-export_start, revision=revision,
                               peak_rss_bytes=sim.diagnostics()['peak_rss_bytes'])
            np.savez_compressed(args.output/f'{label}.npz', initial=h, surface=final,
                                bedrock=sim.bedrock, drainage=sim.area)
            hill = np.clip(.55 + .45*(-dx-dz)/np.maximum(np.hypot(dx,dz), .1),0,1)
            channels = np.clip(np.log1p(sim.area/1024)/12,0,1)
            rgb = np.stack([hill*.6, hill*.7, hill*.45+channels*.35],axis=-1)
            rgb[final<0] = [.08,.2,.35]
            Image.fromarray((rgb*255).astype('u1')).resize((768,768)).save(args.output/f'{label}.png')
            reports.append(diagnostics)
            (args.output/'report.json').write_text(json.dumps(reports,indent=2))
            print(json.dumps(diagnostics), flush=True)
    if not args.benchmark:
        coarse = np.sqrt(np.mean((results['baseline']-results['half-step'])**2))
        fine = np.sqrt(np.mean((results['half-step']-results['quarter-step'])**2))
        comparison = dict(coarse_rms_m=float(coarse), fine_rms_m=float(fine),
                          initial_relief_m=float(np.ptp(h)))
        (args.output/'convergence.json').write_text(json.dumps(comparison,indent=2))
        assert fine < coarse and fine < .01*np.ptp(h), comparison


if __name__ == '__main__':
    main()
