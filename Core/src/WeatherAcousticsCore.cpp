#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rwwa
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float FlushDenormal(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0f;
    }
    return std::abs(value) < std::numeric_limits<float>::min() ? 0.0f : value;
}

float WrapRadians(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0f;
    }

    value = std::fmod(value + kPi, 2.0f * kPi);
    if (value < 0.0f)
    {
        value += 2.0f * kPi;
    }
    return value - kPi;
}

float Lerp(float from, float to, float alpha) noexcept
{
    return from + (to - from) * alpha;
}

float LerpAngle(float from, float to, float alpha) noexcept
{
    return WrapRadians(from + WrapRadians(to - from) * alpha);
}

struct Candidate
{
    const SphereFeature* feature = nullptr;
    float radius = 0.0f;
    float priority = 0.0f;
    float centerDistance = 0.0f;
    float surfaceDistance = 0.0f;
    float score = 0.0f;
    float azimuth = 0.0f;
    std::uint32_t sourceIndex = 0u;
};

bool IsCandidateBetter(const Candidate& left, const Candidate& right) noexcept
{
    if (left.score != right.score)
    {
        return left.score > right.score;
    }
    if (left.priority != right.priority)
    {
        return left.priority > right.priority;
    }
    if (left.radius != right.radius)
    {
        return left.radius > right.radius;
    }
    if (left.surfaceDistance != right.surfaceDistance)
    {
        return left.surfaceDistance < right.surfaceDistance;
    }
    if (left.feature->id != right.feature->id)
    {
        return left.feature->id < right.feature->id;
    }
    return left.sourceIndex < right.sourceIndex;
}

const Contribution* FindContribution(
    const SceneSnapshot& snapshot,
    std::uint64_t featureId) noexcept
{
    const std::size_t count = std::min<std::size_t>(
        snapshot.contributionCount,
        snapshot.contributions.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        if (snapshot.contributions[index].featureId == featureId)
        {
            return &snapshot.contributions[index];
        }
    }
    return nullptr;
}

struct ProfileTuning
{
    float frequencyHz;
    float poleRadius;
    float continuousExcitation;
    float impactGain;
    float outputGain;
};

constexpr std::array<ProfileTuning, 4> kProfileTuning{{
    {4300.0f, 0.993f, 0.010f, 1.00f, 0.76f},
    {720.0f, 0.965f, 0.020f, 0.70f, 0.95f},
    {3100.0f, 0.996f, 0.008f, 0.90f, 0.72f},
    {1650.0f, 0.982f, 0.013f, 0.78f, 0.82f},
}};

std::uint32_t SanitizeProfileId(std::uint32_t profileId) noexcept
{
    return profileId <= static_cast<std::uint32_t>(ResponseProfile::Tile)
        ? profileId
        : static_cast<std::uint32_t>(ResponseProfile::Metal);
}

float ClampAudio(float sample) noexcept
{
    if (!std::isfinite(sample))
    {
        return 0.0f;
    }
    return std::clamp(sample, -1.0f, 1.0f);
}
} // namespace

