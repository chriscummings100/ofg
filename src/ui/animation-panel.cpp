// Concrete animator transport controls; no animation graph or render-side pose editing.
#include "ui/workspace.h"
#include "core/engine-error.h"

namespace ofg {
namespace {
// Includes entity identity so similarly named instances remain selectable.
std::string animatorLabel(const Animator& animator)
{
    const auto* entity = animator.entity();
    return (entity->name().empty() ? "Animator" : entity->name()) + " [" + std::to_string(entity->id()) + "]";
}
// Source indices disambiguate unnamed and duplicate clip names without changing immutable assets.
std::string clipLabel(const Animation& clip, size_t index)
{
    return (clip.desc().name.empty() ? "Unnamed clip" : clip.desc().name) + " [" + std::to_string(index) + "]";
}
} // namespace

void Workspace::animationPanel(Scene& scene)
{
    if (!m_showAnimation)
    {
        return;
    }
    if (!m_animator && !scene.animators().empty())
    {
        m_animator = scene.animators().front().get();
    }
    if (ImGui::Begin("Animation", &m_showAnimation))
    {
        if (ImGui::BeginCombo("Animator", m_animator ? animatorLabel(*m_animator).c_str() : "No animators"))
        {
            for (const auto& animator : scene.animators())
            {
                if (ImGui::Selectable(animatorLabel(*animator).c_str(), m_animator.get() == animator.get()))
                {
                    m_animator = animator.get();
                    m_animationError.clear();
                }
            }
            ImGui::EndCombo();
        }
        if (m_animator)
        {
            auto& animator = *m_animator;
            auto selected = animator.selectedAnimation();
            const auto& clips = animator.animations();
            if (ImGui::BeginCombo("Clip", selected ? clipLabel(*clips[*selected], *selected).c_str() : "Select a clip"))
            {
                for (size_t i = 0; i < clips.size(); ++i)
                {
                    if (ImGui::Selectable(clipLabel(*clips[i], i).c_str(), selected == i))
                    {
                        try
                        {
                            animator.selectAnimation(i);
                            m_animationError.clear();
                        } catch (const EngineError& error)
                        {
                            m_animationError = error.what();
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!animator.selectedAnimation());
            try
            {
                if (ImGui::Button(animator.isPlaying() ? "Pause" : "Play"))
                {
                    if (animator.isPlaying())
                    {
                        animator.pause();
                    }
                    else
                    {
                        animator.play();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop"))
                {
                    animator.stop();
                }
                bool looping = animator.isLooping();
                if (ImGui::Checkbox("Looping", &looping))
                {
                    animator.setLooping(looping);
                }
                float speed = animator.playbackSpeed();
                if (ImGui::DragFloat("Speed", &speed, .05f, 0, 10, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
                {
                    animator.setPlaybackSpeed(speed);
                }
                double time = animator.timeSeconds(), zero = 0, duration = animator.durationSeconds();
                if (ImGui::SliderScalar("Time", ImGuiDataType_Double, &time, &zero, &duration, "%.3f s"))
                {
                    animator.pause();
                    animator.seek(time);
                }
                ImGui::Text("Duration: %.3f s", duration);
            } catch (const EngineError& error)
            {
                m_animationError = error.what();
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextWrapped("Load an animated model or open the character fixture.");
        }
        if (!m_animationError.empty())
        {
            ImGui::TextWrapped("%s", m_animationError.c_str());
        }
    }
    ImGui::End();
}
} // namespace ofg
