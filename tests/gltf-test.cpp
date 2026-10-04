// Real file import, adversarial accessor boundaries and atomic scene instantiation without a GPU.
#include "resources/model.h"
#include "resources/resources.h"
#include "resources/gltf-import.h"
#include "scene/scene.h"
#include <ostream>
#include <doctest.h>
#include <json.hpp>
#include <bit>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace ofg;
using Json = nlohmann::json;
namespace {
// Loads the original fixture as editable source for boundary and failure tests.
Json fixtureJson()
{
    std::ifstream file("assets/models/laboratory.gltf");
    Json json;
    file >> json;
    json["buffers"][0]["uri"] = "laboratory.bin";
    return json;
}
// Owns exactly one temporary fixture file in the copied test assets directory.
struct FixtureFile
{
    std::string path;
    // Writes a unique file beside the fixture dependencies, avoiding cross-test cache identity.
    explicit FixtureFile(const Json& json)
    {
        static unsigned serial = 0;
        path = "assets/models/test-" + std::to_string(++serial) + ".gltf";
        std::ofstream file(path);
        file << json.dump();
        if (!file)
        {
            throw EngineError("Could not write glTF test fixture.");
        }
    }
    // Removes only this helper's file, after all local Model owners have gone away.
    ~FixtureFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
// Exercises the actual cooperative loader and returns its terminal result.
std::shared_ptr<Model> loadJson(const Json& json)
{
    FixtureFile file(json);
    return Resources::loadResource<Model>(file.path);
}
// Returns a simple normalized interleaved accessor, independent of parser schema validation.
tinygltf::Model accessorModel()
{
    tinygltf::Model model;
    tinygltf::Buffer buffer;
    buffer.data = {0, 127, 255, 99, 255, 0, 128, 99};
    model.buffers.push_back(buffer);
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteLength = 8;
    view.byteStride = 4;
    model.bufferViews.push_back(view);
    tinygltf::Accessor accessor;
    accessor.bufferView = 0;
    accessor.count = 2;
    accessor.type = TINYGLTF_TYPE_VEC3;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    accessor.normalized = true;
    model.accessors.push_back(accessor);
    return model;
}
// Encodes original fixture bytes as a standard base64 data URI to exercise embedded dependency loading.
std::string dataUri(const char* path, const char* mime)
{
    std::ifstream input(path, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result = std::string("data:") + mime + ";base64,";
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t word = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                              (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        result += alphabet[word >> 18];
        result += alphabet[(word >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
    }
    return result;
}
} // namespace

TEST_CASE("embedded data URIs and escaped relative dependency paths load through the same importer")
{
    auto json = fixtureJson();
    SUBCASE("embedded buffer and JPEG")
    {
        json["buffers"][0]["uri"] = dataUri("assets/models/laboratory.bin", "application/octet-stream");
        json["images"][0]["uri"] = dataUri("assets/checker.jpg", "image/jpeg");
    }
    SUBCASE("percent decoded paths with parent traversal")
    {
        json["buffers"][0]["uri"] = "../models/%6Caboratory.bin";
        json["images"][0]["uri"] = "../%63hecker.png";
    }
    auto model = loadJson(json);
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    CHECK(model->data().textures[0]->desc().width == 96);
}

TEST_CASE("primitive conversion preserves submeshes influence sets and strip fan winding")
{
    auto json = fixtureJson();
    auto& primitive = json["meshes"][0]["primitives"][0];
    const int indexAccessor = primitive["indices"].get<int>();
    SUBCASE("nonindexed")
    {
        primitive.erase("indices");
        auto model = loadJson(json);
        REQUIRE_MESSAGE(model->isLoaded(), model->error());
        CHECK(model->data().meshes[0]->indices().size() == 24);
        CHECK(model->data().meshes[0]->indices()[5] == 4);
    }
    SUBCASE("strip")
    {
        primitive["mode"] = 5;
        json["accessors"][indexAccessor]["count"] = 4;
        auto model = loadJson(json);
        REQUIRE_MESSAGE(model->isLoaded(), model->error());
        auto indices = model->data().meshes[0]->indices();
        REQUIRE(indices.size() == 6);
        CHECK(indices[3] == 2);
        CHECK(indices[4] == 0);
        CHECK(indices[5] == 1);
    }
    SUBCASE("fan")
    {
        primitive["mode"] = 6;
        json["accessors"][indexAccessor]["count"] = 4;
        auto model = loadJson(json);
        REQUIRE_MESSAGE(model->isLoaded(), model->error());
        auto indices = model->data().meshes[0]->indices();
        REQUIRE(indices.size() == 6);
        CHECK(indices[3] == 0);
        CHECK(indices[4] == 0);
        CHECK(indices[5] == 2);
    }
    SUBCASE("two primitives and two influence sets")
    {
        primitive["attributes"]["JOINTS_1"] = primitive["attributes"]["JOINTS_0"];
        primitive["attributes"]["WEIGHTS_1"] = json["accessors"].size();
        json["accessors"].push_back({{"componentType", 5126}, {"type", "VEC4"}, {"count", 24}});
        auto second = primitive;
        json["meshes"][0]["primitives"].push_back(second);
        auto model = loadJson(json);
        REQUIRE_MESSAGE(model->isLoaded(), model->error());
        auto mesh = model->data().meshes[0];
        REQUIRE(mesh->subMeshes().size() == 2);
        CHECK(mesh->vertices().size() == 48);
        CHECK(mesh->indices()[36] == 24);
        CHECK(mesh->deformation().influenceSets.size() == 2);
        CHECK(mesh->deformation().influenceSets[1].size() == 48);
        CHECK(mesh->deformation().morphTargets[0].positions.size() == 48);
        CHECK(mesh->subMeshes()[0].material == mesh->subMeshes()[1].material);
    }
}

TEST_CASE("truncated GLB and overflowing container lengths fail without publication")
{
    std::ifstream input("assets/models/laboratory.glb", std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    SUBCASE("truncated bytes")
    {
        bytes.resize(bytes.size() - 1);
    }
    SUBCASE("overflowing JSON chunk")
    {
        for (size_t i = 12; i < 16; ++i)
        {
            bytes[i] = char(255);
        }
    }
    SUBCASE("wrong version")
    {
        bytes[4] = 1;
    }
    FixtureFile file(Json::object());
    {
        std::ofstream output(file.path, std::ios::binary);
        output.write(bytes.data(), bytes.size());
    }
    auto model = Resources::loadResource<Model>(file.path);
    CHECK(model->isFailed());
    CHECK_THROWS_AS(model->data(), EngineError);
}

TEST_CASE("glTF and GLB load complete shared resources and convert coordinate domains")
{
    for (const auto* path : {"assets/models/laboratory.gltf", "assets/models/laboratory.glb"})
    {
        auto model = Resources::loadResourceAsync<Model>(path);
        CHECK_THROWS_AS(model->data(), EngineError);
        CHECK(Resources::loadResourceAsync<Model>(path) == model);
        model = Resources::loadResource<Model>(path);
        REQUIRE_MESSAGE(model->isLoaded(), model->error());
        const auto& data = model->data();
        REQUIRE(data.nodes.size() == 4);
        REQUIRE(data.meshes.size() == 1);
        REQUIRE(data.skins.size() == 1);
        REQUIRE(data.animations.size() == 3);
        REQUIRE(data.textures.size() == 1);
        auto mesh = data.meshes[0];
        CHECK(mesh->vertices().size() == 24);
        CHECK(mesh->indices().size() == 36);
        CHECK(mesh->indices()[0] == 0);
        CHECK(mesh->indices()[1] == 2);
        CHECK(mesh->indices()[2] == 1);
        CHECK(mesh->vertices()[0].position.z == -.5f);
        CHECK(mesh->deformation().morphTargets[0].positions[0].z == doctest::Approx(-.1));
        CHECK(mesh->deformation().morphTargets[0].normals.size() == 24);
        CHECK(mesh->deformation().defaultMorphWeights[0] == doctest::Approx(.1));
        CHECK(data.skins[0]->desc().joints[0].node == 1);
        CHECK(data.samplers[0]->desc().addressU == TextureAddressMode::MirroredRepeat);
        CHECK_FALSE(data.samplers[0]->desc().useMipmaps);
        CHECK(data.textures[0]->desc().format == TextureFormat::RGBA8UnormSrgb);
        CHECK(data.animations[0]->desc().tracks[0].interpolation == AnimationInterpolation::Step);
        CHECK(data.animations[0]->desc().tracks[0].values[5] == -1);
        const auto& cubic = data.animations[1]->desc().tracks[0];
        CHECK(cubic.interpolation == AnimationInterpolation::CubicSpline);
        CHECK(cubic.values[8] == -1);
        CHECK(cubic.values[9] == -2);
        CHECK(cubic.values[10] == 3);
        CHECK(cubic.values[11] == 4);
        CHECK(data.animations[2]->desc().tracks[0].componentsPerValue == 1);
    }
}

TEST_CASE("model instances bind source indices independently and commit atomically")
{
    auto model = Resources::loadResource<Model>("assets/models/laboratory.gltf");
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    Scene scene;
    auto existing = scene.createEntity(scene.getRoot());
    existing->setLocalPosition({10, 0, 0});
    const auto before = scene.entityCount();
    CHECK_THROWS_AS(scene.instantiateModel(model, existing, 99), EngineError);
    CHECK(scene.entityCount() == before);
    CHECK(existing->firstChild() == nullptr);
    Scene other;
    CHECK_THROWS_AS(scene.instantiateModel(model, other.getRoot()), EngineError);
    CHECK_THROWS_AS(scene.instantiateModel({}, existing), EngineError);
    auto a = scene.instantiateModel(model, existing), b = scene.instantiateModel(model, scene.getRoot());
    REQUIRE(a.animator);
    REQUIRE(b.animator);
    CHECK(a.nodes[0].get() != b.nodes[0].get());
    CHECK(a.root->firstChild() == a.nodes[2].get());
    CHECK(a.nodes[2]->firstChild() == a.nodes[0].get());
    CHECK(a.nodes[0]->nextSibling() == a.nodes[1].get());
    auto* ar = a.nodes[0]->meshRenderer();
    auto* br = b.nodes[0]->meshRenderer();
    REQUIRE(ar);
    REQUIRE(br);
    CHECK(ar->mesh() == br->mesh());
    CHECK(ar->skin() == br->skin());
    CHECK(ar->joints()[0].get() == a.nodes[1].get());
    CHECK(br->joints()[0].get() == b.nodes[1].get());
    CHECK(ar->morphWeights()[0] == .25f);
    CHECK(a.nodes[3]->meshRenderer()->morphWeights()[0] == .75f);
    ar->setMorphWeights({.9f});
    CHECK(br->morphWeights()[0] == .25f);
    scene.update();
    CHECK(a.nodes[0]->worldTransform()[3].x == doctest::Approx(9.2));
    CHECK(a.nodes[1]->localTransform().position.z == 0); // Passive clips do not evaluate.
    auto empty = scene.instantiateModel(model, scene.getRoot(), 1);
    CHECK_FALSE(empty.animator);
    CHECK(empty.root->firstChild() == nullptr);
    CHECK_FALSE(empty.nodes[0]);
    Scene moved(std::move(scene));
    CHECK(&a.nodes[0]->scene() == &moved);
    CHECK(moved.getEntity(a.nodes[0]->id()) == a.nodes[0].get());
    auto shared = ar->mesh();
    std::weak_ptr<Model> weak = model;
    model.reset();
    a.model.reset();
    b.model.reset();
    empty.model.reset();
    CHECK(weak.expired());
    CHECK(shared->isLoaded());
    CHECK(a.root);
    moved.clear();
    CHECK_FALSE(a.root);
    CHECK_FALSE(a.nodes[0]);
    CHECK_FALSE(a.animator);
    CHECK(shared->isLoaded());
}

TEST_CASE("accessors decode stride normalized signed values sparse overrides and padded matrices")
{
    auto model = accessorModel();
    auto values = readGltfAccessor(model, 0).values;
    CHECK(values[1] == doctest::Approx(127.0 / 255));
    CHECK(values[2] == 1);
    CHECK(values[3] == 1);
    model.accessors[0].componentType = TINYGLTF_COMPONENT_TYPE_BYTE;
    values = readGltfAccessor(model, 0).values;
    CHECK(values[1] == 1);
    CHECK(values[5] == -1);
    model = accessorModel();
    tinygltf::BufferView indexView;
    indexView.buffer = 0;
    indexView.byteOffset = 8;
    indexView.byteLength = 1;
    tinygltf::BufferView valueView;
    valueView.buffer = 0;
    valueView.byteOffset = 9;
    valueView.byteLength = 3;
    model.bufferViews.push_back(indexView);
    model.bufferViews.push_back(valueView);
    model.buffers[0].data.insert(model.buffers[0].data.end(), {1, 10, 20, 30});
    auto& sparse = model.accessors[0].sparse;
    sparse.isSparse = true;
    sparse.count = 1;
    sparse.indices.bufferView = 1;
    sparse.indices.byteOffset = 0;
    sparse.indices.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    sparse.values.bufferView = 2;
    sparse.values.byteOffset = 0;
    values = readGltfAccessor(model, 0).values;
    CHECK(values[3] == doctest::Approx(10.0 / 255));
    CHECK(values[5] == doctest::Approx(30.0 / 255));
    model.accessors[0].bufferView = -1;
    values = readGltfAccessor(model, 0).values;
    CHECK(values[0] == 0);
    model.buffers[0].data[8] = 2;
    CHECK_THROWS_AS(readGltfAccessor(model, 0), EngineError);
    model = accessorModel();
    model.accessors[0].type = TINYGLTF_TYPE_MAT2;
    model.accessors[0].count = 1;
    model.accessors[0].normalized = false;
    model.bufferViews[0].byteStride = 0;
    values = readGltfAccessor(model, 0).values;
    CHECK(values.size() == 4);
    CHECK(values[0] == 0);
    CHECK(values[1] == 127);
    CHECK(values[2] == 255);
    CHECK(values[3] == 0);
    model.bufferViews[0].byteLength = 6;
    model.buffers[0].data.resize(6);
    CHECK(readGltfAccessor(model, 0).values == values); // Final column needs no trailing alignment bytes.
}

TEST_CASE("skin import accepts shared matrix accessors longer than its palette and reflects translations")
{
    tinygltf::Model source;
    source.nodes.resize(1);
    tinygltf::Skin skin;
    skin.joints = {0};
    skin.inverseBindMatrices = 0;
    source.skins.push_back(skin);
    tinygltf::Buffer buffer;
    for (size_t matrix = 0; matrix < 2; ++matrix)
    {
        for (size_t element = 0; element < 16; ++element)
        {
            const float value = element == 14 ? 2.f : (element % 5 == 0 ? 1.f : 0.f);
            const auto word = std::bit_cast<uint32_t>(value);
            for (size_t byte = 0; byte < 4; ++byte)
            {
                buffer.data.push_back(static_cast<unsigned char>(word >> (byte * 8)));
            }
        }
    }
    source.buffers.push_back(buffer);
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteLength = 128;
    source.bufferViews.push_back(view);
    tinygltf::Accessor accessor;
    accessor.bufferView = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = TINYGLTF_TYPE_MAT4;
    accessor.count = 2;
    source.accessors.push_back(accessor);
    std::vector<std::string> warnings;
    auto model = importGltf(source, warnings);
    REQUIRE(model.skins.size() == 1);
    REQUIRE(model.skins[0]->desc().joints.size() == 1);
    CHECK(model.skins[0]->desc().joints[0].inverseBindMatrix[3].z == -2);
    source.accessors[0].count = 0;
    CHECK_THROWS_AS(importGltf(source, warnings), EngineError);
}

TEST_CASE("imported floating vertex colors clamp without applying position coordinate conversion")
{
    auto json = fixtureJson();
    json["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = 0;
    auto model = loadJson(json);
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    auto color = model->data().meshes[0]->vertices()[0].color;
    CHECK(color.x == 0);
    CHECK(color.y == 0);
    CHECK(color.z == .5f);
    CHECK(color.w == 1);
}

TEST_CASE("accessor boundaries fail before reading invalid ranges")
{
    auto model = accessorModel();
    SUBCASE("invalid view")
    {
        model.accessors[0].bufferView = 4;
    }
    SUBCASE("view overflow")
    {
        model.bufferViews[0].byteOffset = SIZE_MAX;
    }
    SUBCASE("accessor overflow")
    {
        model.accessors[0].byteOffset = SIZE_MAX;
    }
    SUBCASE("count overflow")
    {
        model.accessors[0].count = SIZE_MAX;
    }
    SUBCASE("stride undersized")
    {
        model.bufferViews[0].byteStride = 2;
    }
    SUBCASE("stride misaligned")
    {
        model.bufferViews[0].byteStride = 5;
    }
    SUBCASE("view truncated")
    {
        model.bufferViews[0].byteLength = 6;
    }
    SUBCASE("float normalized")
    {
        model.accessors[0].componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    }
    CHECK_THROWS_AS(readGltfAccessor(model, 0), EngineError);
}

TEST_CASE("matrix decomposition preserves negative and zero scales and rejects shear")
{
    for (auto scale :
         {math::Vec3{-2, 3, 4},
          math::Vec3{0, 3, 4},
          math::Vec3{0, 0, 4},
          math::Vec3{2, 0, 0},
          math::Vec3{0, 3, 0},
          math::Vec3{0, 0, 0}})
    {
        std::string error;
        LocalTransform original{{1, 2, 3}, *math::quatFromAxisAngle({1, 2, 3}, .8f, error), scale};
        auto matrix = parentFromLocal(original);
        tinygltf::Node node;
        for (size_t c = 0; c < 4; ++c)
        {
            for (size_t r = 0; r < 4; ++r)
            {
                node.matrix.push_back(matrix[c][r] * ((c == 2) != (r == 2) ? -1.f : 1.f));
            }
        }
        auto converted = parentFromLocal(importGltfTransform(node));
        for (size_t c = 0; c < 4; ++c)
        {
            for (size_t r = 0; r < 4; ++r)
            {
                CHECK(converted[c][r] == doctest::Approx(matrix[c][r]).epsilon(1e-5));
            }
        }
    }
    tinygltf::Node node;
    node.matrix = {1, 0, 0, 0, .5, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    CHECK_THROWS_AS(importGltfTransform(node), EngineError);
    node.matrix.clear();
    node.rotation = {0, 0, 0, 0};
    CHECK_THROWS_AS(importGltfTransform(node), EngineError);
}

TEST_CASE("invalid glTF graphs and required features fail without publication")
{
    auto json = fixtureJson();
    SUBCASE("cycle")
    {
        json["nodes"][0]["children"] = {2};
    }
    SUBCASE("duplicate child")
    {
        json["nodes"][2]["children"] = {0, 1, 0, 3};
    }
    SUBCASE("bad mesh")
    {
        json["nodes"][0]["mesh"] = 999;
    }
    SUBCASE("bad scene")
    {
        json["scene"] = 99;
    }
    SUBCASE("missing joint in scene")
    {
        json["nodes"][2]["children"] = {0, 3};
    }
    SUBCASE("bad palette")
    {
        json["skins"][0]["joints"] = {99};
    }
    SUBCASE("morph count")
    {
        json["nodes"][0]["weights"] = {1, 2};
    }
    SUBCASE("morph track target")
    {
        json["animations"][2]["channels"][0]["target"]["node"] = 1;
    }
    SUBCASE("missing dependency")
    {
        json["buffers"][0]["uri"] = "missing.bin";
    }
    SUBCASE("absolute dependency")
    {
        json["buffers"][0]["uri"] = "https://example.com/file.bin";
    }
    SUBCASE("encoded absolute dependency")
    {
        json["buffers"][0]["uri"] = "%2Ffile.bin";
    }
    SUBCASE("unknown required extension")
    {
        json["extensionsRequired"] = {"KHR_draco_mesh_compression"};
    }
    SUBCASE("line topology")
    {
        json["meshes"][0]["primitives"][0]["mode"] = 1;
    }
    SUBCASE("missing UV")
    {
        json["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 1;
    }
    SUBCASE("UV2")
    {
        json["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["texCoord"] = 2;
    }
    SUBCASE("oversized buffer")
    {
        json["buffers"][0]["byteLength"] = 67108865;
    }
    auto model = loadJson(json);
    CHECK_MESSAGE(model->isFailed(), model->error());
    CHECK_THROWS_AS(model->data(), EngineError);
}

TEST_CASE("material extensions retain factors transformed UVs and separate color data roles")
{
    auto json = fixtureJson();
    auto& material = json["materials"][0];
    material["extensions"] = {
        {"KHR_materials_unlit", Json::object()},
        {"KHR_materials_emissive_strength", {{"emissiveStrength", 2}}},
        {"KHR_materials_ior", {{"ior", 1.4}}},
        {"KHR_materials_specular",
         {{"specularFactor", .6},
          {"specularColorFactor", {.1, .2, .3}},
          {"specularTexture", {{"index", 0}}},
          {"specularColorTexture", {{"index", 0}}}}},
        {"KHR_materials_clearcoat",
         {{"clearcoatFactor", .5},
          {"clearcoatRoughnessFactor", .3},
          {"clearcoatNormalTexture", {{"index", 0}, {"scale", .4}}}}},
        {"KHR_materials_sheen", {{"sheenColorFactor", {.2, .3, .4}}, {"sheenRoughnessFactor", .7}}},
        {"KHR_materials_iridescence",
         {{"iridescenceFactor", .8},
          {"iridescenceIor", 1.6},
          {"iridescenceThicknessMinimum", 120},
          {"iridescenceThicknessMaximum", 300}}},
        {"KHR_materials_anisotropy", {{"anisotropyStrength", .9}, {"anisotropyRotation", .5}}}
    };
    material["pbrMetallicRoughness"]["baseColorTexture"]["extensions"] = {
        {"KHR_texture_transform", {{"offset", {.25, .5}}, {"scale", {2, 3}}, {"rotation", 0}}}
    };
    auto model = loadJson(json);
    REQUIRE_MESSAGE(model->isLoaded(), model->error());
    const auto& data = model->data();
    REQUIRE(data.textures.size() == 2);
    auto mat = data.materials[0];
    CHECK(mat->renderState().unlit);
    CHECK(std::get<float>(mat->uniforms().at("emissiveStrength")) == 2);
    CHECK(std::get<float>(mat->uniforms().at("ior")) == doctest::Approx(1.4));
    CHECK(std::get<float>(mat->uniforms().at("specular")) == doctest::Approx(.6));
    CHECK(std::get<float>(mat->uniforms().at("clearcoatNormalScale")) == doctest::Approx(.4));
    CHECK(std::get<float>(mat->uniforms().at("sheenRoughness")) == doctest::Approx(.7));
    CHECK(std::get<float>(mat->uniforms().at("thicknessMinimum")) == 120);
    CHECK(std::get<float>(mat->uniforms().at("anisotropy")) == doctest::Approx(.9));
    CHECK(std::get<math::Vec3>(mat->uniforms().at("baseColorTransformX")).z == .25f);
    CHECK(
        bindingTexture(mat->textures().at("baseColorTexture")) ==
        bindingTexture(mat->textures().at("specularColorTexture"))
    );
    CHECK(bindingTexture(mat->textures().at("specularTexture"))->desc().format == TextureFormat::RGBA8Unorm);
    CHECK(mat->samplers().at("baseColorSampler") == mat->samplers().at("specularSampler"));
}

TEST_CASE("model cancellation retry optional extensions and no-scene inspection")
{
    auto json = fixtureJson();
    json["buffers"][0]["uri"] = "missing.bin";
    FixtureFile file(json);
    auto pending = Resources::loadResourceAsync<Model>(file.path);
    Resources::update();
    Resources::update();
    std::weak_ptr<Model> weak = pending;
    pending.reset();
    CHECK(weak.expired());
    Resources::update();
    auto failed = Resources::loadResource<Model>(file.path);
    REQUIRE(failed->isFailed());
    CHECK(failed->error().find("missing.bin") != std::string::npos);
    failed.reset();
    json = fixtureJson();
    json["extensionsUsed"] = {"OFG_optional_test"};
    json.erase("scenes");
    json.erase("scene");
    {
        std::ofstream rewrite(file.path);
        rewrite << json.dump();
    }
    auto retry = Resources::loadResource<Model>(file.path);
    REQUIRE_MESSAGE(retry->isLoaded(), retry->error());
    CHECK_FALSE(retry->warnings().empty());
    CHECK(retry->data().scenes.empty());
    Scene scene;
    CHECK_THROWS_AS(scene.instantiateModel(retry, scene.getRoot()), EngineError);
    CHECK(scene.entityCount() == 1);
}