SceneSnapshot CompileScene(
    const SceneInput& scene,
    const ListenerState& listener,
    const WeatherState& weather,
    const SceneSnapshot* previousSnapshot,
    float smoothingAlpha) noexcept
{
    SceneSnapshot result{};
    result.listener.position.x = ClampFinite(listener.position.x, -1.0e6f, 1.0e6f, 0.0f);
    result.listener.position.y = ClampFinite(listener.position.y, -1.0e6f, 1.0e6f, 0.0f);
    result.listener.position.z = ClampFinite(listener.position.z, -1.0e6f, 1.0e6f, 0.0f);
    result.listener.yawRadians = WrapRadians(listener.yawRadians);
    result.weather.rainIntensity = ClampFinite(weather.rainIntensity, 0.0f, 1.0f, 0.0f);
    result.weather.seed = weather.seed;
    result.weather.geometryEnabled = weather.geometryEnabled;
    result.weather.masterGainLinear = ClampFinite(weather.masterGainLinear, 0.0f, 4.0f, 0.0f);

    if (!result.weather.geometryEnabled)
    {
        return result;
    }

    std::array<Candidate, kMaxRegisteredPreviewFeatures> candidates{};
    std::size_t candidateCount = 0u;
    const std::size_t featureCount = std::min<std::size_t>(
        scene.featureCount,
        scene.features.size());

    for (std::size_t index = 0u; index < featureCount; ++index)
    {
        const SphereFeature& feature = scene.features[index];
        if ((feature.responseMask & kResponseMaskRain) == 0u ||
            !std::isfinite(feature.position.x) ||
            !std::isfinite(feature.position.y) ||
            !std::isfinite(feature.position.z) ||
            !std::isfinite(feature.radius) ||
            feature.radius <= 0.0f)
        {
            continue;
        }

        const float dx = feature.position.x - result.listener.position.x;
        const float dy = feature.position.y - result.listener.position.y;
        const float dz = feature.position.z - result.listener.position.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared))
        {
            continue;
        }

        Candidate& candidate = candidates[candidateCount++];
        candidate.feature = &feature;
        candidate.radius = std::clamp(feature.radius, 0.01f, 100000.0f);
        candidate.priority = static_cast<float>(std::clamp(feature.priority, 0, 1000));
        candidate.centerDistance = std::sqrt(std::max(0.0f, distanceSquared));
        candidate.surfaceDistance = std::max(0.0f, candidate.centerDistance - candidate.radius);
        candidate.score = (1.0f + candidate.priority) * candidate.radius /
            (1.0f + candidate.surfaceDistance);
        const float worldAzimuth = (dx == 0.0f && dz == 0.0f) ? 0.0f : std::atan2(dx, dz);
        candidate.azimuth = WrapRadians(worldAzimuth - result.listener.yawRadians);
        candidate.sourceIndex = static_cast<std::uint32_t>(index);
    }

    for (std::size_t index = 1u; index < candidateCount; ++index)
    {
        Candidate value = candidates[index];
        std::size_t insertion = index;
        while (insertion > 0u && IsCandidateBetter(value, candidates[insertion - 1u]))
        {
            candidates[insertion] = candidates[insertion - 1u];
            --insertion;
        }
        candidates[insertion] = value;
    }

    const float alpha = ClampFinite(smoothingAlpha, 0.0f, 1.0f, 1.0f);
    const std::size_t activeCount = std::min(candidateCount, result.contributions.size());
    result.contributionCount = static_cast<std::uint32_t>(activeCount);

    for (std::size_t index = 0u; index < activeCount; ++index)
    {
        const Candidate& candidate = candidates[index];
        Contribution& contribution = result.contributions[index];
        contribution.featureId = candidate.feature->id;
        contribution.profileId = SanitizeProfileId(candidate.feature->profileId);
        contribution.radius = candidate.radius;
        contribution.distance = candidate.surfaceDistance;
        contribution.azimuthRadians = candidate.azimuth;
        contribution.pan = std::sin(std::clamp(candidate.azimuth, -kHalfPi, kHalfPi));
        contribution.gain = candidate.radius /
            (candidate.radius + candidate.surfaceDistance + 0.001f);
        contribution.selectionScore = candidate.score;

        if (previousSnapshot != nullptr && alpha < 1.0f)
        {
            if (const Contribution* previous = FindContribution(*previousSnapshot, contribution.featureId))
            {
                contribution.azimuthRadians = LerpAngle(
                    previous->azimuthRadians,
                    contribution.azimuthRadians,
                    alpha);
                contribution.pan = Lerp(
                    ClampFinite(previous->pan, -1.0f, 1.0f, 0.0f),
                    contribution.pan,
                    alpha);
                contribution.gain = Lerp(
                    ClampFinite(previous->gain, 0.0f, 1.0f, 0.0f),
                    contribution.gain,
                    alpha);
            }
            else
            {
                contribution.gain *= alpha;
            }
        }

        contribution.pan = ClampFinite(contribution.pan, -1.0f, 1.0f, 0.0f);
        contribution.gain = ClampFinite(contribution.gain, 0.0f, 1.0f, 0.0f);
        contribution.selectionScore = ClampFinite(
            contribution.selectionScore,
            0.0f,
            std::numeric_limits<float>::max(),
            0.0f);
    }

    return result;
}

