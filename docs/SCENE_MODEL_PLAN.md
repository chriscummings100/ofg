# Scene Model Plan

## Goal

Add a tiny scene graph and component model that the game can stick to while it grows.
This is not a full ECS and should not become one by accident. The model is:

- One global active `Scene`.
- A `Scene` contains a tree of `Entity` objects.
- Every `Entity` has a `Transform`.
- Behavior and renderability are added with `Component` objects.
- Scene-level resources such as meshes, textures, and materials live in a
  `ResourceStore`.
- Render extraction produces plain render data for WebGPU. The renderer should not
  know about entities.

The design favors simple, explicit APIs over strict encapsulation. Components may
call `getScene()` when they need world context.

## Proposed Files

```text
src/engine/scene/
  activeScene.ts
  Scene.ts
  Entity.ts
  Component.ts
  Transform.ts
  ResourceStore.ts

src/engine/render/
  Mesh.ts
  Texture.ts
  Material.ts
  MeshRenderer.ts
  TerrainRenderer.ts
  RenderWorld.ts
  SceneRenderExtractor.ts

src/game/components/
  PlayerController.ts
  TerrainChunkStreamer.ts

src/engine/world/
  simplexNoise3D.ts
  terrainChunk.ts
  terrainChunkMesher.ts
```

## Global Scene API

```ts
let activeScene: Scene | undefined;

export function createScene(): Scene;
export function getScene(): Scene;
export function setScene(scene: Scene): void;
export function resetScene(): Scene;
```

Rules:

- Runtime code should normally call `getScene()`.
- Tests should call `resetScene()` in setup.
- `getScene()` throws if no active scene exists.
- `createScene()` creates and installs a fresh scene.
- `setScene(scene)` exists for tests and explicit bootstrapping only.

## Core Types

```ts
type EntityId = number;
type ComponentType<T extends Component> = new (...args: never[]) => T;
type ResourceId = string;
```

### Scene

```ts
class Scene {
  readonly root: Entity;
  readonly resources: ResourceStore;
  mainLight: DirectionalLight;
  terrain?: TerrainRenderer;
  activeCamera?: Entity;

  createEntity(name?: string): Entity;
  destroyEntity(entity: Entity): void;
  update(deltaSeconds: number): void;
  traverse(callback: (entity: Entity) => void): void;
  findByName(name: string): Entity | undefined;
  queryComponents<T extends Component>(type: ComponentType<T>): T[];
  getTerrainHeight(x: number, z: number): number | undefined;
}
```

Responsibilities:

- Own the root entity and global resources.
- Maintain scene-wide convenience references such as `terrain` and `activeCamera`.
- Maintain the main directional light used as the sun.
- Traverse enabled entities and update enabled components.
- Delegate terrain height queries to `terrain`.

Non-responsibilities:

- No WebGPU resource creation.
- No chunk meshing logic.
- No browser input handling.

### Entity

```ts
class Entity {
  readonly id: EntityId;
  name: string;
  enabled: boolean;
  parent?: Entity;
  readonly transform: Transform;
  readonly children: Entity[];
  readonly components: Component[];

  addChild(child: Entity): void;
  removeChild(child: Entity): void;
  destroy(): void;
  addComponent<T extends Component>(component: T): T;
  getComponent<T extends Component>(type: ComponentType<T>): T | undefined;
  removeComponent(component: Component): void;
  updateWorldTransform(): void;
}
```

Responsibilities:

- Maintain parent/child invariants.
- Own local and world transform state.
- Own component lifecycle attachment.

Rules:

- Reparenting removes the entity from its previous parent.
- Root cannot be parented under another entity.
- Destroying an entity destroys descendants and detaches components.

### Component

```ts
abstract class Component {
  entity?: Entity;
  enabled: boolean;

  onAttach(): void;
  onDetach(): void;
  update(deltaSeconds: number): void;
}
```

Responsibilities:

- Add behavior or metadata to an entity.
- Use `getScene()` when global context is needed.

Rules:

- A component may only be attached to one entity at a time.
- Disabled components do not update.
- Components should not directly create WebGPU resources.

### Transform

```ts
class Transform {
  position: Vec3;
  rotation: Quat;
  scale: Vec3;

  getLocalMatrix(): Mat4;
  getWorldMatrix(): Mat4;
  setPosition(position: Vec3): void;
  translate(delta: Vec3): void;
  setRotation(rotation: Quat): void;
  setScale(scale: Vec3): void;
  markDirty(): void;
}
```

Responsibilities:

- Store local transform data.
- Cache local/world matrices once that becomes useful.
- Propagate dirty state through child entities.

Implementation note:

- `Quat` is available and is the supported rotation representation.

### ResourceStore

```ts
class ResourceStore {
  addMesh(mesh: Mesh): ResourceId;
  getMesh(id: ResourceId): Mesh;
  removeMesh(id: ResourceId): void;

  addTexture(texture: Texture): ResourceId;
  getTexture(id: ResourceId): Texture;
  removeTexture(id: ResourceId): void;

  addMaterial(material: Material): ResourceId;
  getMaterial(id: ResourceId): Material;
  removeMaterial(id: ResourceId): void;
}
```

Responsibilities:

- Store CPU-side resource descriptions by stable IDs.
- Keep renderer ownership separate from scene ownership.

Rules:

- Missing resources should throw with useful messages.
- GPU handles do not live here in the first version.

## Render Types

### Mesh

```ts
class Mesh {
  readonly id: ResourceId;
  readonly vertices: Float32Array;
  readonly indices: Uint32Array;
  readonly layout: VertexLayout;
}
```

### Texture

```ts
class Texture {
  readonly id: ResourceId;
  readonly width: number;
  readonly height: number;
  readonly format: TextureFormat;
  readonly data?: Uint8Array | Uint8ClampedArray;
}
```

### Material

The initial material exists to shape the single uber-shader contract.

```ts
class Material {
  readonly id: ResourceId;
  albedoFactor: Vec4;
  albedoTexture?: ResourceId;
  specular: Vec3;
  specularFactor: number;
  flags: number;
}
```

Initial renderer support:

- Vertex color is treated as mesh albedo input.
- `albedoFactor` and optional `albedoTexture` multiply vertex color.
- `specular` and `specularFactor` feed a simple Blinn-Phong highlight.
- Texture resources remain CPU-side descriptions; the WebGPU renderer owns upload
  and sampler state.

### MeshRenderer

```ts
class MeshRenderer extends Component {
  mesh: ResourceId;
  material?: ResourceId;
  visible: boolean;

  getRenderItem(): RenderItem | undefined;
}
```

Rules:

- Returns no render item when disabled or hidden.
- Resolves resources through `getScene().resources`.
- Emits world transform, mesh, and material only.

### TerrainRenderer

```ts
class TerrainRenderer extends Component {
  field: TerrainField;
  chunks: TerrainChunk[];

  heightAt(x: number, z: number): number;
  densityAt(position: Vec3): number;
  addChunk(chunk: TerrainChunk): void;
  getChunk(chunk: ChunkKey | TerrainChunkCoord): TerrainChunk | undefined;
  removeChunk(chunk: ChunkKey | TerrainChunkCoord): boolean;
  setChunks(chunks: TerrainChunk[]): void;
  rebuildChunk(chunkKey: ChunkKey | TerrainChunkCoord): TerrainChunk | undefined;
  getRenderItems(): RenderItem[];
}
```

Implementation:

- Wrap the current seed heightfield mesh.
- Expose `heightAt()` and `densityAt()`.
- Track render chunks by stable 3D terrain chunk keys.
- Later, this becomes the boundary for chunked Dual Contouring terrain.

### Terrain Density Chunks

The current seed `TerrainField` is an implicit density field. Low-frequency x/z
noise produces a broad preferred surface height, and octave 3D simplex noise adds
density detail:

