import { Component } from "../scene/Component.js";
import { getScene } from "../scene/activeScene.js";
import type { ResourceId } from "../scene/types.js";
import type { RenderItem } from "./RenderWorld.js";

export class MeshRenderer extends Component {
  mesh: ResourceId;
  material?: ResourceId;
  visible = true;

  constructor(mesh: ResourceId, material?: ResourceId) {
    super();
    this.mesh = mesh;
    this.material = material;
  }

  getRenderItem(): RenderItem | undefined {
    if (!this.enabled || !this.visible || this.entity === undefined) {
      return undefined;
    }

    const resources = getScene().resources;
    const material = this.material === undefined ? undefined : resources.getMaterial(this.material);
    return {
      id: `mesh-renderer:${this.entity.id}`,
      mesh: resources.getMesh(this.mesh),
      material,
      albedoTexture: material?.albedoTexture === undefined
        ? undefined
        : resources.getTexture(material.albedoTexture),
      normalTexture: material?.normalTexture === undefined
        ? undefined
        : resources.getTexture(material.normalTexture),
      materialTexture: material?.materialTexture === undefined
        ? undefined
        : resources.getTexture(material.materialTexture),
      worldMatrix: this.entity.transform.getWorldMatrix()
    };
  }
}
