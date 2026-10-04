// Existing Render Settings controls plus outdoor laboratory values; Workspace owns transaction/reset behavior.
#include "ui/render-settings-panel.h"
#include "render/graphics.h"
#include <imgui.h>
#include <algorithm>
#include <numbers>
namespace ofg {
bool drawLightingControls(Lighting& draft)
{
    bool changed = false;
    ImGui::PushItemWidth(-1);
    ImGui::SeparatorText("Output");
    ImGui::TextUnformatted("Exposure (multiplier)");
    changed |= ImGui::DragFloat("##Exposure", &draft.exposure, 0.02f, 0, 0, "%.3f");
    int mapping = draft.linearOutput ? 1 : 0;
    ImGui::TextUnformatted("Tone mapping");
    if (ImGui::Combo("##Tone mapping", &mapping, "PBR Neutral\0Linear diagnostic\0"))
    {
        draft.linearOutput = mapping == 1;
        changed = true;
    }
    int view = int(draft.debugView);
    ImGui::TextUnformatted("Debug view");
    if (ImGui::Combo("##Debug view", &view, "Shaded\0Normals\0Roughness\0Metallic\0Base color\0"))
    {
        draft.debugView = view;
        changed = true;
    }
    ImGui::SeparatorText("Environment");
    ImGui::TextUnformatted("Intensity");
    changed |= ImGui::DragFloat("##Environment intensity", &draft.environmentIntensity, 0.02f, 0, 0, "%.3f");
    ImGui::BeginDisabled(draft.outdoor.has_value());
    float degrees = draft.environmentRotation * 180 / std::numbers::pi_v<float>;
    ImGui::TextUnformatted("Rotation (degrees)");
    if (ImGui::DragFloat("##Environment rotation", &degrees, 0.5f))
    {
        draft.environmentRotation = degrees * std::numbers::pi_v<float> / 180;
        changed = true;
    }
    ImGui::EndDisabled();
    if (draft.outdoor)
    {
        ImGui::TextWrapped("Outdoor IBL follows the world sky. Slot 1 is the sun or moon.");
    }
    for (uint32_t i = draft.outdoor ? 1u : 0u; i < std::min(draft.lightCount, uint32_t(draft.lights.size())); ++i)
    {
        ImGui::PushID(int(i));
        auto& light = draft.lights[i];
        const char* type = light.type == LightType::Directional ? "Directional"
                           : light.type == LightType::Point     ? "Point"
                                                                : "Spot";
        if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen, "%s light %u", type, i + 1))
        {
            ImGui::TextUnformatted("Colour (linear)");
            changed |=
                ImGui::ColorEdit3("##Colour", &light.color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            ImGui::TextUnformatted(light.type == LightType::Directional ? "Intensity (lux)" : "Intensity (candela)");
            changed |= ImGui::DragFloat("##Light intensity", &light.intensity, 0.1f, 0, 0, "%.3f");
            if (light.type != LightType::Directional)
            {
                ImGui::TextUnformatted("Position (m)");
                changed |= ImGui::DragFloat3("##Position", &light.position.x, 0.05f);
                ImGui::TextUnformatted("Range (m, 0 = infinite)");
                changed |= ImGui::DragFloat("##Range", &light.range, 0.1f);
            }
            if (light.type != LightType::Point)
            {
                ImGui::TextUnformatted("Travel direction");
                changed |= ImGui::DragFloat3("##Direction", &light.direction.x, 0.01f);
            }
            if (light.type == LightType::Spot)
            {
                float angles[]{
                    light.innerCone * 180 / std::numbers::pi_v<float>,
                    light.outerCone * 180 / std::numbers::pi_v<float>
                };
                ImGui::TextUnformatted("Inner / outer cone (degrees)");
                if (ImGui::DragFloat2("##Cone", angles, 0.25f))
                {
                    light.innerCone = angles[0] * std::numbers::pi_v<float> / 180;
                    light.outerCone = angles[1] * std::numbers::pi_v<float> / 180;
                    changed = true;
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }


    ImGui::PopItemWidth();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .45f);
    bool enabled = draft.outdoor.has_value();
    if (ImGui::Checkbox("Procedural outdoor lighting", &enabled))
    {
        draft.outdoor = enabled ? std::optional<OutdoorLighting>{OutdoorLighting{}} : std::nullopt;
        changed = true;
    }
    if (draft.outdoor)
    {
        auto& s = *draft.outdoor;
        if (ImGui::CollapsingHeader("Day / night", ImGuiTreeNodeFlags_DefaultOpen))
        {
            float hours = float(s.dayCycle.timeHours);
            if (ImGui::SliderFloat("Time (hours)", &hours, 0, 24, "%.2f"))
            {
                s.dayCycle.timeHours = hours;
                changed = true;
            }
            changed |= ImGui::Checkbox("Pause cycle and wind", &s.dayCycle.paused);
            changed |= ImGui::SliderFloat("Cycle (seconds)", &s.dayCycle.cycleSeconds, 60, 3600);
            changed |= ImGui::SliderAngle("Noon elevation", &s.dayCycle.noonElevation, 1, 90);
            changed |= ImGui::SliderAngle("Path azimuth", &s.dayCycle.azimuth, -180, 180);
            const char* names[]{"Noon", "Sunset", "Twilight", "Midnight"};
            const double times[]{12, 17.9, 18.5, 0};
            for (int i = 0; i < 4; ++i)
            {
                if (i)
                {
                    ImGui::SameLine();
                }
                if (ImGui::Button(names[i]))
                {
                    s.dayCycle.timeHours = times[i];
                    s.dayCycle.paused = true;
                    changed = true;
                }
            }
            changed |= ImGui::Checkbox("Visible celestial disks", &s.showDisks);
            changed |= ImGui::SliderFloat("Sun (lux)", &s.atmosphere.sunIlluminance, 0, 200000, "%.0f");
            changed |= ImGui::SliderFloat("Moon (lux)", &s.atmosphere.moonIlluminance, 0, 10, "%.3f");
            if (validOutdoorLighting(s))
            {
                auto f = evaluateOutdoorLighting(s, 0);
                ImGui::Text("Active source: %s (%.3f lux)", f.moon ? "Moon" : "Sun", f.illuminance);
            }
        }
        if (ImGui::CollapsingHeader("Atmosphere and clouds", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::SliderFloat("Rayleigh density", &s.atmosphere.rayleigh, 0, 4);
            changed |= ImGui::SliderFloat("Aerosol density", &s.atmosphere.mie, 0, 8);
            changed |= ImGui::SliderFloat("Ozone density", &s.atmosphere.ozone, 0, 4);
            changed |= ImGui::SliderFloat("Ground albedo", &s.atmosphere.groundAlbedo, 0, 1);
            changed |= ImGui::SliderFloat("Haze distance (m)", &s.atmosphere.aerialDistance, 100, 20000);
            changed |= ImGui::SliderFloat("Cloud coverage", &s.clouds.coverage, 0, 1);
            changed |= ImGui::SliderFloat("Cloud optical thickness", &s.clouds.opticalThickness, 0, 10);
            changed |= ImGui::SliderFloat("Wind speed (m/s)", &s.clouds.windSpeed, 0, 100);
            changed |= ImGui::SliderAngle("Wind direction", &s.clouds.windAngle, -180, 180);
            if (ImGui::Button("Clear"))
            {
                s.clouds.coverage = 0;
                s.atmosphere.mie = 1;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Overcast"))
            {
                s.clouds.coverage = .95f;
                s.clouds.opticalThickness = 4;
                s.atmosphere.mie = 2;
                changed = true;
            }
        }
        if (ImGui::CollapsingHeader("Outdoor diagnostics"))
        {
            auto stats = Graphics::outdoorDiagnostics();
            ImGui::Text(
                "IBL update: %u / 24 steps, %u passes this frame",
                stats.environmentSteps,
                stats.environmentPasses
            );
            if (!stats.ready)
            {
                ImGui::TextUnformatted("Building first environment; direct lighting remains active.");
            }
            if (stats.ready)
            {
                ImGui::Text(
                    "IBL snapshot age: %.2f s, blend %.2f",
                    stats.environmentAgeSeconds,
                    stats.environmentBlend
                );
                ImGui::TextUnformatted("Capture 128, filtered 64; 24 frames/update, 0.1 s transition");
                ImGui::Text(
                    "Cascade ends: %.1f / %.1f / %.1f / %.1f m",
                    stats.cascadeDistances[0],
                    stats.cascadeDistances[1],
                    stats.cascadeDistances[2],
                    stats.cascadeDistances[3]
                );
                ImGui::Text(
                    "Shadow texture payload: %.1f MiB",
                    16.0 * stats.shadowResolution * stats.shadowResolution / (1024 * 1024)
                );
                ImGui::TextWrapped(
                    "Sky and dynamic IBL payload: 4.63 MiB. Payload excludes driver alignment, shared BRDF lookup and "
                    "in-flight retirement."
                );
            }
        }
        if (ImGui::CollapsingHeader("Outdoor exposure"))
        {
            changed |= ImGui::Checkbox("Time-of-day exposure", &s.exposure.automatic);
            changed |= ImGui::SliderFloat("Manual EV", &s.exposure.manualEv, -2, 20);
            changed |= ImGui::SliderFloat("Day EV", &s.exposure.dayEv, 0, 20);
            changed |= ImGui::SliderFloat("Twilight EV", &s.exposure.twilightEv, -2, 15);
            changed |= ImGui::SliderFloat("Night EV", &s.exposure.nightEv, -5, 10);
        }
        if (ImGui::CollapsingHeader("Cascaded shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= ImGui::Checkbox("Enable shadows", &s.shadows.enabled);
            int resolution = s.shadows.resolution == 2048 ? 1 : 0;
            if (ImGui::Combo(
                    "Resolution",
                    &resolution,
                    "1024 (16 MiB)\0"
                    "2048 (64 MiB)\0"
                ))
            {
                s.shadows.resolution = resolution ? 2048 : 1024;
                changed = true;
            }
            changed |= ImGui::SliderFloat("Shadow distance (m)", &s.shadows.distance, 10, 1000);
            changed |= ImGui::SliderFloat("Split lambda", &s.shadows.splitLambda, 0, 1);
            changed |= ImGui::SliderFloat("Transition fraction", &s.shadows.transition, .01f, .3f);
            changed |= ImGui::SliderFloat("Normal bias (texels)", &s.shadows.normalBias, 0, 3);
            changed |= ImGui::SliderFloat("Depth bias", &s.shadows.depthBias, 0, .005f, "%.5f");
            int debug = int(s.shadows.debugView);
            if (ImGui::Combo("Shadow debug", &debug, "Shaded\0Cascades\0Visibility\0"))
            {
                s.shadows.debugView = uint32_t(debug);
                changed = true;
            }
        }
    }
    ImGui::PopItemWidth();
    return changed;
}
} // namespace ofg