```text
density(p) = p.y - largeFeatureHeight(p.x, p.z) - detail3D(p) * amplitude
```

The simplex module returns both values and analytic gradients; the seed field uses
the density gradient for smooth normals. `heightAt(x, z)` remains available for the
temporary player grounding path by scanning the density column for the highest zero
crossing.

```ts
const TERRAIN_CHUNK_CELLS_PER_AXIS = 32;
const TERRAIN_CHUNK_SAMPLES_PER_AXIS = 33;

type TerrainChunkCoord = { x: number; y: number; z: number };
type TerrainDensitySource = { densityAt(position: Vec3): number };

class TerrainDensityChunk {
  readonly coord: TerrainChunkCoord;
  readonly key: TerrainChunkKey;
  readonly cellSize: number;
  readonly densities: Float32Array;

  densityAtSample(sample: TerrainChunkSampleCoord): number;
  setDensityAtSample(sample: TerrainChunkSampleCoord, density: number): void;
  samplePosition(sample: TerrainChunkSampleCoord): Vec3;
  bounds(): TerrainChunkBounds;
}
```

Rules:

- Chunks are fully 3D, with 32x32x32 cells and 33x33x33 density samples.
- Adjacent chunks share seam positions by construction.
- Baseline generation samples a `TerrainDensitySource`.
- Terrain edits apply after the baseline density. The first edit operation is a
  subtract-sphere edit for cave/mining-style cuts.

### Highest-Surface Chunk Meshing

```ts
function findHighestSurfaceInColumn(
  chunk: TerrainDensityChunk,
  x: number,
  z: number
): number | undefined;

function meshChunkHighestSurface(chunk: TerrainDensityChunk): MeshData;
function meshChunkHighestSurfaceStack(chunks: readonly TerrainDensityChunk[]): MeshData;
```

Current role:

- Provide a small, testable meshing bridge before Dual Contouring exists.
- Scan density columns from top to bottom and interpolate the first solid-to-air
  crossing.
- Mesh a vertical stack as one x/z render column so surfaces can cross density
  chunk y boundaries without visible holes.
- Emit the same position/color/normal/uv layout used by regular terrain meshes.

Non-goals:

- This is not the final voxel mesher.
- It does not represent overhangs or caves; lower surfaces in a column are ignored.

### TerrainChunkStreamer

```ts
class TerrainChunkStreamer extends Component {
  terrain: TerrainRenderer;
  source: TerrainDensitySource;
  target?: Entity;
  material?: ResourceId;
  horizontalRadius: number;
  verticalChunkOffsets: readonly number[];
  cellSize: number;

  syncAround(center: Vec3): void;
  rebuildChunk(chunk: TerrainChunkKey | TerrainChunkCoord): void;
  invalidateAll(): void;
  getLoadedChunkKeys(): string[];
}
```

Responsibilities:

- Keep a square x/z neighborhood of density chunks loaded around a target position.
- Generate density chunks from the source and mesh each x/z column through the
  configured vertical chunk-offset stack centered on the target chunk y coordinate.
- Add and remove visible render chunks through `TerrainRenderer`.
- Remain deterministic and easy to test without WebGPU.

### RenderWorld

```ts
type RenderWorld = {
  camera: CameraFrame;
  mainLight: DirectionalLight;
  items: RenderItem[];
};

type RenderItem = {
  id: string;
  mesh: Mesh;
  material?: Material;
  albedoTexture?: Texture;
  worldMatrix: Mat4;
};
```

### SceneRenderExtractor

```ts
class SceneRenderExtractor {
  static buildRenderWorld(): RenderWorld;
}
```

Rules:

- Reads the global scene with `getScene()`.
- Finds render components.
- Produces plain render data for WebGPU.
- Does not create buffers, pipelines, textures, or bind groups.

## Game Components

### PlayerController

```ts
class PlayerController extends Component {
  moveSpeed: number;
  eyeHeight: number;
  mode: "firstPerson" | "debugFly";

  update(deltaSeconds: number): void;
  toggleCameraMode(): void;
  getEyeTransform(): TransformSnapshot;
}
```

