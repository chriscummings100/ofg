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

Initial implementation note:

- If `Quat` is not yet available, add it before this work or temporarily support
  yaw-only rotation with an explicit TODO in the tests.

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
}
```

### Material

The initial material exists to shape the single uber-shader contract.

```ts
class Material {
  readonly id: ResourceId;
  baseColor: Vec4;
  texture?: ResourceId;
  flags: number;
}
```

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
  rebuildChunk(chunkKey: ChunkKey): void;
  getRenderItems(): RenderItem[];
}
```

Initial implementation:

- Wrap the current seed heightfield mesh.
- Expose `heightAt()` and `densityAt()`.
- Later, this becomes the boundary for chunked Dual Contouring terrain.

### RenderWorld

```ts
type RenderWorld = {
  camera: CameraFrame;
  items: RenderItem[];
};

type RenderItem = {
  id: string;
  mesh: Mesh;
  material?: Material;
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

Initial implementation note:

- Input can stay outside the scene model at first. `PlayerController` may consume a
  simple `MovementIntent` object until an input binding layer exists.

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

Status: Phases 1 through 4 are implemented. Phase 5 is the next graphics-facing
architecture step.

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

Done when:

- Shader source is no longer embedded in `WebGpuRenderer`.
- Shader build output is committed or generated deterministically.
- A test or script verifies shader artifacts can be produced.

## Constraints

- Keep the global scene explicit and easy to reset.
- Do not add a general-purpose ECS.
- Do not let WebGPU handles leak into scene resources yet.
- Keep scene tests fast and browser-free.
- Prefer boring data shapes that can later cross a Rust/WASM boundary.
