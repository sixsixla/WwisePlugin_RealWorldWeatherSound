#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwwa
{
inline constexpr std::size_t kMaxRegisteredPreviewFeatures = 8;
inline constexpr std::size_t kActiveContributionCount = 4;

inline constexpr std::uint32_t kResponseMaskRain = 1u << 0u;
inline constexpr std::uint32_t kResponseMaskWind = 1u << 1u;
inline constexpr std::uint32_t kResponseMaskBoth = kResponseMaskRain | kResponseMaskWind;

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
    float windSpeedMetersPerSecond = 0.0f;
    float windDirectionRadians = 0.0f;
    float windGustiness = 0.0f;
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
    std::uint32_t responseMask = kResponseMaskBoth;
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
    std::uint32_t responseMask = kResponseMaskBoth;
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
// from +Z toward +X. Wind direction is the direction the air travels toward.
// smoothingAlpha is clamped to [0, 1]. Deterministic DSP output is bit-stable
// for the same inputs, block sequence, build, and toolchain. Across toolchains,
// compare output using numerical/perceptual tolerances rather than bit identity.
SceneSnapshot CompileScene(
    const SceneInput& scene,
    const ListenerState& listener,
    const WeatherState& weather,
    const SceneSnapshot* previousSnapshot = nullptr,
    float smoothingAlpha = 1.0f) noexcept;

class WeatherSynth
{
public:
    explicit WeatherSynth(std::uint32_t sampleRate = 48000u) noexcept;

    void Reset(std::uint32_t seed) noexcept;

    // outputLeft and outputRight must each contain frameCount floats. Process
    // owns fixed-size state only and performs no allocation.
    void Process(
        const SceneSnapshot& snapshot,
        float* outputLeft,
        float* outputRight,
        std::size_t frameCount) noexcept;

    std::uint32_t SampleRate() const noexcept;

private:
    struct ResonatorState
    {
        float delay1 = 0.0f;
        float delay2 = 0.0f;
    };

    struct VoiceState
    {
        std::uint64_t featureId = 0u;
        std::uint32_t profileId = 0u;
        std::uint32_t responseMask = 0u;
        std::uint32_t rainRandomState = 1u;
        std::uint32_t windRandomState = 1u;
        float gain = 0.0f;
        float pan = 0.0f;
        float azimuthRadians = 0.0f;
        float windFlow = 0.0f;
        ResonatorState rainModeA{};
        ResonatorState rainModeB{};
    };

    struct ModeCoefficients
    {
        float feedback1 = 0.0f;
        float feedback2 = 0.0f;
        float outputScale = 0.0f;
    };

    struct ProfileCoefficients
    {
        ModeCoefficients modeA{};
        ModeCoefficients modeB{};
        float continuousExcitation = 0.0f;
        float impactGain = 0.0f;
        float directImpactGain = 0.0f;
        float windFlowCoefficient = 0.0f;
        float windGain = 0.0f;
    };

    static std::uint32_t NextRandom(std::uint32_t& state) noexcept;
    static float NextSignedFloat(std::uint32_t& state) noexcept;
    static float ProcessMode(
        ResonatorState& state,
        const ModeCoefficients& coefficients,
        float excitation) noexcept;
    void PrepareCoefficients() noexcept;
    void PrepareVoices(const SceneSnapshot& snapshot) noexcept;

    std::uint32_t m_sampleRate = 48000u;
    std::uint32_t m_seed = 1u;
    std::array<std::uint32_t, 3> m_rainRandomStates{};
    std::array<std::uint32_t, 6> m_windRandomStates{};

    float m_rainLow = 0.0f;
    float m_rainMid = 0.0f;
    float m_rainFineLow = 0.0f;
    float m_rainFineHigh = 0.0f;
    float m_rainSide = 0.0f;
    float m_windLow = 0.0f;
    float m_windMid = 0.0f;
    float m_windAir = 0.0f;
    float m_windSide = 0.0f;
    float m_gustSlow = 0.0f;
    float m_gustMedium = 0.0f;

    float m_rainIntensity = 0.0f;
    float m_windSpeed = 0.0f;
    float m_windDirection = 0.0f;
    float m_windGustiness = 0.0f;
    float m_masterGain = 0.0f;
    float m_controlSmoothing = 0.0f;
    float m_voiceSmoothing = 0.0f;

    float m_rainLowCoefficient = 0.0f;
    float m_rainMidCoefficient = 0.0f;
    float m_rainFineLowCoefficient = 0.0f;
    float m_rainFineHighCoefficient = 0.0f;
    float m_rainSideCoefficient = 0.0f;
    float m_windLowCoefficient = 0.0f;
    float m_windMidCoefficient = 0.0f;
    float m_windAirCoefficient = 0.0f;
    float m_windSideCoefficient = 0.0f;
    float m_gustSlowPole = 0.0f;
    float m_gustSlowDrive = 0.0f;
    float m_gustMediumPole = 0.0f;
    float m_gustMediumDrive = 0.0f;

    std::array<ProfileCoefficients, 4> m_profileCoefficients{};
    std::array<VoiceState, kActiveContributionCount> m_voices{};
};

// Source compatibility for integrations built against the v0.1 class name.
using RainSynth = WeatherSynth;
} // namespace rwwa
