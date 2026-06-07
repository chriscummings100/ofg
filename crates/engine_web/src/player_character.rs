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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn player_character_ids_round_trip_browser_names_and_display() {
        assert_eq!(PlayerCharacterId::Male.js_name(), "male");
        assert_eq!(PlayerCharacterId::Female.js_name(), "female");
        assert_eq!(
            PlayerCharacterId::from_js_name("male"),
            Some(PlayerCharacterId::Male)
        );
        assert_eq!(
            PlayerCharacterId::from_js_name("female"),
            Some(PlayerCharacterId::Female)
        );
        assert_eq!(PlayerCharacterId::from_js_name("wizard"), None);
        assert_eq!(PlayerCharacterId::Male.to_string(), "male");
        assert_eq!(PlayerCharacterId::Female.to_string(), "female");
    }

    #[test]
    fn player_character_toggle_switches_between_known_characters() {
        assert_eq!(PlayerCharacterId::Male.toggled(), PlayerCharacterId::Female);
        assert_eq!(PlayerCharacterId::Female.toggled(), PlayerCharacterId::Male);
    }

    #[test]
    fn player_character_descriptors_keep_browser_ids_and_asset_labels_stable() {
        assert_eq!(PLAYER_CHARACTER_DESCRIPTORS.len(), 2);

        let male = player_character_descriptor(PlayerCharacterId::Male);
        assert_eq!(male.id, PlayerCharacterId::Male);
        assert_eq!(male.label, "Male");
        assert_eq!(male.model_id, PLAYER_SUPERHERO_MALE_MODEL_ID);
        assert_eq!(male.model_url, PLAYER_SUPERHERO_MALE_MODEL_URL);
        assert_eq!(male.mesh_label, PLAYER_SUPERHERO_MALE_MESH_LABEL);
        assert_eq!(male.material_label, PLAYER_SUPERHERO_MALE_MATERIAL_LABEL);

        let female = player_character_descriptor(PlayerCharacterId::Female);
        assert_eq!(female.id, PlayerCharacterId::Female);
        assert_eq!(female.label, "Female");
        assert_eq!(female.model_id, PLAYER_SUPERHERO_FEMALE_MODEL_ID);
        assert_eq!(female.model_url, PLAYER_SUPERHERO_FEMALE_MODEL_URL);
        assert_eq!(female.mesh_label, PLAYER_SUPERHERO_FEMALE_MESH_LABEL);
        assert_eq!(
            female.material_label,
            PLAYER_SUPERHERO_FEMALE_MATERIAL_LABEL
        );
    }
}
