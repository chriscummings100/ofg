use engine_core::{
    Engine, EngineError, EngineUpdateInput, EntityId, MaterialId, MeshId, PlayerMode,
    PlayerMovementIntent, TerrainComponent, Vec3, RENDER_SNAPSHOT_FLOAT_COUNT,
};
use terrain_core::{height_at, DEFAULT_TERRAIN_PRESET};

const INITIAL_PLAYER_X: f32 = 0.0;
const INITIAL_PLAYER_Z: f32 = 0.0;
const INITIAL_PLAYER_YAW: f32 = std::f32::consts::PI * 0.18;
const INITIAL_PLAYER_PITCH: f32 = -0.08;
const INITIAL_DEBUG_X: f32 = 14.0;
const INITIAL_DEBUG_Z: f32 = 18.0;
const INITIAL_DEBUG_HEIGHT_OFFSET: f32 = 12.0;
const INITIAL_DEBUG_YAW: f32 = std::f32::consts::PI * 1.24;
const INITIAL_DEBUG_PITCH: f32 = -0.48;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BrowserGameInput {
    pub delta_seconds: f32,
    pub forward: f32,
    pub right: f32,
    pub up: f32,
    pub fast: bool,
    pub look_delta_x: f32,
    pub look_delta_y: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub enum BrowserGameStateError {
    Engine(EngineError),
    InvalidTerrainHeight { x: f32, z: f32 },
    MissingSceneMeshResource(MeshId),
    MissingSceneMaterialResource(MaterialId),
}

#[derive(Clone, Debug, PartialEq)]
pub struct BrowserSceneMeshItem {
    pub entity: EntityId,
    pub mesh: MeshId,
    pub mesh_label: String,
    pub material: MaterialId,
    pub material_label: String,
    pub world_matrix: [f32; 16],
}

pub struct BrowserGameState {
    engine: Engine,
    terrain_seed: u32,
    terrain_preset: u32,
}

impl BrowserGameState {
    pub fn new() -> Self {
        Self {
            engine: Engine::new(),
            terrain_seed: 0,
            terrain_preset: DEFAULT_TERRAIN_PRESET,
        }
    }

    pub fn reset_game(
        &mut self,
        terrain_seed: u32,
        terrain_preset: u32,
    ) -> Result<(), BrowserGameStateError> {
        self.engine = Engine::new();
        self.terrain_seed = terrain_seed;
        self.terrain_preset = terrain_preset;

        let terrain_entity = self.engine.scene_mut().create_entity();
        {
            let mut entity = self
                .engine
                .scene_mut()
                .entity_mut(terrain_entity)
                .map_err(EngineError::from)?;
            entity.add_terrain(TerrainComponent {
                seed: terrain_seed,
                preset: terrain_preset,
            });
        }
        self.engine
            .scene_mut()
            .set_terrain(Some(terrain_entity))
            .map_err(EngineError::from)?;

        let initial_height = self.terrain_height_at(INITIAL_PLAYER_X, INITIAL_PLAYER_Z)?;
        self.engine.create_player(Vec3::new(
            INITIAL_PLAYER_X,
            initial_height,
            INITIAL_PLAYER_Z,
        ));
        self.engine
            .set_player_view(INITIAL_PLAYER_YAW, INITIAL_PLAYER_PITCH)?;
        self.engine.set_debug_camera(
            Vec3::new(
                INITIAL_DEBUG_X,
                initial_height + INITIAL_DEBUG_HEIGHT_OFFSET,
                INITIAL_DEBUG_Z,
            ),
            INITIAL_DEBUG_YAW,
            INITIAL_DEBUG_PITCH,
        )?;
        self.engine.set_player_mode(PlayerMode::FirstPerson)?;

        Ok(())
    }

    pub fn tick(&mut self, input: BrowserGameInput) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine
            .set_player_movement_intent(PlayerMovementIntent {
                forward: input.forward,
                right: input.right,
                up: input.up,
                fast: input.fast,
                look_delta_x: input.look_delta_x,
                look_delta_y: input.look_delta_y,
            })?;

        let terrain_height = if self.player_mode()? == PlayerMode::FirstPerson {
            let preview = self.engine.preview_player_position(input.delta_seconds)?;
            Some(self.terrain_height_at(preview.x, preview.z)?)
        } else {
            None
        };

        self.engine
            .update_player(input.delta_seconds, terrain_height)?;
        self.engine.update(EngineUpdateInput {
            delta_seconds: input.delta_seconds,
        })?;

        Ok(())
    }

    pub fn toggle_player_mode(&mut self) -> Result<PlayerMode, BrowserGameStateError> {
        self.ensure_player()?;
        Ok(self.engine.toggle_player_mode()?)
    }

    pub fn player_mode(&self) -> Result<PlayerMode, BrowserGameStateError> {
        Ok(self.engine.player_mode()?)
    }

    pub fn set_player_mode(&mut self, mode: PlayerMode) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine.set_player_mode(mode)?;
        Ok(())
    }

    pub fn player_position(&self) -> Result<Vec3, BrowserGameStateError> {
        Ok(self.engine.player_position()?)
    }

    pub fn terrain_seed(&self) -> u32 {
        self.terrain_seed
    }

    pub fn terrain_preset(&self) -> u32 {
        self.terrain_preset
    }

    pub fn set_player_position_xz(
        &mut self,
        x: f32,
        z: f32,
    ) -> Result<Vec3, BrowserGameStateError> {
        self.ensure_player()?;
        let height = self.terrain_height_at(x, z)?;
        let position = Vec3::new(x, height, z);
        self.engine.set_player_position(position)?;
        Ok(position)
    }

    pub fn set_debug_camera(
        &mut self,
        position: Vec3,
        yaw: f32,
        pitch: f32,
    ) -> Result<(), BrowserGameStateError> {
        self.ensure_player()?;
        self.engine.set_debug_camera(position, yaw, pitch)?;
        Ok(())
    }

    pub fn render_snapshot_values(
        &self,
    ) -> Result<[f32; RENDER_SNAPSHOT_FLOAT_COUNT], BrowserGameStateError> {
        let mut values = [0.0; RENDER_SNAPSHOT_FLOAT_COUNT];
        self.engine.render_snapshot()?.write_f32s(&mut values);
        Ok(values)
    }

    pub fn render_mesh_items(&self) -> Result<Vec<BrowserSceneMeshItem>, BrowserGameStateError> {
        let items = self.engine.render_mesh_items()?;
        let resources = self.engine.scene().resources();
        let mut browser_items = Vec::with_capacity(items.len());

        for item in items {
            let mesh_label = resources
                .mesh(item.mesh)
                .ok_or(BrowserGameStateError::MissingSceneMeshResource(item.mesh))?
                .label
                .clone();
            let material_label = resources
                .material(item.material)
                .ok_or(BrowserGameStateError::MissingSceneMaterialResource(
                    item.material,
                ))?
                .label
                .clone();

            browser_items.push(BrowserSceneMeshItem {
                entity: item.entity,
                mesh: item.mesh,
                mesh_label,
                material: item.material,
                material_label,
                world_matrix: item.world_matrix,
            });
        }

        Ok(browser_items)
    }

    #[cfg(test)]
    pub(crate) fn terrain_component(&self) -> Option<TerrainComponent> {
        let terrain = self.engine.scene().terrain_id()?;
        self.engine.scene().entity(terrain).ok()?.terrain().copied()
    }

    fn ensure_player(&mut self) -> Result<(), BrowserGameStateError> {
        if self.engine.player_rig().is_some() {
            return Ok(());
        }

        self.reset_game(self.terrain_seed, self.terrain_preset)
    }

    fn terrain_height_at(&self, x: f32, z: f32) -> Result<f32, BrowserGameStateError> {
        let height = height_at(
            self.terrain_seed,
            self.terrain_preset,
            f64::from(x),
            f64::from(z),
        ) as f32;
        if !height.is_finite() {
            return Err(BrowserGameStateError::InvalidTerrainHeight { x, z });
        }

        Ok(height)
    }
}

impl Default for BrowserGameState {
    fn default() -> Self {
        Self::new()
    }
}

impl From<EngineError> for BrowserGameStateError {
    fn from(error: EngineError) -> Self {
        Self::Engine(error)
    }
}

impl std::fmt::Display for BrowserGameStateError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Engine(error) => write!(formatter, "Rust browser game engine error: {error:?}"),
            Self::InvalidTerrainHeight { x, z } => {
                write!(
                    formatter,
                    "Rust browser game terrain height was invalid at ({x}, {z})"
                )
            }
            Self::MissingSceneMeshResource(mesh) => {
                write!(
                    formatter,
                    "Rust browser game scene mesh {mesh:?} was missing"
                )
            }
            Self::MissingSceneMaterialResource(material) => {
                write!(
                    formatter,
                    "Rust browser game scene material {material:?} was missing"
                )
            }
        }
    }
}

pub fn player_mode_code(mode: PlayerMode) -> u32 {
    mode.code()
}

pub fn player_mode_from_code(code: u32) -> Option<PlayerMode> {
    PlayerMode::from_code(code)
}