RainSynth::RainSynth(std::uint32_t sampleRate) noexcept
    : m_sampleRate(std::clamp(sampleRate, 8000u, 192000u))
{
    PrepareCoefficients();
    Reset(1u);
}

void RainSynth::Reset(std::uint32_t seed) noexcept
{
    m_seed = seed;
    m_randomState = seed == 0u ? 0x6d2b79f5u : seed;
    m_lowBand = 0.0f;
    m_midBand = 0.0f;
    m_sideBand = 0.0f;
    m_rainIntensity = 0.0f;
    m_masterGain = 0.0f;
    m_voices = {};
}

std::uint32_t RainSynth::NextRandom() noexcept
{
    std::uint32_t value = m_randomState;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    m_randomState = value;
    return value;
}

float RainSynth::NextSignedFloat() noexcept
{
    constexpr float scale = 1.0f / 8388607.5f;
    return static_cast<float>(NextRandom() >> 8u) * scale - 1.0f;
}

void RainSynth::PrepareCoefficients() noexcept
{
    const float sampleRate = static_cast<float>(m_sampleRate);
    m_controlSmoothing = 1.0f - std::exp(-1.0f / (0.030f * sampleRate));
    m_voiceSmoothing = 1.0f - std::exp(-1.0f / (0.018f * sampleRate));
    m_lowCoefficient = 1.0f - std::exp(-2.0f * kPi * 180.0f / sampleRate);
    m_midCoefficient = 1.0f - std::exp(-2.0f * kPi * 2100.0f / sampleRate);
    m_sideCoefficient = 1.0f - std::exp(-2.0f * kPi * 900.0f / sampleRate);

    for (std::size_t index = 0u; index < kProfileTuning.size(); ++index)
    {
        const ProfileTuning& tuning = kProfileTuning[index];
        const float frequency = std::min(tuning.frequencyHz, sampleRate * 0.45f);
        ProfileCoefficients& coefficients = m_profileCoefficients[index];
        coefficients.poleSquared = tuning.poleRadius * tuning.poleRadius;
        coefficients.resonatorCoefficient = 2.0f * tuning.poleRadius *
            std::cos(2.0f * kPi * frequency / sampleRate);
        coefficients.continuousExcitation = tuning.continuousExcitation;
        coefficients.impactGain = tuning.impactGain;
        coefficients.outputScale = (1.0f - tuning.poleRadius) * tuning.outputGain;
    }
}

void RainSynth::PrepareVoices(const SceneSnapshot& snapshot) noexcept
{
    const auto previousVoices = m_voices;
    const std::size_t count = std::min<std::size_t>(
        snapshot.contributionCount,
        snapshot.contributions.size());

    for (std::size_t index = 0u; index < m_voices.size(); ++index)
    {
        VoiceState next{};
        if (index < count)
        {
            const Contribution& contribution = snapshot.contributions[index];
            for (const VoiceState& previous : previousVoices)
            {
                if (previous.featureId == contribution.featureId)
                {
                    next = previous;
                    break;
                }
            }
            next.featureId = contribution.featureId;
            next.profileId = SanitizeProfileId(contribution.profileId);
            if (next.gain == 0.0f)
            {
                next.pan = ClampFinite(contribution.pan, -1.0f, 1.0f, 0.0f);
            }
        }
        m_voices[index] = next;
    }
}

