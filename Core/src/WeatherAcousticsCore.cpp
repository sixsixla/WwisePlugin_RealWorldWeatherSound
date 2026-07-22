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
constexpr float kWeatherSilenceEpsilon = 1.0e-6f;

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

float SanitizeWeatherMagnitude(float value, float maximum) noexcept
{
    const float sanitized = ClampFinite(value, 0.0f, maximum, 0.0f);
    return sanitized <= kWeatherSilenceEpsilon ? 0.0f : sanitized;
}

float FlushDenormal(float value) noexcept
{
    if (!std::isfinite(value) || std::abs(value) < 1.0e-20f)
    {
        return 0.0f;
    }
    return value;
}

float MoveTowardSmoothed(float current, float target, float coefficient) noexcept
{
    const float value = FlushDenormal(current + (target - current) * coefficient);
    return target == 0.0f && std::abs(value) < 1.0e-8f ? 0.0f : value;
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

float OnePoleCoefficient(float frequencyHz, float sampleRate) noexcept
{
    return 1.0f - std::exp(-2.0f * kPi * frequencyHz / sampleRate);
}

struct Candidate
{
    const SphereFeature* feature = nullptr;
    std::uint32_t responseMask = 0u;
    float radius = 0.0f;
    float priority = 0.0f;
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
    float modeAFrequencyHz;
    float modeAPoleRadius;
    float modeAGain;
    float modeBFrequencyHz;
    float modeBPoleRadius;
    float modeBGain;
    float continuousExcitation;
    float impactGain;
    float directImpactGain;
    float windCutoffHz;
    float windGain;
};

constexpr std::array<ProfileTuning, 4> kProfileTuning{{
    {2350.0f, 0.994f, 0.76f, 5350.0f, 0.990f, 0.42f, 0.0035f, 1.00f, 0.020f, 1050.0f, 0.90f},
    {480.0f, 0.975f, 1.05f, 1280.0f, 0.958f, 0.58f, 0.0100f, 0.72f, 0.010f, 520.0f, 1.10f},
    {2860.0f, 0.996f, 0.62f, 5080.0f, 0.992f, 0.48f, 0.0025f, 0.90f, 0.016f, 1350.0f, 0.82f},
    {1120.0f, 0.985f, 0.92f, 2640.0f, 0.979f, 0.52f, 0.0065f, 0.82f, 0.014f, 760.0f, 1.00f},
}};

std::uint32_t SanitizeProfileId(std::uint32_t profileId) noexcept
{
    return profileId <= static_cast<std::uint32_t>(ResponseProfile::Tile)
        ? profileId
        : static_cast<std::uint32_t>(ResponseProfile::Metal);
}

std::uint32_t SanitizeResponseMask(std::uint32_t mask) noexcept
{
    return mask & kResponseMaskBoth;
}

std::uint32_t MixSeed(std::uint32_t seed, std::uint32_t salt) noexcept
{
    std::uint32_t value = seed ^ salt;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value == 0u ? salt | 1u : value;
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
    result.weather.rainIntensity = SanitizeWeatherMagnitude(weather.rainIntensity, 1.0f);
    result.weather.windSpeedMetersPerSecond = SanitizeWeatherMagnitude(
        weather.windSpeedMetersPerSecond, 60.0f);
    result.weather.windDirectionRadians = WrapRadians(weather.windDirectionRadians);
    result.weather.windGustiness = ClampFinite(weather.windGustiness, 0.0f, 1.0f, 0.0f);
    result.weather.seed = weather.seed;
    result.weather.geometryEnabled = weather.geometryEnabled;
    result.weather.masterGainLinear = ClampFinite(weather.masterGainLinear, 0.0f, 4.0f, 0.0f);

    if (!result.weather.geometryEnabled)
    {
        return result;
    }

    std::uint32_t activeWeatherMask = 0u;
    if (result.weather.rainIntensity > 0.0f)
    {
        activeWeatherMask |= kResponseMaskRain;
    }
    if (result.weather.windSpeedMetersPerSecond > 0.0f)
    {
        activeWeatherMask |= kResponseMaskWind;
    }
    if (activeWeatherMask == 0u)
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
        const std::uint32_t configuredResponseMask =
            SanitizeResponseMask(feature.responseMask);
        if ((configuredResponseMask & activeWeatherMask) == 0u ||
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
        candidate.responseMask = configuredResponseMask;
        candidate.radius = std::clamp(feature.radius, 0.01f, 100000.0f);
        candidate.priority = static_cast<float>(std::clamp(feature.priority, 0, 1000));
        const float centerDistance = std::sqrt(std::max(0.0f, distanceSquared));
        candidate.surfaceDistance = std::max(0.0f, centerDistance - candidate.radius);
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
        contribution.responseMask = candidate.responseMask;
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

WeatherSynth::WeatherSynth(std::uint32_t sampleRate) noexcept
    : m_sampleRate(std::clamp(sampleRate, 8000u, 192000u))
{
    PrepareCoefficients();
    Reset(1u);
}

void WeatherSynth::Reset(std::uint32_t seed) noexcept
{
    m_seed = seed;
    for (std::size_t index = 0u; index < m_rainRandomStates.size(); ++index)
    {
        m_rainRandomStates[index] = MixSeed(seed, 0x9e3779b9u + static_cast<std::uint32_t>(index) * 0x85ebca6bu);
    }
    for (std::size_t index = 0u; index < m_windRandomStates.size(); ++index)
    {
        m_windRandomStates[index] = MixSeed(seed, 0xc2b2ae35u + static_cast<std::uint32_t>(index) * 0x27d4eb2fu);
    }

    m_rainLow = 0.0f;
    m_rainMid = 0.0f;
    m_rainFineLow = 0.0f;
    m_rainFineHigh = 0.0f;
    m_rainSide = 0.0f;
    m_windLow = 0.0f;
    m_windMid = 0.0f;
    m_windAir = 0.0f;
    m_windSide = 0.0f;
    m_gustSlow = 0.0f;
    m_gustMedium = 0.0f;
    m_rainIntensity = 0.0f;
    m_windSpeed = 0.0f;
    m_windDirection = 0.0f;
    m_windGustiness = 0.0f;
    m_masterGain = 0.0f;
    m_voices = {};
    for (std::size_t index = 0u; index < m_voices.size(); ++index)
    {
        m_voices[index].rainRandomState = MixSeed(seed, 0x165667b1u + static_cast<std::uint32_t>(index) * 0xd3a2646cu);
        m_voices[index].windRandomState = MixSeed(seed, 0x7f4a7c15u + static_cast<std::uint32_t>(index) * 0x9e3779b9u);
    }
}

std::uint32_t WeatherSynth::NextRandom(std::uint32_t& state) noexcept
{
    std::uint32_t value = state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    state = value;
    return value;
}

float WeatherSynth::NextSignedFloat(std::uint32_t& state) noexcept
{
    constexpr float scale = 1.0f / 8388607.5f;
    return static_cast<float>(NextRandom(state) >> 8u) * scale - 1.0f;
}

float WeatherSynth::ProcessMode(
    ResonatorState& state,
    const ModeCoefficients& coefficients,
    float excitation) noexcept
{
    float value = excitation + coefficients.feedback1 * state.delay1 -
        coefficients.feedback2 * state.delay2;
    if (!std::isfinite(value) || std::abs(value) > 10000.0f)
    {
        value = 0.0f;
        state = {};
    }
    state.delay2 = FlushDenormal(state.delay1);
    state.delay1 = FlushDenormal(value);
    return value * coefficients.outputScale;
}

void WeatherSynth::PrepareCoefficients() noexcept
{
    const float sampleRate = static_cast<float>(m_sampleRate);
    m_controlSmoothing = 1.0f - std::exp(-1.0f / (0.035f * sampleRate));
    m_voiceSmoothing = 1.0f - std::exp(-1.0f / (0.025f * sampleRate));

    m_rainLowCoefficient = OnePoleCoefficient(380.0f, sampleRate);
    m_rainMidCoefficient = OnePoleCoefficient(1800.0f, sampleRate);
    m_rainFineLowCoefficient = OnePoleCoefficient(2400.0f, sampleRate);
    m_rainFineHighCoefficient = OnePoleCoefficient(6200.0f, sampleRate);
    m_rainSideCoefficient = OnePoleCoefficient(1250.0f, sampleRate);
    m_windLowCoefficient = OnePoleCoefficient(95.0f, sampleRate);
    m_windMidCoefficient = OnePoleCoefficient(620.0f, sampleRate);
    m_windAirCoefficient = OnePoleCoefficient(2600.0f, sampleRate);
    m_windSideCoefficient = OnePoleCoefficient(840.0f, sampleRate);

    m_gustSlowPole = std::exp(-2.0f * kPi * 0.18f / sampleRate);
    m_gustSlowDrive = std::sqrt(std::max(0.0f, 1.0f - m_gustSlowPole * m_gustSlowPole));
    m_gustMediumPole = std::exp(-2.0f * kPi * 1.15f / sampleRate);
    m_gustMediumDrive = std::sqrt(std::max(0.0f, 1.0f - m_gustMediumPole * m_gustMediumPole));

    for (std::size_t index = 0u; index < kProfileTuning.size(); ++index)
    {
        const ProfileTuning& tuning = kProfileTuning[index];
        ProfileCoefficients& coefficients = m_profileCoefficients[index];
        const auto prepareMode = [sampleRate](float frequency, float radius, float gain) noexcept
        {
            ModeCoefficients mode{};
            frequency = std::min(frequency, sampleRate * 0.44f);
            mode.feedback1 = 2.0f * radius * std::cos(2.0f * kPi * frequency / sampleRate);
            mode.feedback2 = radius * radius;
            mode.outputScale = (1.0f - radius) * gain;
            return mode;
        };
        coefficients.modeA = prepareMode(
            tuning.modeAFrequencyHz, tuning.modeAPoleRadius, tuning.modeAGain);
        coefficients.modeB = prepareMode(
            tuning.modeBFrequencyHz, tuning.modeBPoleRadius, tuning.modeBGain);
        coefficients.continuousExcitation = tuning.continuousExcitation;
        coefficients.impactGain = tuning.impactGain;
        coefficients.directImpactGain = tuning.directImpactGain;
        coefficients.windFlowCoefficient = OnePoleCoefficient(tuning.windCutoffHz, sampleRate);
        coefficients.windGain = tuning.windGain;
    }
}

void WeatherSynth::PrepareVoices(const SceneSnapshot& snapshot) noexcept
{
    const auto previousVoices = m_voices;
    const std::size_t count = std::min<std::size_t>(
        snapshot.contributionCount,
        snapshot.contributions.size());

    for (std::size_t index = 0u; index < m_voices.size(); ++index)
    {
        VoiceState next{};
        next.rainRandomState = MixSeed(m_seed, 0x165667b1u + static_cast<std::uint32_t>(index) * 0xd3a2646cu);
        next.windRandomState = MixSeed(m_seed, 0x7f4a7c15u + static_cast<std::uint32_t>(index) * 0x9e3779b9u);
        if (index < count)
        {
            const Contribution& contribution = snapshot.contributions[index];
            const std::uint32_t profileId = SanitizeProfileId(contribution.profileId);
            const std::uint32_t responseMask = SanitizeResponseMask(contribution.responseMask);
            for (const VoiceState& previous : previousVoices)
            {
                if (previous.featureId == contribution.featureId)
                {
                    if (previous.profileId == profileId &&
                        previous.responseMask == responseMask)
                    {
                        next = previous;
                    }
                    else
                    {
                        // A compacted authoring slot may retain its feature id
                        // while its acoustic meaning changes. Preserve only the
                        // spatial ramps; modal, wind, and random state must start
                        // as a new voice for the new response signature.
                        next.gain = previous.gain;
                        next.pan = previous.pan;
                        next.azimuthRadians = previous.azimuthRadians;
                    }
                    break;
                }
            }
            next.featureId = contribution.featureId;
            next.profileId = profileId;
            next.responseMask = responseMask;
            if (next.gain == 0.0f)
            {
                next.pan = ClampFinite(contribution.pan, -1.0f, 1.0f, 0.0f);
                next.azimuthRadians = ClampFinite(contribution.azimuthRadians, -kPi, kPi, 0.0f);
            }
        }
        m_voices[index] = next;
    }
}

void WeatherSynth::Process(
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

    const float targetRain = SanitizeWeatherMagnitude(snapshot.weather.rainIntensity, 1.0f);
    const float targetWind = SanitizeWeatherMagnitude(
        snapshot.weather.windSpeedMetersPerSecond, 60.0f);
    const float targetDirection = WrapRadians(snapshot.weather.windDirectionRadians);
    const float targetGustiness = ClampFinite(snapshot.weather.windGustiness, 0.0f, 1.0f, 0.0f);
    const float targetMaster = ClampFinite(snapshot.weather.masterGainLinear, 0.0f, 4.0f, 0.0f);
    const std::size_t contributionCount = std::min<std::size_t>(
        snapshot.contributionCount,
        snapshot.contributions.size());

    for (std::size_t frame = 0u; frame < frameCount; ++frame)
    {
        m_rainIntensity = MoveTowardSmoothed(m_rainIntensity, targetRain, m_controlSmoothing);
        m_windSpeed = MoveTowardSmoothed(m_windSpeed, targetWind, m_controlSmoothing);
        m_windDirection = LerpAngle(m_windDirection, targetDirection, m_controlSmoothing);
        m_windGustiness = MoveTowardSmoothed(
            m_windGustiness, targetGustiness, m_controlSmoothing);
        m_masterGain = MoveTowardSmoothed(m_masterGain, targetMaster, m_controlSmoothing);

        if (targetRain > 0.0f)
        {
            const float rainLowNoise = NextSignedFloat(m_rainRandomStates[0]);
            const float rainMidNoise = NextSignedFloat(m_rainRandomStates[1]);
            const float rainFineNoise = NextSignedFloat(m_rainRandomStates[2]);
            m_rainLow = FlushDenormal(
                m_rainLow + (rainLowNoise - m_rainLow) * m_rainLowCoefficient);
            m_rainMid = FlushDenormal(
                m_rainMid + (rainMidNoise - m_rainMid) * m_rainMidCoefficient);
            m_rainFineLow = FlushDenormal(
                m_rainFineLow + (rainFineNoise - m_rainFineLow) * m_rainFineLowCoefficient);
            m_rainFineHigh = FlushDenormal(
                m_rainFineHigh + (rainFineNoise - m_rainFineHigh) * m_rainFineHighCoefficient);
            m_rainSide = FlushDenormal(
                m_rainSide + (rainMidNoise - m_rainSide) * m_rainSideCoefficient);
        }
        else
        {
            m_rainLow = MoveTowardSmoothed(m_rainLow, 0.0f, m_rainLowCoefficient);
            m_rainMid = MoveTowardSmoothed(m_rainMid, 0.0f, m_rainMidCoefficient);
            m_rainFineLow = MoveTowardSmoothed(m_rainFineLow, 0.0f, m_rainFineLowCoefficient);
            m_rainFineHigh = MoveTowardSmoothed(m_rainFineHigh, 0.0f, m_rainFineHighCoefficient);
            m_rainSide = MoveTowardSmoothed(m_rainSide, 0.0f, m_rainSideCoefficient);
        }

        const float windLowNoise = NextSignedFloat(m_windRandomStates[0]);
        const float windMidNoise = NextSignedFloat(m_windRandomStates[1]);
        const float windAirNoise = NextSignedFloat(m_windRandomStates[2]);
        const float windSideNoise = NextSignedFloat(m_windRandomStates[3]);
        const float gustSlowNoise = NextSignedFloat(m_windRandomStates[4]);
        const float gustMediumNoise = NextSignedFloat(m_windRandomStates[5]);
        m_windLow = FlushDenormal(
            m_windLow + (windLowNoise - m_windLow) * m_windLowCoefficient);
        m_windMid = FlushDenormal(
            m_windMid + (windMidNoise - m_windMid) * m_windMidCoefficient);
        m_windAir = FlushDenormal(
            m_windAir + (windAirNoise - m_windAir) * m_windAirCoefficient);
        m_windSide = FlushDenormal(
            m_windSide + (windSideNoise - m_windSide) * m_windSideCoefficient);
        m_gustSlow = FlushDenormal(
            m_gustSlowPole * m_gustSlow + m_gustSlowDrive * gustSlowNoise);
        m_gustMedium = FlushDenormal(
            m_gustMediumPole * m_gustMedium + m_gustMediumDrive * gustMediumNoise);

        const float rain = m_rainIntensity;
        const float rainBed = (
            0.40f * m_rainLow +
            0.48f * m_rainMid +
            0.18f * (m_rainFineHigh - m_rainFineLow)) *
            rain * (0.025f + 0.095f * rain);
        const float rainSide = m_rainSide * rain * 0.028f;
        float left = rainBed + rainSide;
        float right = rainBed - rainSide;

        const float speed01 = std::clamp(m_windSpeed / 25.0f, 0.0f, 1.0f);
        const float windStrength = std::pow(speed01, 0.78f);
        const float gustModulation = std::clamp(
            1.0f + m_windGustiness * (0.68f * m_gustSlow + 0.32f * m_gustMedium),
            0.18f,
            1.85f);
        const float flow =
            0.58f * m_windLow +
            (0.33f + 0.10f * speed01) * m_windMid +
            (0.09f + 0.13f * speed01) * m_windAir;
        const float windBed = flow * windStrength * gustModulation * (0.105f + 0.130f * speed01);
        const float relativeWindDirection = WrapRadians(
            m_windDirection - snapshot.listener.yawRadians);
        const float windPan = 0.52f * std::sin(relativeWindDirection);
        const float windLeftPan = std::sqrt(0.5f * (1.0f - windPan));
        const float windRightPan = std::sqrt(0.5f * (1.0f + windPan));
        const float windWidth = m_windSide * windStrength * gustModulation * 0.026f;
        left += windBed * windLeftPan + windWidth;
        right += windBed * windRightPan - windWidth;

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
            const float targetAzimuth = contribution != nullptr
                ? ClampFinite(contribution->azimuthRadians, -kPi, kPi, 0.0f)
                : voice.azimuthRadians;
            voice.gain = MoveTowardSmoothed(voice.gain, targetGain, m_voiceSmoothing);
            voice.pan = MoveTowardSmoothed(voice.pan, targetPan, m_voiceSmoothing);
            voice.azimuthRadians = LerpAngle(
                voice.azimuthRadians, targetAzimuth, m_voiceSmoothing);

            const ProfileCoefficients& profile = m_profileCoefficients[voice.profileId];
            float surfaceSample = 0.0f;

            if ((voice.responseMask & kResponseMaskRain) != 0u)
            {
                if (targetRain > 0.0f)
                {
                    const float voiceRainNoise = NextSignedFloat(voice.rainRandomState);
                    const float gate = static_cast<float>(NextRandom(voice.rainRandomState) >> 8u) *
                        (1.0f / 16777216.0f);
                    const float amplitudeRandom =
                        static_cast<float>(NextRandom(voice.rainRandomState) >> 8u) *
                        (1.0f / 16777216.0f);
                    const float density = rain * rain;
                    const float radiusScale = contribution != nullptr
                        ? std::clamp(std::sqrt(std::max(0.01f, contribution->radius)) * 0.72f, 0.45f, 2.2f)
                        : 1.0f;
                    const float impactRate = (3.0f + 92.0f * density) * radiusScale *
                        (0.55f + 0.45f * std::min(voice.gain, 1.0f));
                    const float impactProbability = impactRate / static_cast<float>(m_sampleRate);
                    float excitation = voiceRainNoise * profile.continuousExcitation * rain;
                    float directImpact = 0.0f;
                    if (gate < impactProbability)
                    {
                        const float impact = (0.18f + 0.82f * amplitudeRandom * amplitudeRandom) *
                            profile.impactGain * (0.24f + 0.76f * rain);
                        excitation += impact;
                        directImpact = impact * profile.directImpactGain;
                    }
                    surfaceSample += (
                        ProcessMode(voice.rainModeA, profile.modeA, excitation) +
                        ProcessMode(voice.rainModeB, profile.modeB, excitation * 0.76f) +
                        directImpact) * voice.gain * rain;
                }
                else
                {
                    ProcessMode(voice.rainModeA, profile.modeA, 0.0f);
                    ProcessMode(voice.rainModeB, profile.modeB, 0.0f);
                }
            }
            else
            {
                ProcessMode(voice.rainModeA, profile.modeA, 0.0f);
                ProcessMode(voice.rainModeB, profile.modeB, 0.0f);
            }

            if ((voice.responseMask & kResponseMaskWind) != 0u)
            {
                const float voiceWindNoise = NextSignedFloat(voice.windRandomState);
                voice.windFlow = FlushDenormal(
                    voice.windFlow + (voiceWindNoise - voice.windFlow) * profile.windFlowCoefficient);
                const float incidence = 0.25f + 0.75f * std::max(
                    0.0f,
                    std::cos(WrapRadians(relativeWindDirection - voice.azimuthRadians)));
                const float turbulence = 0.74f * voice.windFlow +
                    0.18f * (voiceWindNoise - voice.windFlow);
                surfaceSample += turbulence * profile.windGain * voice.gain * incidence *
                    windStrength * gustModulation * (0.040f + 0.090f * speed01);
            }

            const float safePan = std::clamp(voice.pan, -1.0f, 1.0f);
            const float leftPan = std::sqrt(0.5f * (1.0f - safePan));
            const float rightPan = std::sqrt(0.5f * (1.0f + safePan));
            left += surfaceSample * leftPan;
            right += surfaceSample * rightPan;
        }

        constexpr float outputTrim = 2.0f;
        outputLeft[frame] = ClampAudio(left * m_masterGain * outputTrim);
        outputRight[frame] = ClampAudio(right * m_masterGain * outputTrim);
    }
}

std::uint32_t WeatherSynth::SampleRate() const noexcept
{
    return m_sampleRate;
}
} // namespace rwwa
