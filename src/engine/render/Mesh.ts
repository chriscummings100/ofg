import type { ResourceId } from "./ResourceId.js";

export type VertexLayout = {
  readonly floatsPerVertex: number;
  readonly attributes: readonly VertexAttribute[];
};

export type VertexAttribute = {
  readonly name: string;
  readonly offset: number;
  readonly size: number;
};

export class Mesh {
  readonly id: ResourceId;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly layout: VertexLayout;

  constructor(
    id: ResourceId,
    vertices: Float32Array,
    indices: Uint32Array,
    layout: VertexLayout
  ) {
    this.id = id;
    this.vertices = vertices;
    this.indices = indices;
    this.layout = layout;
  }
}