Responsibilities:

- Move the entity from input intent.
- In first-person mode, ground the entity via `getScene().getTerrainHeight()`.
- In debug-fly mode, move freely.
- Provide the camera eye transform.

Implementation note:

- Input currently stays outside the scene model. `PlayerController` consumes a
  `PlayerMovementIntent` object until an input binding layer exists.

## Test Plan

### `src/engine/scene/activeScene.test.ts`

- `getScene throws before a scene is created`
- `createScene installs a global scene`
- `setScene replaces the active scene`
- `resetScene gives tests a clean scene`

### `src/engine/scene/Entity.test.ts`

- `creates entities with stable unique ids`
- `parents new scene entities under the root`
- `reparenting removes the child from its previous parent`
- `destroy removes descendants from traversal`
- `root cannot be parented under another entity`

### `src/engine/scene/Component.test.ts`

- `addComponent attaches the component to the entity`
- `removeComponent detaches the component`
- `scene update calls enabled components`
- `scene update skips disabled components`
- `a component cannot be attached to two entities`

### `src/engine/scene/Transform.test.ts`

- `starts with identity local transform`
- `local matrix reflects translation rotation and scale`
- `child world matrix includes parent transform`
- `markDirty propagates to descendants`

### `src/engine/scene/ResourceStore.test.ts`

- `adds and retrieves meshes by stable id`
- `throws when a mesh id is missing`
- `removes meshes`
- `stores materials and textures independently`

### `src/engine/render/MeshRenderer.test.ts`

- `emits a render item with the entity world matrix`
- `resolves mesh and material from the global scene resources`
- `emits no render item when hidden`
- `emits no render item when disabled`
- `throws a useful error for missing mesh resources`

### `src/engine/render/TerrainRenderer.test.ts`

- `delegates height queries to the terrain field`
- `delegates density queries to the terrain field`
- `registers itself as scene terrain when attached`
- `clears scene terrain when detached`
- `emits terrain render items`
- `adds and replaces chunks by key`
- `finds and removes chunks by 3D chunk coordinates`

### `src/engine/world/terrainChunk.test.ts`

- `uses 32 cells and 33 samples per axis`
- `creates stable chunk keys that support negative coordinates`
- `samples adjacent chunks with matching seam densities`
- `applies subtract sphere edits after baseline density`
- `generates chunks with edits applied on top of the baseline`

### `src/engine/world/simplexNoise3D.test.ts`

- `returns deterministic values for a seed`
- `uses the seed to choose a different gradient lattice`
- `reports analytic gradients that match finite differences`
- `combines octaves while keeping gradients in input coordinate space`

### `src/engine/world/scalarField.test.ts`

- `reports zero density on the terrain surface`
- `uses deterministic noise terrain with useful height variation`
- `uses 3D detail noise inside the density field`
- `returns normals that point from solid toward air`

### `src/engine/world/terrainChunkMesher.test.ts`

- `finds an interpolated highest surface in a density column`
- `meshes a flat chunk surface with shared vertices and full cell coverage`
- `skips cells whose corners have no surface`
- `meshes a complete surface across a vertical stack of density chunks`
- `writes sloped normals from neighboring surface heights`

### `src/game/components/TerrainChunkStreamer.test.ts`

- `generates a render chunk around the target entity`
- `generates square xz neighborhoods for every requested vertical chunk offset`
- `centers vertical chunk offsets on the target y coordinate`
- `moves the loaded chunk window as the target crosses chunk boundaries`
- `skips render chunks with no surface while remembering they were loaded`
- `can rebuild an already loaded chunk`
- `updates from scene traversal when attached as a component`

### `src/engine/render/SceneRenderExtractor.test.ts`

- `builds a render world from the active scene`
- `includes mesh renderer items`
- `includes terrain renderer items`
- `excludes disabled entities`
- `uses the scene active camera`

### `src/game/components/PlayerController.test.ts`

