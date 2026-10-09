// Laboratory generation controls and physical recipe values; content acquisition remains a separate service.
#pragma once
#include "platform/http-client.h"
#include <chrono>
#include <map>
#include <string_view>

namespace ofg::terrain {
struct IslandRecipe
{
    uint64_t seed = 1;
    int64_t regionX = 0, regionZ = 0;
    int seedSpacing = 4096, sourceSpacing = 32, sourceIntervals = 512;
    double jitter = 512, oceanClearance = 256, contourRounding = .35, contourVariation = .1;
    double plateauHeight = 40, seaLevel = 0, seabedHeight = -80, coastWidth = 256;
};
struct ErosionRecipe
{
    double timestepYears = 1000, uplift = .001, incision = .00002, sedimentIncision = .00004;
    double diffusivity = .1, deposition = 1, marineDiffusivity = 0, precipitation = 1, perturbation = 1;
    int steps = 100, previewSteps = 10, previewStride = 1;
    double previewSeconds = 1;
};
// Decodes a complete server-validated recipe while preserving exact integer world identifiers.
IslandRecipe decodeIslandRecipe(std::string_view json);
// Encodes draft physical parameters; authoritative geometric validation is performed by the service.
std::string encodeIslandRecipe(const IslandRecipe& recipe);

class TerrainGenerationClient
{
public:
    // Captures the same service/island identity used by terrain content; controls always bypass caches.
    TerrainGenerationClient(std::string baseUrl, std::string island);
    // Starts one immutable job with a fresh operation ID, leaving the draft independent of accepted input.
    void regenerate();
    // Starts a paused, independent experiment from explicit displayed source data and its matching recipe.
    void startErosion(std::string_view initialRevision, const IslandRecipe& initialRecipe);
    // Sends an explicit job action; only cancel/pause/resume/step are accepted.
    void control(std::string_view action);
    // Retries idempotent submission; ambiguous controls refresh status without repeating a numerical Step.
    void retryRequest();
    // Advances HTTP completion and polls active jobs at most twice per second without blocking.
    void update();
    // Reports whether the current operation is nonterminal or an initial submission is in flight.
    bool active() const;

    IslandRecipe draft, accepted;
    ErosionRecipe erosion;
    bool erosionJob = false;
    int step = 0;
    double years = 0;
    std::string jobId, state, phase, latestRevision, error;
    std::map<std::string, std::string> fieldErrors;
    double progress = 0;

private:
    // Starts a bounded control request and retains its exact identity for explicit retry.
    void request(std::string url, std::string body = {});
    // Assigns an operation identity and submits a flat or erosion recipe without duplicating HTTP ownership.
    void submit(const IslandRecipe& recipe, std::string_view erosionJson = {});
    std::string m_baseUrl, m_island, m_lastUrl, m_lastBody;
    std::unique_ptr<HttpRequest> m_request;
    std::chrono::steady_clock::time_point m_nextPoll;
    bool m_submitting = false;
};
} // namespace ofg::terrain
