// Asynchronous superhero fixture with shared UAL1 clips and direct name/hierarchy mapping.
#pragma once
#include "scene/scene.h"

namespace ofg {
// Creates the existing PBR model-view lighting and a fly-camera view framing the two-metre character.
std::unique_ptr<Scene> createCharacterFixtureScene();
// Restores the character framing after the fly camera has reset its orientation.
void frameCharacter(Entity& camera);

class CharacterFixture
{
public:
    // Requests both preserved assets; the library rig is never instantiated.
    explicit CharacterFixture(const std::string& assetDirectory, bool pair = false);
    // Publishes a superhero once both assets are ready, validates mappings and explicitly starts idle.
    void update(Scene& scene);
    // Reports successful binding and instantiation, rather than completed file I/O.
    bool ready() const noexcept
    {
        return m_instances.size() == (m_pair ? 2u : 1u) && bool(m_instances.front().animator);
    }

private:
    std::shared_ptr<Model> m_character, m_library;
    std::vector<ModelInstance> m_instances;
    bool m_pair;
};
} // namespace ofg
