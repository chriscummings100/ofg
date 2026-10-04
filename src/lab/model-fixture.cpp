// Connects a loaded Model to the existing fly camera and PBR laboratory lighting without animation playback.
#include "lab/model-fixture.h"
#include "lab/pbr-fixture.h"
#include "resources/resources.h"
#include <cstdio>

namespace ofg {
std::unique_ptr<Scene> createModelFixtureScene()
{
    auto scene = std::make_unique<Scene>();
    auto* entity = scene->createEntity(scene->getRoot());
    entity->setLocalPosition({0, 0, -5});
    auto* camera = scene->createCamera(entity);
    scene->setActiveCamera(camera);
    scene->lighting.hdr = true;
    scene->lighting.environment = createStudioEnvironment();
    scene->lighting.lightCount = 1;
    scene->lighting.lights[0].direction = {.4f, -.6f, 1};
    scene->lighting.lights[0].intensity = 3;
    return scene;
}

ModelFixture::ModelFixture(const std::string& path)
    : m_model(Resources::loadResourceAsync<Model>(path))
{
}

void ModelFixture::update(Scene& scene)
{
    if (ready() || !m_model->isFinished())
    {
        return;
    }
    if (m_model->isFailed())
    {
        throw EngineError(m_model->error());
    }
    m_instance = scene.instantiateModel(m_model, scene.getRoot());
    const auto& data = m_model->data();
    m_status = "Model ready | " + std::to_string(data.nodes.size()) + " nodes, " + std::to_string(data.meshes.size()) +
               " meshes, " + std::to_string(data.materials.size() - 1) + " materials, " +
               std::to_string(data.skins.size()) + " skins, " + std::to_string(data.animations.size()) +
               " clips | Undeformed preview; animation is not playing";
    std::printf("%s\n", m_status.c_str());
    for (const auto& warning : m_model->warnings())
    {
        std::printf("Model warning: %s\n", warning.c_str());
    }
}
} // namespace ofg
