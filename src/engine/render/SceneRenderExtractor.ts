import { getScene } from "../scene/activeScene.js";
import { add, vec3, VEC3_UP } from "../math/vec3.js";
import { rotateVec3ByQuat } from "../math/quat.js";
import { inverseMat4, lookAtMat4, multiplyMat4, perspectiveMat4 } from "../math/mat4.js";
import { MeshRenderer } from "./MeshRenderer.js";
import { TerrainRenderer } from "./TerrainRenderer.js";
import type { CameraFrame } from "../camera/cameraRig.js";
import type { Entity } from "../scene/Entity.js";
import type { RenderItem, RenderWorld } from "./RenderWorld.js";

export class SceneRenderExtractor {
  static buildRenderWorld(aspect = 1): RenderWorld {
    const scene = getScene();
    const activeCamera = scene.activeCamera;

    if (activeCamera === undefined) {
      throw new Error("SceneRenderExtractor requires scene.activeCamera to be set.");
    }

    const items: RenderItem[] = [];
    for (const renderer of scene.queryComponents(MeshRenderer)) {
      const item = renderer.getRenderItem();
      if (item !== undefined) {
        items.push(item);
      }
    }

    for (const renderer of scene.queryComponents(TerrainRenderer)) {
      items.push(...renderer.getRenderItems());
    }

    return {
      camera: buildCameraFrame(activeCamera, aspect),
      mainLight: scene.mainLight,
      items
    };
  }
}

function buildCameraFrame(cameraEntity: Entity, aspect: number): CameraFrame {
  const eye = cameraEntity.transform.getWorldPosition();
  const forward = rotateVec3ByQuat(vec3(0, 0, 1), cameraEntity.transform.rotation);
  const target = add(eye, forward);
  const projection = perspectiveMat4((70 * Math.PI) / 180, aspect, 0.05, 500);
  const view = lookAtMat4(eye, target, VEC3_UP);
  const viewProjection = multiplyMat4(projection, view);

  return {
    eye,
    target,
    viewProjection,
    inverseViewProjection: inverseMat4(viewProjection)
  };
}
