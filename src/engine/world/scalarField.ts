import { vec3, type Vec3 } from "../math/vec3.js";
import type { TerrainDensitySource } from "./terrainChunk.js";

export type TerrainField = TerrainDensitySource & {
  readonly heightAt: (x: number, z: number) => number;
  readonly normalAt: (x: number, z: number) => Vec3;
};

export function createSeedTerrainField(): TerrainField {
  return {
    heightAt,
    densityAt(position) {
      return position.y - heightAt(position.x, position.z);
    },
    normalAt(x, z) {
      const sampleDistance = 0.25;
      const left = heightAt(x - sampleDistance, z);
      const right = heightAt(x + sampleDistance, z);
      const back = heightAt(x, z - sampleDistance);
      const front = heightAt(x, z + sampleDistance);

      const dx = (right - left) / (sampleDistance * 2);
      const dz = (front - back) / (sampleDistance * 2);
      const invLength = 1 / Math.hypot(dx, 1, dz);

      return vec3(-dx * invLength, invLength, -dz * invLength);
    }
  };
}

function heightAt(x: number, z: number): number {
  const broadHill = Math.sin(x * 0.16) * Math.cos(z * 0.13) * 2.2;
  const ridge = Math.sin((x + z) * 0.055) * 1.3;
  const bowl = -0.0025 * (x * x + z * z);

  return broadHill + ridge + bowl;
}
