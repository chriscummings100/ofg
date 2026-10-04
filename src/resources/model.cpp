// Cooperative model/dependency reads followed by private tinygltf parsing and atomic CPU resource publication.
#define TINYGLTF_IMPLEMENTATION
#include "resources/gltf-import.h"
#include "resources/asset-read.h"
#include <json.hpp>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Resolve against the document URL, not the page's asset directory; the caller releases returned UTF-8 storage.
// clang-format off
EM_JS(char*, resolveModelUri, (const char* model, const char* uri), {
    try { return stringToNewUTF8(new URL(UTF8ToString(uri), new URL(UTF8ToString(model), document.baseURI)).href); }
    catch (error) { return 0; }
});
// clang-format on
#endif

namespace ofg {
namespace {
constexpr size_t encodedBudget = 256 * 1024 * 1024;
// Decodes one URI path without treating '+' as a space, rejecting invalid escapes and embedded NULs.
std::string decodeUri(const std::string& uri)
{
    std::string decoded;
    // Converts a hexadecimal escape digit or fails rather than substituting a different filename.
    const auto hex = [](char c) -> unsigned
    {
        if (c >= '0' && c <= '9')
        {
            return unsigned(c - '0');
        }
        if (c >= 'a' && c <= 'f')
        {
            return unsigned(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F')
        {
            return unsigned(c - 'A' + 10);
        }
        throw EngineError("Invalid glTF URI escape.");
    };
    for (size_t i = 0; i < uri.size(); ++i)
    {
        char c = uri[i];
        if (c == '%')
        {
            if (uri.size() - i < 3)
            {
                throw EngineError("Truncated glTF URI escape.");
            }
            c = char(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
            i += 2;
        }
        if (!c)
        {
            throw EngineError("glTF URI contains a NUL byte.");
        }
        decoded += c;
    }
    return decoded;
}
// Resolves supported relative dependency URIs for native files or the browser URL loader.
std::string dependencyLocation(const std::string& model, const std::string& uri)
{
    const auto path = decodeUri(uri.substr(0, uri.find_first_of("?#")));
    if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos ||
        path.find('\\') != std::string::npos)
    {
        throw EngineError("Model dependency must be a relative URI: " + uri);
    }
#ifdef __EMSCRIPTEN__
    std::unique_ptr<char, decltype(&std::free)> resolved(resolveModelUri(model.c_str(), uri.c_str()), std::free);
    if (!resolved)
    {
        throw EngineError("Could not resolve model dependency URI: " + uri);
    }
    return resolved.get();
#else
    const auto resolved =
        (std::filesystem::u8path(model).parent_path() / std::filesystem::u8path(path)).lexically_normal();
    const auto utf8 = resolved.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#endif
}
// Reads little-endian GLB header/chunk lengths after the caller establishes four available bytes.
uint32_t glbWord(const unsigned char* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
// Borrows the JSON portion of a checked GLB or a plain glTF document.
std::span<const unsigned char> documentJson(std::span<const unsigned char> bytes, bool binary)
{
    if (!binary)
    {
        return bytes;
    }
    if (bytes.size() < 20 || glbWord(bytes.data() + 4) != 2 || glbWord(bytes.data() + 8) != bytes.size())
    {
        throw EngineError("Invalid GLB version or container length.");
    }
    size_t cursor = 12;
    bool hasBinary = false;
    std::span<const unsigned char> json;
    while (cursor < bytes.size())
    {
        if (bytes.size() - cursor < 8)
        {
            throw EngineError("Truncated GLB chunk header.");
        }
        const size_t length = glbWord(bytes.data() + cursor);
        const auto type = glbWord(bytes.data() + cursor + 4);
        if (length % 4 || length > bytes.size() - cursor - 8)
        {
            throw EngineError("Invalid GLB chunk length.");
        }
        if (cursor == 12)
        {
            if (type != 0x4e4f534a || !length)
            {
                throw EngineError("GLB must start with JSON.");
            }
            json = bytes.subspan(cursor + 8, length);
        }
        else if (type == 0x4e4f534a)
        {
            throw EngineError("GLB contains duplicate JSON chunks.");
        }
        if (type == 0x004e4942)
        {
            if (hasBinary || cursor != 20 + json.size())
            {
                throw EngineError("GLB BIN chunk must be unique and second.");
            }
            hasBinary = true;
        }
        cursor += 8 + length;
    }
    return json;
}
// Checks the supported extension profile before fetching files or constructing resources.
void inspectExtensions(const nlohmann::json& json, std::vector<std::string>& warnings)
{
    const std::set<std::string> supported{
        "KHR_materials_unlit",
        "KHR_materials_emissive_strength",
        "KHR_materials_ior",
        "KHR_materials_specular",
        "KHR_materials_clearcoat",
        "KHR_materials_sheen",
        "KHR_materials_iridescence",
        "KHR_materials_anisotropy",
        "KHR_texture_transform"
    };
    for (const auto& extension : json.value("extensionsRequired", nlohmann::json::array()))
    {
        const auto name = extension.get<std::string>();
        if (!supported.contains(name))
        {
            throw EngineError("Unsupported required glTF extension: " + name);
        }
    }
    for (const auto& extension : json.value("extensionsUsed", nlohmann::json::array()))
    {
        const auto name = extension.get<std::string>();
        if (!supported.contains(name))
        {
            warnings.push_back("Ignored optional extension (core fallback only): " + name);
        }
    }
}
} // namespace

struct ModelLoad
{
    std::unique_ptr<AssetRead> read;
    std::vector<unsigned char> document;
    std::vector<std::pair<std::string, std::string>> dependencies; // Source URI, resolved location.
    std::map<std::string, std::vector<unsigned char>> files;       // Resolved location -> prepared bytes.
    std::map<std::string, std::string> locations;                  // Exact parser URI -> resolved location.
    size_t nextDependency = 0;
    size_t encodedBytes = 0;
    bool binary = false;

    // Checks JSON buffer declarations and discovers dependencies without synchronously opening them in tinygltf.
    void inspect(const std::string& key, std::vector<std::string>& warnings)
    {
        binary = document.size() >= 4 && glbWord(document.data()) == 0x46546c67;
        const auto bytes = documentJson(document, binary);
        const auto* jsonBegin = reinterpret_cast<const char*>(bytes.data());
        const auto json = nlohmann::json::parse(jsonBegin, jsonBegin + bytes.size());
        if (!json.is_object() || json.at("asset").at("version") != "2.0")
        {
            throw EngineError("Model requires glTF 2.0.");
        }
        if (json.at("asset").contains("minVersion") && json.at("asset").at("minVersion") != "2.0")
        {
            throw EngineError("Unsupported glTF minimum version.");
        }
        inspectExtensions(json, warnings);
        size_t declaredBytes = 0;
        std::set<std::string> seen;
        for (const char* table : {"buffers", "images"})
        {
            const auto entries = json.value(table, nlohmann::json::array());
            if (!entries.is_array())
            {
                throw EngineError("glTF buffers/images must be arrays.");
            }
            for (const auto& entry : entries)
            {
                if (std::string_view(table) == "buffers")
                {
                    const auto length = entry.at("byteLength");
                    if (!length.is_number_unsigned() || !length.get<uint64_t>() ||
                        length.get<uint64_t>() > maxEncodedBytes)
                    {
                        throw EngineError("Invalid glTF buffer size (limit 64 MiB).");
                    }
                    const size_t size = length.get<size_t>();
                    if (size > encodedBudget - declaredBytes)
                    {
                        throw EngineError("Model buffer declarations exceed 256 MiB.");
                    }
                    declaredBytes += size;
                }
                if (!entry.contains("uri"))
                {
                    continue;
                }
                const auto uri = entry.at("uri").get<std::string>();
                if (uri.starts_with("data:"))
                {
                    continue;
                }
                const auto location = dependencyLocation(key, uri);
                locations[uri] = location;
                if (seen.insert(location).second)
                {
                    dependencies.emplace_back(uri, location);
                }
            }
        }
    }

    // Provides a complete read-only virtual filesystem; callbacks never start I/O or silently fall back to disk.
    tinygltf::FsCallbacks callbacks()
    {
        tinygltf::FsCallbacks fs;
        fs.user_data = this;
        // Looks up exact source URIs; the empty parser base directory prevents accidental path reinterpretation.
        fs.FileExists = [](const std::string& path, void* data)
        {
            const auto& load = *static_cast<ModelLoad*>(data);
            return load.locations.contains(path) && load.files.contains(load.locations.at(path));
        };
        // No environment or filesystem expansion is permitted.
        fs.ExpandFilePath = [](const std::string& path, void*)
        {
            return path;
        };
        // Copies a previously fetched dependency into parser-owned storage.
        fs.ReadWholeFile = [](std::vector<unsigned char>* out, std::string*, const std::string& path, void* data)
        {
            const auto& load = *static_cast<ModelLoad*>(data);
            const auto found = load.locations.find(path);
            if (found == load.locations.end())
            {
                return false;
            }
            *out = load.files.at(found->second);
            return true;
        };
        // This importer never writes through tinygltf.
        fs.WriteWholeFile = [](std::string*, const std::string&, const std::vector<unsigned char>&, void*)
        {
            return false;
        };
        // Reports actual prepared bytes, not untrusted JSON buffer declarations.
        fs.GetFileSizeInBytes = [](size_t* out, std::string*, const std::string& path, void* data)
        {
            const auto& load = *static_cast<ModelLoad*>(data);
            const auto found = load.locations.find(path);
            if (found == load.locations.end())
            {
                return false;
            }
            *out = load.files.at(found->second).size();
            return true;
        };
        return fs;
    }

    // Parses prepared memory once, retaining encoded image bytes for OFG's bounded shared decoder.
    tinygltf::Model parse(std::vector<std::string>& warnings)
    {
        tinygltf::TinyGLTF parser;
        std::string error, warning;
        if (!parser.SetFsCallbacks(callbacks(), &error))
        {
            throw EngineError(error);
        }
        tinygltf::URICallbacks uri{};
        // Keep source spelling for lookup; native/browser URI resolution was already performed during discovery.
        uri.decode = [](const std::string& in, std::string* out, void*)
        {
            *out = in;
            return true;
        };
        if (!parser.SetURICallbacks(uri, &error))
        {
            throw EngineError(error);
        }
        // Preserve compressed image bytes; decoding and color-role selection are OFG's responsibilities.
        parser.SetImageLoader(
            [](tinygltf::Image* image,
               int,
               std::string*,
               std::string*,
               int,
               int,
               const unsigned char* bytes,
               int size,
               void*)
            {
                if (size <= 0 || size_t(size) > maxEncodedBytes)
                {
                    return false;
                }
                image->image.assign(bytes, bytes + size);
                image->as_is = true;
                return true;
            },
            nullptr
        );
        tinygltf::Model result;
        const bool success = binary ? parser.LoadBinaryFromMemory(
                                          &result,
                                          &error,
                                          &warning,
                                          document.data(),
                                          unsigned(document.size()),
                                          "",
                                          0
                                      )
                                    : parser.LoadASCIIFromString(
                                          &result,
                                          &error,
                                          &warning,
                                          reinterpret_cast<const char*>(document.data()),
                                          unsigned(document.size()),
                                          "",
                                          0
                                      );
        if (!warning.empty())
        {
            warnings.push_back(warning);
        }
        if (!success)
        {
            throw EngineError("glTF parse failed: " + error);
        }
        return result;
    }
};

Model::Model(std::string key)
    : Resource(std::move(key))
{
}
Model::~Model() = default;

const ModelData& Model::data() const
{
    if (!isLoaded())
    {
        throw EngineError("Model data is not ready: " + key());
    }
    return m_data;
}

bool Model::loadStep()
{
    try
    {
        if (!m_load)
        {
            m_load = std::make_unique<ModelLoad>();
            m_load->read = std::make_unique<AssetRead>();
            m_load->read->start(key());
            return false;
        }
        auto& load = *m_load;
        if (load.document.empty())
        {
            if (!load.read->poll())
            {
                return false;
            }
            const auto bytes = load.read->data();
            load.document.assign(
                reinterpret_cast<const unsigned char*>(bytes.data()),
                reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size()
            );
            load.encodedBytes = bytes.size();
            load.read.reset();
            load.inspect(key(), m_warnings);
            return false;
        }
        if (load.nextDependency < load.dependencies.size())
        {
            const auto& [uri, location] = load.dependencies[load.nextDependency];
            try
            {
                if (!load.read)
                {
                    load.read = std::make_unique<AssetRead>();
                    load.read->start(location);
                    return false;
                }
                if (!load.read->poll())
                {
                    return false;
                }
                const auto bytes = load.read->data();
                if (bytes.size() > encodedBudget - load.encodedBytes)
                {
                    throw EngineError("Model exceeds 256 MiB encoded budget.");
                }
                load.encodedBytes += bytes.size();
                load.files[location].assign(
                    reinterpret_cast<const unsigned char*>(bytes.data()),
                    reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size()
                );
                load.read.reset();
                ++load.nextDependency;
                return false;
            } catch (const std::exception& error)
            {
                throw EngineError("Dependency '" + uri + "': " + error.what());
            }
        }
        const auto source = load.parse(m_warnings);
        auto data = importGltf(source, m_warnings);
        m_data = std::move(data);
        m_load.reset();
        return true;
    } catch (const std::exception& error)
    {
        m_load.reset();
        throw EngineError("Model " + key() + ": " + error.what());
    }
}
} // namespace ofg
