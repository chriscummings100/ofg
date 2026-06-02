import type { ResourceId } from "./ResourceId.js";

export type RenderMeshPacket = {
  readonly id: ResourceId;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly floatsPerVertex: number;
};
