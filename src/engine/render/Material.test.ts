import { equal, throws } from "node:assert/strict";
import { vec3 } from "../math/vec3.js";
import { vec4 } from "../math/vec4.js";
import { MATERIAL_FLAG_TRIPLANAR_ALBEDO, Material } from "./Material.js";

describe("Material", () => {
  it("stores basic material properties", () => {
    const albedoFactor = vec4(0.1, 0.2, 0.3, 1);
    const specular = vec3(0.8, 0.7, 0.6);
    const material = new Material("material:test", {
      albedoFactor,
      albedoTexture: "texture:albedo",
      normalTexture: "texture:normal",
      materialTexture: "texture:material",
      specular,
      specularFactor: 0.42,
      flags: MATERIAL_FLAG_TRIPLANAR_ALBEDO,
      textureScale: 0.125
    });

    equal(material.id, "material:test");
    equal(material.albedoFactor, albedoFactor);
    equal(material.albedoTexture, "texture:albedo");
    equal(material.normalTexture, "texture:normal");
    equal(material.materialTexture, "texture:material");
    equal(material.specular, specular);
    equal(material.specularFactor, 0.42);
    equal(material.flags, MATERIAL_FLAG_TRIPLANAR_ALBEDO);
    equal(material.textureScale, 0.125);
  });

  it("uses useful lighting defaults", () => {
    const material = new Material("material:test");

    equal(material.albedoFactor.x, 1);
    equal(material.albedoFactor.y, 1);
    equal(material.albedoFactor.z, 1);
    equal(material.albedoFactor.w, 1);
    equal(material.albedoTexture, undefined);
    equal(material.normalTexture, undefined);
    equal(material.materialTexture, undefined);
    equal(material.specular.x, 1);
    equal(material.specular.y, 1);
    equal(material.specular.z, 1);
    equal(material.specularFactor, 0.18);
    equal(material.flags, 0);
    equal(material.textureScale, 1);
  });

  it("allows albedo texture assignment to change", () => {
    const material = new Material("material:test", { albedoTexture: "texture:first" });

    material.albedoTexture = "texture:second";

    equal(material.albedoTexture, "texture:second");
  });

  it("rejects invalid texture scales", () => {
    throws(() => new Material("material:test", { textureScale: 0 }), /textureScale/);
  });
});
