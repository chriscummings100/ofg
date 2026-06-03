import { equal, ok } from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import {
  TERRAIN_TEXTURE_ARRAY_LAYER_COUNT
} from "./terrainTextures.js";
import {
  TERRAIN_MATERIAL_LAYER_COUNT,
  TERRAIN_MATERIALS
} from "../world/terrainMaterials.js";

describe("terrainTextures", () => {
  it("defines texture arrays for the terrain material library", () => {
    equal(TERRAIN_TEXTURE_ARRAY_LAYER_COUNT, 16);
    equal(TERRAIN_TEXTURE_ARRAY_LAYER_COUNT, TERRAIN_MATERIAL_LAYER_COUNT);
  });

  it("keeps the Poly Haven material manifest aligned with the runtime material list", () => {
    const manifest = JSON.parse(readFileSync("assets/textures/polyhaven/manifest.json", "utf8")) as {
      readonly source: string;
      readonly license: string;
      readonly materials: readonly { readonly id: string; readonly slug: string }[];
    };

    equal(manifest.source, "Poly Haven");
    equal(manifest.license, "CC0");
    equal(manifest.materials.length, TERRAIN_MATERIALS.length);
    equal(manifest.materials.map((material) => material.id).join("|"), TERRAIN_MATERIALS.map((material) => material.id).join("|"));
    ok(manifest.materials.every((material) => material.slug.length > 0));
  });

  it("points every terrain material layer at checked-in 1k texture maps", () => {
    for (const material of TERRAIN_MATERIALS) {
      assertJpegTexture(material.albedoUrl);
      assertJpegTexture(material.normalUrl);
      assertJpegTexture(material.roughnessUrl);
    }
  });
});

function assertJpegTexture(url: string): void {
  const path = `.${url}`;
  ok(existsSync(path), `${path} should exist`);
  const bytes = readFileSync(path);
  const size = readJpegSize(bytes);

  equal(size.width, 1024, `${path} width`);
  equal(size.height, 1024, `${path} height`);
}

function readJpegSize(bytes: Uint8Array): { readonly width: number; readonly height: number } {
  if (bytes[0] !== 0xff || bytes[1] !== 0xd8) {
    throw new Error("Expected JPEG SOI marker.");
  }

  let offset = 2;
  while (offset < bytes.length) {
    if (bytes[offset] !== 0xff) {
      offset += 1;
      continue;
    }

    const marker = bytes[offset + 1];
    const length = bytes[offset + 2] * 256 + bytes[offset + 3];
    if (marker >= 0xc0 && marker <= 0xc3) {
      return {
        height: bytes[offset + 5] * 256 + bytes[offset + 6],
        width: bytes[offset + 7] * 256 + bytes[offset + 8]
      };
    }

    offset += 2 + length;
  }

  throw new Error("JPEG dimensions were not found.");
}
