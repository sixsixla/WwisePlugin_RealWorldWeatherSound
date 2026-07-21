#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwwa
{
inline constexpr std::size_t kMaxRegisteredPreviewFeatures = 8;
inline constexpr std::size_t kActiveContributionCount = 4;

inline constexpr std::uint32_t kResponseMaskRain = 1u << 0u;

enum class ResponseProfile : std::uint32_t
{
    Metal = 0,
    Wood = 1,
    Glass = 2,
    Tile = 3,
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ListenerState
{
    Vec3 position{};
    float yawRadians = 0.0f;
};

struct WeatherState
{
    float rainIntensity = 0.0f;
    std::uint32_t seed = 1u;
    bool geometryEnabled = true;
    float masterGainLinear = 1.0f;
};

struct SphereFeature
{
    std::uint64_t id = 0u;
    Vec3 position{};
    float radius = 1.0f;
    std::uint32_t profileId = static_cast<std::uint32_t>(ResponseProfile::Metal);
    std::uint32_t responseMask = kResponseMaskRain;
    std::int32_t priority = 0;
};

struct SceneInput
{
    std::array<SphereFeature, kMaxRegisteredPreviewFeatures> features{};
    std::uint32_t featureCount = 0u;
};

struct Contribution
{
    std::uint64_t featureId = 0u;
    std::uint32_t profileId = static_cast<std::uint32_t>(ResponseProfile::Metal);
    float radius = 0.0f;
    float distance = 0.0f;
    float azimuthRadians = 0.0f;
    float pan = 0.0f;
    float gain = 0.0f;
    float selectionScore = 0.0f;
};

struct SceneSnapshot
{
    ListenerState listener{};
    WeatherState weather{};
    std::array<Contribution, kActiveContributionCount> contributions{};
    std::uint32_t contributionCount = 0u;
};

// Coordinate convention: +Z is forward, +X is right, and positive yaw turns
// from +Z toward +X. smoothingAlpha is clamped to [0, 1]. Deterministic DSP
// output is bit-stable for the same inputs, block sequence, build, and
// toolchain. Across toolchains, compare output using numerical/perceptual
// tolerances rather than requiring bit identity.
SceneSnapshot CompileScene(
    const SceneInput& scene,
    const ListenerState& listener,
    const WeatherState& weather,
    const SceneSnapshot* previousSnapshot = nullptr,
    float smoothingAlpha = 1.0f) noexcept;

class RainSynth
{
public:
    explicit RainSynth(std::uint32_t sampleRate = 48000u) noexcept;

    void Reset(std::uint32_t seed) noexcept;

    // outputLeft and outputRight must each contain frameCount floats. Process
    // owns no dynamic storage and performs no allocation.
    void Process(
        const SceneSnapshot& snapshot,
        float* outputLeft,
        float* outputRight,
        std::size_t frameCount) noexcept;

    std::uint32_t SampleRate() const noexcept;

private:
    struct VoiceState
    {
        std::uint64_t featureId = 0u;
        std::uint32_t profileId = 0u;
        float gain = 0.0f;
        float pan = 0.0f;
        float resonator1 = 0.0f;
        float resonator2 = 0.0f;
    };

    struct ProfileCoefficients
    {
        float resonatorCoefficient = 0.0f;
        float poleSquared = 0.0f;
        float continuousExcitation = 0.0f;
        float impactGain = 0.0f;
        float outputScale = 0.0f;
    };

    std::uint32_t NextRandom() noexcept;
    float NextSignedFloat() noexcept;
    void PrepareCoefficients() noexcept;
    void PrepareVoices(const SceneSnapshot& snapshot) noexcept;

    std::uint32_t m_sampleRate = 48000u;
    std::uint32_t m_randomState = 1u;
    std::uint32_t m_seed = 1u;
    float m_lowBand = 0.0f;
    float m_midBand = 0.0f;
    float m_sideBand = 0.0f;
    float m_rainIntensity = 0.0f;
    float m_masterGain = 0.0f;
    float m_controlSmoothing = 0.0f;
    float m_voiceSmoothing = 0.0f;
    float m_lowCoefficient = 0.0f;
    float m_midCoefficient = 0.0f;
    float m_sideCoefficient = 0.0f;
    std::array<ProfileCoefficients, 4> m_profileCoefficients{};
    std::array<VoiceState, kActiveContributionCount> m_voices{};
};
} // namespace rwwa
