import { add, vec3, VEC3_ZERO, type Vec3 } from "../math/vec3.js";
import { QUAT_IDENTITY, type Quat } from "../math/quat.js";
import { identityMat4, multiplyMat4, type Mat4 } from "../math/mat4.js";

export class Transform {
  position: Vec3 = VEC3_ZERO;
  rotation: Quat = QUAT_IDENTITY;
  scale: Vec3 = vec3(1, 1, 1);

  private parent: Transform | undefined;
  private readonly children = new Set<Transform>();
  private localMatrix: Mat4 = identityMat4();
  private worldMatrix: Mat4 = identityMat4();
  private localDirty = true;
  private worldDirty = true;

  setParent(parent: Transform | undefined): void {
    if (this.parent === parent) {
      return;
    }

    this.parent?.children.delete(this);
    this.parent = parent;
    this.parent?.children.add(this);
    this.markWorldDirty();
  }

  getLocalMatrix(): Mat4 {
    if (this.localDirty) {
      this.localMatrix = buildTransformMatrix(this.position, this.rotation, this.scale);
      this.localDirty = false;
    }

    return this.localMatrix;
  }

  getWorldMatrix(): Mat4 {
    if (this.worldDirty) {
      this.worldMatrix = this.parent === undefined
        ? this.getLocalMatrix()
        : multiplyMat4(this.parent.getWorldMatrix(), this.getLocalMatrix());
      this.worldDirty = false;
    }

    return this.worldMatrix;
  }

  getWorldPosition(): Vec3 {
    const matrix = this.getWorldMatrix();
    return vec3(matrix[12], matrix[13], matrix[14]);
  }

  setPosition(position: Vec3): void {
    this.position = position;
    this.markDirty();
  }

  translate(delta: Vec3): void {
    this.position = add(this.position, delta);
    this.markDirty();
  }

  setRotation(rotation: Quat): void {
    this.rotation = rotation;
    this.markDirty();
  }

  setScale(scale: Vec3): void {
    this.scale = scale;
    this.markDirty();
  }

  markDirty(): void {
    this.localDirty = true;
    this.markWorldDirty();
  }

  private markWorldDirty(): void {
    this.worldDirty = true;
    for (const child of this.children) {
      child.markWorldDirty();
    }
  }
}

function buildTransformMatrix(position: Vec3, rotation: Quat, scale: Vec3): Mat4 {
  const x2 = rotation.x + rotation.x;
  const y2 = rotation.y + rotation.y;
  const z2 = rotation.z + rotation.z;
  const xx = rotation.x * x2;
  const xy = rotation.x * y2;
  const xz = rotation.x * z2;
  const yy = rotation.y * y2;
  const yz = rotation.y * z2;
  const zz = rotation.z * z2;
  const wx = rotation.w * x2;
  const wy = rotation.w * y2;
  const wz = rotation.w * z2;

  return new Float32Array([
    (1 - (yy + zz)) * scale.x,
    (xy + wz) * scale.x,
    (xz - wy) * scale.x,
    0,
    (xy - wz) * scale.y,
    (1 - (xx + zz)) * scale.y,
    (yz + wx) * scale.y,
    0,
    (xz + wy) * scale.z,
    (yz - wx) * scale.z,
    (1 - (xx + yy)) * scale.z,
    0,
    position.x,
    position.y,
    position.z,
    1
  ]);
}