void RainSynth::Process(
    const SceneSnapshot& snapshot,
    float* outputLeft,
    float* outputRight,
    std::size_t frameCount) noexcept
{
    if (outputLeft == nullptr || outputRight == nullptr || frameCount == 0u)
    {
        return;
    }

    if (snapshot.weather.seed != m_seed)
    {
        Reset(snapshot.weather.seed);
    }

    PrepareVoices(snapshot);

    const float targetRain = ClampFinite(snapshot.weather.rainIntensity, 0.0f, 1.0f, 0.0f);
    const float targetMaster = ClampFinite(snapshot.weather.masterGainLinear, 0.0f, 4.0f, 0.0f);
    const std::size_t contributionCount = std::min<std::size_t>(
        snapshot.contributionCount,
        snapshot.contributions.size());

    for (std::size_t frame = 0u; frame < frameCount; ++frame)
    {
        m_rainIntensity = FlushDenormal(
            m_rainIntensity + (targetRain - m_rainIntensity) * m_controlSmoothing);
        m_masterGain = FlushDenormal(
            m_masterGain + (targetMaster - m_masterGain) * m_controlSmoothing);

        const float white = NextSignedFloat();
        const float sideWhite = NextSignedFloat();
        m_lowBand = FlushDenormal(m_lowBand + (white - m_lowBand) * m_lowCoefficient);
        m_midBand = FlushDenormal(m_midBand + (white - m_midBand) * m_midCoefficient);
        m_sideBand = FlushDenormal(m_sideBand + (sideWhite - m_sideBand) * m_sideCoefficient);
        const float low = m_lowBand;
        const float middle = m_midBand - m_lowBand;
        const float high = white - m_midBand;
        const float rainBrightness = 0.18f + 0.22f * m_rainIntensity;
        const float bed = (0.52f * low + 0.31f * middle + rainBrightness * high) *
            (0.025f + 0.105f * m_rainIntensity) * m_rainIntensity;
        const float side = m_sideBand * 0.018f * m_rainIntensity;
        float left = bed + side;
        float right = bed - side;

        for (std::size_t index = 0u; index < m_voices.size(); ++index)
        {
            VoiceState& voice = m_voices[index];
            const Contribution* contribution = index < contributionCount
                ? &snapshot.contributions[index]
                : nullptr;
            const float targetGain = contribution != nullptr
                ? ClampFinite(contribution->gain, 0.0f, 1.0f, 0.0f)
                : 0.0f;
            const float targetPan = contribution != nullptr
                ? ClampFinite(contribution->pan, -1.0f, 1.0f, 0.0f)
                : voice.pan;
            voice.gain = FlushDenormal(
                voice.gain + (targetGain - voice.gain) * m_voiceSmoothing);
            voice.pan = FlushDenormal(
                voice.pan + (targetPan - voice.pan) * m_voiceSmoothing);

            if (voice.featureId == 0u && contribution == nullptr)
            {
                continue;
            }

            const ProfileCoefficients& profile = m_profileCoefficients[voice.profileId];
            const float impactRate = m_rainIntensity * (4.0f + 52.0f * m_rainIntensity) *
                (0.6f + 0.4f * std::min(voice.gain, 1.0f));
            const float impactProbability = impactRate / static_cast<float>(m_sampleRate);
            const float randomGate = static_cast<float>(NextRandom() >> 8u) * (1.0f / 16777216.0f);
            float excitation = high * profile.continuousExcitation * m_rainIntensity;
            if (randomGate < impactProbability)
            {
                const float impactRandom = 0.35f + 0.65f *
                    (static_cast<float>(NextRandom() >> 8u) * (1.0f / 16777216.0f));
                excitation += impactRandom * profile.impactGain *
                    (0.2f + 0.8f * m_rainIntensity);
            }

            float resonated = excitation + profile.resonatorCoefficient * voice.resonator1 -
                profile.poleSquared * voice.resonator2;
            if (!std::isfinite(resonated) || std::abs(resonated) > 10000.0f)
            {
                resonated = 0.0f;
                voice.resonator1 = 0.0f;
                voice.resonator2 = 0.0f;
            }
            voice.resonator2 = FlushDenormal(voice.resonator1);
            voice.resonator1 = FlushDenormal(resonated);

            const float surfaceSample = resonated * profile.outputScale *
                voice.gain * m_rainIntensity * 0.58f;
            const float safePan = std::clamp(voice.pan, -1.0f, 1.0f);
            const float leftPan = std::sqrt(0.5f * (1.0f - safePan));
            const float rightPan = std::sqrt(0.5f * (1.0f + safePan));
            left += surfaceSample * leftPan;
            right += surfaceSample * rightPan;
        }

        outputLeft[frame] = ClampAudio(left * m_masterGain);
        outputRight[frame] = ClampAudio(right * m_masterGain);
    }
}

std::uint32_t RainSynth::SampleRate() const noexcept
{
    return m_sampleRate;
}
} // namespace rwwa
