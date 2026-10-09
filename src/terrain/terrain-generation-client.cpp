// Concrete HTTP job control with idempotent submissions and bounded polling, independent of rendering.
#include "terrain/terrain-generation-client.h"
#include "terrain/terrain-content.h"
#include "core/engine-error.h"
#include <json.hpp>
#include <random>

namespace ofg::terrain {
IslandRecipe decodeIslandRecipe(std::string_view text)
{
    const auto j = nlohmann::json::parse(text);
    IslandRecipe r;
    r.seed = std::stoull(j.at("seed").get<std::string>());
    r.regionX = std::stoll(j.at("region_x").get<std::string>());
    r.regionZ = std::stoll(j.at("region_z").get<std::string>());
    r.seedSpacing = j.at("seed_spacing");
    r.sourceSpacing = j.at("source_spacing");
    r.sourceIntervals = j.at("source_intervals");
    r.jitter = j.at("jitter");
    r.oceanClearance = j.at("ocean_clearance");
    r.contourRounding = j.at("contour_rounding");
    r.contourVariation = j.at("contour_variation");
    r.plateauHeight = j.at("plateau_height");
    r.seaLevel = j.at("sea_level");
    r.seabedHeight = j.at("seabed_height");
    r.coastWidth = j.at("coast_width");
    return r;
}
std::string encodeIslandRecipe(const IslandRecipe& r)
{
    return nlohmann::json{
        {"seed", std::to_string(r.seed)},
        {"region_x", std::to_string(r.regionX)},
        {"region_z", std::to_string(r.regionZ)},
        {"seed_spacing", r.seedSpacing},
        {"source_spacing", r.sourceSpacing},
        {"source_intervals", r.sourceIntervals},
        {"jitter", r.jitter},
        {"ocean_clearance", r.oceanClearance},
        {"contour_rounding", r.contourRounding},
        {"contour_variation", r.contourVariation},
        {"plateau_height", r.plateauHeight},
        {"sea_level", r.seaLevel},
        {"seabed_height", r.seabedHeight},
        {"coast_width", r.coastWidth}
    }.dump();
}
TerrainGenerationClient::TerrainGenerationClient(std::string baseUrl, std::string island)
    : m_baseUrl(std::move(baseUrl))
    , m_island(std::move(island))
{
}

void TerrainGenerationClient::request(std::string url, std::string body)
{
    m_lastUrl = std::move(url);
    m_lastBody = std::move(body);
    m_request = HttpRequest::start(m_lastUrl, 256 << 10, true, m_lastBody);
    error.clear();
    fieldErrors.clear();
}
void TerrainGenerationClient::regenerate()
{
    submit(draft);
}
void TerrainGenerationClient::startErosion(std::string_view initialRevision, const IslandRecipe& initialRecipe)
{
    const auto& e = erosion;
    const auto json = nlohmann::json{
        {"initial_revision", initialRevision},
        {"timestep_years", e.timestepYears},
        {"steps", e.steps},
        {"uplift_m_per_year", e.uplift},
        {"incision", e.incision},
        {"sediment_incision", e.sedimentIncision},
        {"diffusivity", e.diffusivity},
        {"deposition", e.deposition},
        {"marine_diffusivity", e.marineDiffusivity},
        {"precipitation", e.precipitation},
        {"perturbation_m", e.perturbation},
        {"preview_steps", e.previewSteps},
        {"preview_seconds", e.previewSeconds},
        {"preview_stride", e.previewStride},
        {"start_paused", true}
    }.dump();
    submit(initialRecipe, json);
}
void TerrainGenerationClient::submit(const IslandRecipe& recipe, std::string_view erosionJson)
{
    if (active())
        throw EngineError("Wait for or cancel the current generation job before regenerating.");
    std::random_device random;
    std::string operation;
    for (int i = 0; i < 32; ++i)
        operation += "0123456789abcdef"[random() & 15];
    auto body = nlohmann::json{
        {"island", m_island},
        {"operation_id", operation},
        {"parameters", nlohmann::json::parse(encodeIslandRecipe(recipe))}
    };
    if (!erosionJson.empty())
        body["erosion"] = nlohmann::json::parse(erosionJson);
    jobId.clear();
    state.clear();
    m_submitting = true;
    request(m_baseUrl + "/v1/jobs", body.dump());
}
void TerrainGenerationClient::control(std::string_view action)
{
    if (jobId.empty() || (action != "cancel" && action != "pause" && action != "resume" && action != "step"))
        throw EngineError("Invalid terrain job control.");
    request(m_baseUrl + "/v1/jobs/" + jobId + "/" + std::string(action), "{}");
}
void TerrainGenerationClient::retryRequest()
{
    // Job creation has an operation ID. Controls do not: after an ambiguous Step response,
    // observe status instead of accidentally advancing the live simulation a second time.
    if (!jobId.empty() && m_lastUrl != m_baseUrl + "/v1/jobs")
        request(m_baseUrl + "/v1/jobs/" + jobId);
    else if (!m_lastUrl.empty())
        request(m_lastUrl, m_lastBody);
}
bool TerrainGenerationClient::active() const
{
    return (m_submitting && m_request) || state == "queued" || state == "running" || state == "cancelling" ||
           state == "paused" || state == "pausing";
}
void TerrainGenerationClient::update()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_request)
    {
        auto response = m_request->take();
        if (!response)
            return;
        m_request.reset();
        try
        {
            if (!response->error.empty())
                throw EngineError(response->error);
            const auto j = nlohmann::json::parse(
                std::string_view(reinterpret_cast<const char*>(response->bytes.data()), response->bytes.size())
            );
            if (response->status < 200 || response->status >= 300)
            {
                if (j.contains("detail") && j["detail"].is_array())
                    for (const auto& detail : j["detail"])
                    {
                        const auto& location = detail.at("loc");
                        const auto field = location.empty() ? "parameters" : location.back().get<std::string>();
                        fieldErrors[field] = detail.at("msg").get<std::string>();
                    }
                throw EngineError("Generator HTTP " + std::to_string(response->status) + ": " + j.dump());
            }
            jobId = j.at("id").get<std::string>();
            if (jobId.size() != 32 || jobId.find_first_not_of("0123456789abcdef") != std::string::npos)
                throw EngineError("Invalid generation job identity.");
            state = j.at("state").get<std::string>();
            phase = j.at("phase").get<std::string>();
            progress = j.at("progress").get<double>();
            accepted = decodeIslandRecipe(j.at("parameters").dump());
            erosionJob = j.contains("erosion") && j["erosion"].is_object();
            step = j.value("step", 0);
            years = j.value("years", 0.0);
            if (j.contains("revision") && j["revision"].is_string())
                latestRevision = decodeTerrainRevision(j.dump());
            if (j.contains("error") && j["error"].is_string())
                error = j["error"].get<std::string>();
            m_submitting = false;
        } catch (const std::exception& failure)
        {
            error = failure.what();
        }
        m_nextPoll = now + std::chrono::milliseconds(500);
    }
    if (!m_request && active() && error.empty() && now >= m_nextPoll && !jobId.empty())
        request(m_baseUrl + "/v1/jobs/" + jobId);
}
} // namespace ofg::terrain
