// Player character descriptor table for browser-facing commands and GLTF assets.
// The public IDs are intentionally generic so the placeholder Superhero meshes
// can be replaced by Regular male/female GLBs without changing browser commands.

use std::fmt;

use crate::model_assets::{
    PLAYER_SUPERHERO_FEMALE_MATERIAL_LABEL, PLAYER_SUPERHERO_FEMALE_MESH_LABEL,
    PLAYER_SUPERHERO_FEMALE_MODEL_ID, PLAYER_SUPERHERO_FEMALE_MODEL_URL,
    PLAYER_SUPERHERO_MALE_MATERIAL_LABEL, PLAYER_SUPERHERO_MALE_MESH_LABEL,
    PLAYER_SUPERHERO_MALE_MODEL_ID, PLAYER_SUPERHERO_MALE_MODEL_URL,
};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum PlayerCharacterId {
    Male,
    Female,
}

impl PlayerCharacterId {
    /// Returns the browser command/debug string for this character.
    pub const fn js_name(self) -> &'static str {
        match self {
            Self::Male => "male",
            Self::Female => "female",
        }
    }

    /// Parses a browser command/debug string into a character ID.
    pub fn from_js_name(name: &str) -> Option<Self> {
        match name {
            "male" => Some(Self::Male),
            "female" => Some(Self::Female),
            _ => None,
        }
    }

    /// Returns the other character for the HUD toggle.
    pub const fn toggled(self) -> Self {
        match self {
            Self::Male => Self::Female,
            Self::Female => Self::Male,
        }
    }
}

impl fmt::Display for PlayerCharacterId {
    /// Formats a stable character ID for logs.
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.js_name())
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlayerCharacterDescriptor {
    pub id: PlayerCharacterId,
    pub label: &'static str,
    pub model_id: &'static str,
    pub model_url: &'static str,
    pub mesh_label: &'static str,
    pub material_label: &'static str,
}

pub const PLAYER_CHARACTER_DESCRIPTORS: [PlayerCharacterDescriptor; 2] = [
    PlayerCharacterDescriptor {
        id: PlayerCharacterId::Male,
        label: "Male",
        model_id: PLAYER_SUPERHERO_MALE_MODEL_ID,
        model_url: PLAYER_SUPERHERO_MALE_MODEL_URL,
        mesh_label: PLAYER_SUPERHERO_MALE_MESH_LABEL,
        material_label: PLAYER_SUPERHERO_MALE_MATERIAL_LABEL,
    },
    PlayerCharacterDescriptor {
        id: PlayerCharacterId::Female,
        label: "Female",
        model_id: PLAYER_SUPERHERO_FEMALE_MODEL_ID,
        model_url: PLAYER_SUPERHERO_FEMALE_MODEL_URL,
        mesh_label: PLAYER_SUPERHERO_FEMALE_MESH_LABEL,
        material_label: PLAYER_SUPERHERO_FEMALE_MATERIAL_LABEL,
    },
];

/// Looks up the descriptor for one player character ID.
pub fn player_character_descriptor(id: PlayerCharacterId) -> PlayerCharacterDescriptor {
    PLAYER_CHARACTER_DESCRIPTORS
        .iter()
        .find(|descriptor| descriptor.id == id)
        .copied()
        .expect("player character ID must have a descriptor")
}