- `grounds first-person movement against scene terrain`
- `debug fly movement ignores scene terrain`
- `toggleCameraMode switches between first-person and debug fly`
- `getEyeTransform includes eye height`
- `does not move when disabled`

## Implementation Phases

Status: Phases 1 through 6 have an initial implementation. The next terrain-facing
work is to move from highest-surface meshing toward a tested voxel mesher, with
Dual Contouring as the intended destination.

### Phase 1: Scene Core

Add `activeScene`, `Scene`, `Entity`, `Component`, and basic traversal/update tests.
Do not change rendering yet.

Done when:

- Scene/component tests pass.
- Existing camera/world/render tests still pass.
- Current app still runs.

### Phase 2: Transform Hierarchy

Add `Transform` world/local matrix support and entity transform propagation.

Done when:

- Transform tests pass.
- Existing camera math stays unchanged unless intentionally migrated.

### Phase 3: Resource Store And Render Data

Add `ResourceStore`, `Mesh`, `Material`, `Texture`, `MeshRenderer`, `TerrainRenderer`,
`RenderWorld`, and `SceneRenderExtractor`.

Done when:

- Render extraction is testable without WebGPU.
- Terrain height queries flow through `Scene.getTerrainHeight()`.
- WebGPU renderer still receives plain render data.

### Phase 4: Migrate Current Game

Move the current heightfield terrain and player marker into the scene model.
Replace direct renderer mesh setup in `startGame()` with scene setup plus render
extraction.

Implemented notes:

- `src/app/game.ts` creates the global scene and bootstraps terrain, player, marker,
  and camera entities.
- `PlayerController` owns first-person movement and a separate debug fly camera
  position/orientation.
- `WebGpuRenderer` consumes `RenderWorld` and draws render items with per-object
  transforms.

Done when:

- First-person mode still starts with visible terrain.
- Debug fly mode still shows the player marker.
- `npm test` passes.
- Browser smoke test exists or is created in the same phase.

### Phase 5: Shader Build Boundary

Before adding richer materials or more render component types, add the shader source
boundary for a single uber shader and prepare the Slang build path.

Implemented notes:

- `src/engine/render/shaders/uber.wgsl` owns the current WGSL source.
- `tools/build-shaders.mjs` generates `src/generated/render/uberShader.ts` with
  source, entry-point metadata, and a deterministic source hash.
- `WebGpuRenderer` imports the generated shader artifact instead of embedding WGSL.
- The shader includes both the mesh material pass and the procedural sky pass.
- `RenderWorld.mainLight` drives Lambert/Blinn-Phong lighting and the sky sun disk.

Done when:

- Shader source is no longer embedded in `WebGpuRenderer`.
- Shader build output is committed or generated deterministically.
- A test or script verifies shader artifacts can be produced.

### Phase 6: Chunk Meshing And Streaming

Add a minimal terrain mesher and a scene component that streams chunk columns around
the player.

Implemented notes:

- `TerrainDensityChunk` stores 33x33x33 samples for 32x32x32 chunk cells.
- `terrainChunkMesher` finds the highest surface in each x/z column and emits a
  shared-vertex render mesh.
- `meshChunkHighestSurfaceStack()` meshes a vertical density stack into one render
  chunk to avoid y-boundary gaps in the temporary height-style surface.
- `TerrainChunkStreamer` follows a target entity, generates density chunks on
  demand, and updates the `TerrainRenderer` chunk list.
- Browser smoke moves the player across chunk columns and verifies streamed chunk
  keys before taking a third screenshot.

Done when:

- Chunk and mesher unit tests pass.
- Streamer tests cover movement, rebuild, invalidation, and no-surface chunks.
- Browser smoke verifies terrain still renders after a chunk-window move.

## Constraints

- Keep the global scene explicit and easy to reset.
- Do not add a general-purpose ECS.
- Do not let WebGPU handles leak into scene resources yet.
- Keep scene tests fast and browser-free.
- Prefer boring data shapes that can later cross a Rust/WASM boundary.
