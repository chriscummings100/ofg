// Loads the preserved models without importing the historical animation system or retargeting poses.
#include "lab/character-fixture.h"
#include "lab/model-fixture.h"
#include "scene/animation-binding.h"
#include "resources/resources.h"
#include "core/engine-error.h"
#include <algorithm>
#include <cstdio>

namespace ofg {
void frameCharacter(Entity& camera)
{
    camera.setLocalPosition({0, 1, -4});
}

std::unique_ptr<Scene> createCharacterFixtureScene()
{
    auto scene = createModelFixtureScene();
    frameCharacter(*scene->activeCamera()->entity());
    return scene;
}

CharacterFixture::CharacterFixture(const std::string& assetDirectory, bool pair)
    : m_character(Resources::loadResourceAsync<Model>(assetDirectory + "/quaternius-superhero-male.glb"))
    , m_library(Resources::loadResourceAsync<Model>(assetDirectory + "/quaternius-ual1-standard.glb"))
    , m_pair(pair)
{
}

void CharacterFixture::update(Scene& scene)
{
    if (ready())
    {
        return;
    }
    for (const auto& asset : {m_character, m_library})
    {
        if (asset->isFailed())
        {
            throw EngineError("Character fixture: " + asset->key() + ": " + asset->error());
        }
    }
    if (!m_character->isLoaded() || !m_library->isLoaded())
    {
        return;
    }
    m_instances.clear(); // Scene replacement expires observers while the fixture retains its shared assets.
    for (int index = 0; index < (m_pair ? 2 : 1); ++index)
    {
        auto instance = scene.instantiateModel(m_character, scene.getRoot());
        instance.root->setName("Superhero " + std::to_string(index + 1));
        if (m_pair)
        {
            instance.root->setLocalPosition({index == 0 ? -.85f : .85f, 0, 0});
        }
        auto mapping = mapAnimationNodesByName(m_library->data(), instance);
        auto animator = instance.animator ? instance.animator.get() : scene.createAnimator(instance.root.get());
        animator->setBindings(m_library->data().animations, std::move(mapping));
        const auto& clips = animator->animations();
        auto idle = std::find_if(
            clips.begin(),
            clips.end(),
            [](const auto& clip)
            {
                return clip->desc().name == "Idle_Loop";
            }
        );
        if (clips.size() != 45 || idle == clips.end())
        {
            throw EngineError("Character fixture requires 45 UAL1 clips including Idle_Loop.");
        }
        animator->selectAnimation(size_t(idle - clips.begin()));
        animator->play();
        if (index == 1)
        {
            animator->seek(animator->durationSeconds() * .5);
            animator->setPlaybackSpeed(.6f);
        }
        instance.animator = animator;
        m_instances.push_back(std::move(instance));
    }
    std::printf("Character ready | 45 clips | direct mapping; rest transforms and proportions differ\n");
}
} // namespace ofg
