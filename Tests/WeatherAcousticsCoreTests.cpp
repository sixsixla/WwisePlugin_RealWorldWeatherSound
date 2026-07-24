#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kFrames = 16384u;
constexpr std::uint32_t kSampleRate = 48000u;

int g_failureCount = 0;

void Expect(bool condition, const char* expression, const char* file, int line)
{
    if (!condition)
    {
        ++g_failureCount;
        std::cerr << file << ':' << line << ": EXPECT failed: " << expression << '\n';
    }
}

#define EXPECT(expression) Expect((expression), #expression, __FILE__, __LINE__)

struct AudioMetrics
{
    double rms = 0.0;
    double peak = 0.0;
    double crestFactor = 0.0;
    double envelopeCv = 0.0;
    double stereoCorrelation = 0.0;
    double firstDifferenceRatio = 0.0;
};

rwwa::SceneSnapshot MakeSingleFeatureSnapshot(
    rwwa::ResponseProfile profile,
    std::uint32_t responseMask = rwwa::kResponseMaskBoth,
    rwwa::Vec3 position = {3.0f, 0.0f, 4.0f})
{
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        42u,
        position,
        2.0f,
        static_cast<std::uint32_t>(profile),
        responseMask,
        2};
    rwwa::WeatherState weather{};
    weather.rainIntensity = 0.78f;
    weather.windSpeedMetersPerSecond = 11.0f;
    weather.windDirectionRadians = 0.35f;
    weather.windGustiness = 0.62f;
    weather.seed = 0x12345678u;
    weather.geometryEnabled = true;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene(scene, {}, weather);
}

rwwa::SceneSnapshot MakeOpenWindSnapshot(float direction, float gustiness)
{
    rwwa::WeatherState weather{};
    weather.windSpeedMetersPerSecond = 13.0f;
    weather.windDirectionRadians = direction;
    weather.windGustiness = gustiness;
    weather.seed = 0x2f8a91c3u;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene({}, {}, weather);
}

rwwa::SceneSnapshot MakeFeatureWeatherSnapshot(
    std::uint64_t featureId,
    std::uint32_t responseMask,
    const rwwa::WeatherState& weather)
{
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        featureId,
        {2.0f, 0.0f, 3.0f},
        2.0f,
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal),
        responseMask,
        0};
    return rwwa::CompileScene(scene, {}, weather);
}

AudioMetrics Measure(const std::vector<float>& left, const std::vector<float>& right)
{
    EXPECT(left.size() == right.size());
    EXPECT(!left.empty());
    double energy = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    double cross = 0.0;
    double differenceEnergy = 0.0;
    double peak = 0.0;
    for (std::size_t frame = 0u; frame < left.size(); ++frame)
    {
        const double l = left[frame];
        const double r = right[frame];
        energy += 0.5 * (l * l + r * r);
        leftEnergy += l * l;
        rightEnergy += r * r;
        cross += l * r;
        peak = std::max({peak, std::abs(l), std::abs(r)});
        if (frame > 0u)
        {
            const double dl = l - left[frame - 1u];
            const double dr = r - right[frame - 1u];
            differenceEnergy += 0.5 * (dl * dl + dr * dr);
        }
    }

    constexpr std::size_t envelopeBlock = 256u;
    const std::size_t envelopeCount = left.size() / envelopeBlock;
    double envelopeMean = 0.0;
    double envelopeSquaredMean = 0.0;
    for (std::size_t block = 0u; block < envelopeCount; ++block)
    {
        double blockEnergy = 0.0;
        for (std::size_t offset = 0u; offset < envelopeBlock; ++offset)
        {
            const std::size_t frame = block * envelopeBlock + offset;
            blockEnergy += 0.5 * (
                static_cast<double>(left[frame]) * left[frame] +
                static_cast<double>(right[frame]) * right[frame]);
        }
        const double envelope = std::sqrt(blockEnergy / static_cast<double>(envelopeBlock));
        envelopeMean += envelope;
        envelopeSquaredMean += envelope * envelope;
    }
    envelopeMean /= static_cast<double>(envelopeCount);
    envelopeSquaredMean /= static_cast<double>(envelopeCount);

    AudioMetrics metrics{};
    metrics.rms = std::sqrt(energy / static_cast<double>(left.size()));
    metrics.peak = peak;
    metrics.crestFactor = metrics.rms > 0.0 ? peak / metrics.rms : 0.0;
    const double envelopeVariance = std::max(
        0.0, envelopeSquaredMean - envelopeMean * envelopeMean);
    metrics.envelopeCv = envelopeMean > 0.0
        ? std::sqrt(envelopeVariance) / envelopeMean
        : 0.0;
    metrics.stereoCorrelation = cross /
        std::sqrt(std::max(1.0e-30, leftEnergy * rightEnergy));
    metrics.firstDifferenceRatio = std::sqrt(
        differenceEnergy / std::max(1.0e-30, energy));
    return metrics;
}

AudioMetrics RenderMetrics(const rwwa::SceneSnapshot& snapshot, std::size_t seconds)
{
    rwwa::WeatherSynth synth(kSampleRate);
    std::vector<float> warmLeft(kSampleRate);
    std::vector<float> warmRight(kSampleRate);
    synth.Process(snapshot, warmLeft.data(), warmRight.data(), warmLeft.size());
    std::vector<float> left(static_cast<std::size_t>(kSampleRate) * seconds);
    std::vector<float> right(left.size());
    synth.Process(snapshot, left.data(), right.data(), left.size());
    return Measure(left, right);
}

void TestListenerRelativePanning()
{
    const rwwa::SceneSnapshot right = MakeSingleFeatureSnapshot(
        rwwa::ResponseProfile::Metal,
        rwwa::kResponseMaskBoth,
        {5.0f, 0.0f, 0.0f});
    EXPECT(right.contributionCount == 1u);
    EXPECT(right.contributions[0].pan > 0.99f);

    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        7u, {0.0f, 0.0f, 5.0f}, 1.0f, 1u, rwwa::kResponseMaskRain, 1};
    rwwa::ListenerState listener{};
    listener.yawRadians = kPi * 0.5f;
    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot yawed = rwwa::CompileScene(scene, listener, weather);
    EXPECT(yawed.contributions[0].pan < -0.99f);
    EXPECT(yawed.contributions[0].azimuthRadians < -1.56f);
}

void TestWeatherAndMaskSanitization()
{
    rwwa::SceneInput scene{};
    scene.featureCount = 3u;
    scene.features[0] = {1u, {-2.0f, 0.0f, 3.0f}, 1.0f, 0u, rwwa::kResponseMaskRain, 0};
    scene.features[1] = {2u, {0.0f, 0.0f, 3.0f}, 1.0f, 1u, rwwa::kResponseMaskWind, 0};
    scene.features[2] = {3u, {2.0f, 0.0f, 3.0f}, 1.0f, 2u, 0u, 0};
    rwwa::WeatherState weather{};
    weather.rainIntensity = 5.0f;
    weather.windSpeedMetersPerSecond = 100.0f;
    weather.windDirectionRadians = 9.0f * kPi;
    weather.windGustiness = -2.0f;
    weather.masterGainLinear = -1.0f;
    const rwwa::SceneSnapshot snapshot = rwwa::CompileScene(scene, {}, weather);
    EXPECT(snapshot.weather.rainIntensity == 1.0f);
    EXPECT(snapshot.weather.windSpeedMetersPerSecond == 60.0f);
    EXPECT(snapshot.weather.windDirectionRadians >= -kPi);
    EXPECT(snapshot.weather.windDirectionRadians <= kPi);
    EXPECT(snapshot.weather.windGustiness == 0.0f);
    EXPECT(snapshot.weather.masterGainLinear == 0.0f);
    EXPECT(snapshot.contributionCount == 2u);
    bool foundRain = false;
    bool foundWind = false;
    for (std::size_t index = 0u; index < snapshot.contributionCount; ++index)
    {
        foundRain |= snapshot.contributions[index].responseMask == rwwa::kResponseMaskRain;
        foundWind |= snapshot.contributions[index].responseMask == rwwa::kResponseMaskWind;
    }
    EXPECT(foundRain && foundWind);
}

void TestActiveSelectionAndPriority()
{
    rwwa::SceneInput scene{};
    scene.featureCount = static_cast<std::uint32_t>(scene.features.size());
    for (std::size_t index = 0u; index < scene.features.size(); ++index)
    {
        scene.features[index] = {
            static_cast<std::uint64_t>(index + 1u),
            {0.0f, 0.0f, 2.0f + static_cast<float>(index)},
            1.0f,
            3u,
            rwwa::kResponseMaskBoth,
            0};
    }
    scene.features[7].priority = 100;

    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot snapshot = rwwa::CompileScene(scene, {}, weather);
    EXPECT(snapshot.contributionCount == rwwa::kActiveContributionCount);
    EXPECT(snapshot.contributions[0].featureId == 8u);
    bool containsFarthestLowPriority = false;
    for (const rwwa::Contribution& contribution : snapshot.contributions)
    {
        containsFarthestLowPriority |= contribution.featureId == 7u;
    }
    EXPECT(!containsFarthestLowPriority);
}

void TestActiveSelectionFiltersInactiveWeatherMasks()
{
    rwwa::SceneInput scene{};
    scene.featureCount = static_cast<std::uint32_t>(scene.features.size());
    for (std::size_t index = 0u; index < scene.features.size(); ++index)
    {
        const bool windOnly = index < rwwa::kActiveContributionCount;
        scene.features[index] = {
            static_cast<std::uint64_t>(index + 1u),
            {0.0f, 0.0f, 2.0f + static_cast<float>(index)},
            1.0f,
            0u,
            windOnly ? rwwa::kResponseMaskWind : rwwa::kResponseMaskRain,
            windOnly ? 100 : 0};
    }

    rwwa::WeatherState rainWeather{};
    rainWeather.rainIntensity = 1.0f;
    rainWeather.masterGainLinear = 1.0f;
    const rwwa::SceneSnapshot rain = rwwa::CompileScene(scene, {}, rainWeather);
    EXPECT(rain.contributionCount == rwwa::kActiveContributionCount);
    for (std::size_t index = 0u; index < rain.contributionCount; ++index)
    {
        EXPECT(rain.contributions[index].featureId >= 5u);
        EXPECT(rain.contributions[index].responseMask == rwwa::kResponseMaskRain);
    }

    rwwa::WeatherState windWeather{};
    windWeather.windSpeedMetersPerSecond = 12.0f;
    windWeather.masterGainLinear = 1.0f;
    const rwwa::SceneSnapshot wind = rwwa::CompileScene(scene, {}, windWeather);
    EXPECT(wind.contributionCount == rwwa::kActiveContributionCount);
    for (std::size_t index = 0u; index < wind.contributionCount; ++index)
    {
        EXPECT(wind.contributions[index].featureId <= 4u);
        EXPECT(wind.contributions[index].responseMask == rwwa::kResponseMaskWind);
    }

    rwwa::SceneInput bothScene{};
    bothScene.featureCount = 1u;
    bothScene.features[0] = {
        99u, {0.0f, 0.0f, 3.0f}, 1.0f, 0u, rwwa::kResponseMaskBoth, 0};
    const rwwa::SceneSnapshot bothDuringRain =
        rwwa::CompileScene(bothScene, {}, rainWeather);
    const rwwa::SceneSnapshot bothDuringWind =
        rwwa::CompileScene(bothScene, {}, windWeather);
    EXPECT(bothDuringRain.contributionCount == 1u);
    EXPECT(bothDuringRain.contributions[0].responseMask == rwwa::kResponseMaskBoth);
    EXPECT(bothDuringWind.contributionCount == 1u);
    EXPECT(bothDuringWind.contributions[0].responseMask == rwwa::kResponseMaskBoth);

    rwwa::WeatherState silentWeather{};
    silentWeather.rainIntensity = 5.0e-7f;
    silentWeather.windSpeedMetersPerSecond = 5.0e-7f;
    silentWeather.masterGainLinear = 1.0f;
    const rwwa::SceneSnapshot silent = rwwa::CompileScene(scene, {}, silentWeather);
    EXPECT(silent.weather.rainIntensity == 0.0f);
    EXPECT(silent.weather.windSpeedMetersPerSecond == 0.0f);
    EXPECT(silent.contributionCount == 0u);

    std::array<float, 1024u> left{};
    std::array<float, 1024u> right{};
    rwwa::WeatherSynth synth(kSampleRate);
    synth.Process(silent, left.data(), right.data(), left.size());
    for (std::size_t frame = 0u; frame < left.size(); ++frame)
    {
        EXPECT(left[frame] == 0.0f);
        EXPECT(right[frame] == 0.0f);
    }
}

void TestMalformedSceneAndSmoothing()
{
    rwwa::SceneSnapshot previous = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Glass);
    rwwa::SceneInput scene{};
    scene.featureCount = 4u;
    scene.features[0] = {42u, {-3.0f, 0.0f, 4.0f}, 2.0f, 2u, rwwa::kResponseMaskBoth, 2};
    scene.features[1] = {12u, {0.0f, 0.0f, 1.0f}, -1.0f, 0u, rwwa::kResponseMaskRain, 0};
    scene.features[2] = {
        13u,
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f},
        1.0f,
        0u,
        rwwa::kResponseMaskRain,
        0};
    scene.features[3] = {14u, {0.0f, 0.0f, 1.0f}, 1.0f, 0xffffffffu, 0u, 0};
    const rwwa::SceneSnapshot smoothed = rwwa::CompileScene(
        scene, {}, previous.weather, &previous, 0.25f);
    EXPECT(smoothed.contributionCount == 1u);
    EXPECT(smoothed.contributions[0].featureId == 42u);
    EXPECT(smoothed.contributions[0].profileId == 2u);
    EXPECT(smoothed.contributions[0].pan < previous.contributions[0].pan);
    EXPECT(smoothed.contributions[0].pan > -1.0f);
    EXPECT(std::isfinite(smoothed.contributions[0].gain));

    scene.features[0].profileId = 0xffffffffu;
    const rwwa::SceneSnapshot fallback = rwwa::CompileScene(scene, {}, previous.weather);
    EXPECT(fallback.contributions[0].profileId == 0u);
}

void Render(
    const rwwa::SceneSnapshot& snapshot,
    std::array<float, kFrames>& left,
    std::array<float, kFrames>& right)
{
    rwwa::WeatherSynth synth(kSampleRate);
    synth.Process(snapshot, left.data(), right.data(), left.size());
}

void TestDeterminismBoundsAndSeed()
{
    const rwwa::SceneSnapshot snapshot = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Metal);
    std::array<float, kFrames> leftA{};
    std::array<float, kFrames> rightA{};
    std::array<float, kFrames> leftB{};
    std::array<float, kFrames> rightB{};
    Render(snapshot, leftA, rightA);
    Render(snapshot, leftB, rightB);

    bool nonSilent = false;
    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT(leftA[frame] == leftB[frame]);
        EXPECT(rightA[frame] == rightB[frame]);
        EXPECT(std::isfinite(leftA[frame]));
        EXPECT(std::isfinite(rightA[frame]));
        EXPECT(leftA[frame] >= -1.0f && leftA[frame] <= 1.0f);
        EXPECT(rightA[frame] >= -1.0f && rightA[frame] <= 1.0f);
        nonSilent |= leftA[frame] != 0.0f || rightA[frame] != 0.0f;
    }
    EXPECT(nonSilent);

    rwwa::SceneSnapshot differentSeed = snapshot;
    differentSeed.weather.seed ^= 0xf00dcafeu;
    std::array<float, kFrames> changedLeft{};
    std::array<float, kFrames> changedRight{};
    Render(differentSeed, changedLeft, changedRight);
    bool differs = false;
    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        differs |= leftA[frame] != changedLeft[frame] || rightA[frame] != changedRight[frame];
    }
    EXPECT(differs);
}

void TestBlockSizeDeterminism()
{
    const rwwa::SceneSnapshot snapshot = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Tile);
    std::array<float, kFrames> wholeLeft{};
    std::array<float, kFrames> wholeRight{};
    std::array<float, kFrames> blockedLeft{};
    std::array<float, kFrames> blockedRight{};

    rwwa::WeatherSynth wholeSynth(kSampleRate);
    wholeSynth.Process(snapshot, wholeLeft.data(), wholeRight.data(), kFrames);

    constexpr std::array<std::size_t, 7> blockPattern{{1u, 7u, 64u, 255u, 511u, 1024u, 37u}};
    rwwa::WeatherSynth blockedSynth(kSampleRate);
    std::size_t offset = 0u;
    std::size_t patternIndex = 0u;
    while (offset < kFrames)
    {
        const std::size_t blockSize = std::min(
            blockPattern[patternIndex % blockPattern.size()], kFrames - offset);
        blockedSynth.Process(
            snapshot, blockedLeft.data() + offset, blockedRight.data() + offset, blockSize);
        offset += blockSize;
        ++patternIndex;
    }

    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT(wholeLeft[frame] == blockedLeft[frame]);
        EXPECT(wholeRight[frame] == blockedRight[frame]);
    }
}

void TestSeedChangeResetsSequence()
{
    const rwwa::SceneSnapshot first = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Glass);
    rwwa::SceneSnapshot second = first;
    second.weather.seed = 0x87654321u;
    std::array<float, kFrames> discardedLeft{};
    std::array<float, kFrames> discardedRight{};
    std::array<float, kFrames> changedLeft{};
    std::array<float, kFrames> changedRight{};
    std::array<float, kFrames> freshLeft{};
    std::array<float, kFrames> freshRight{};

    rwwa::WeatherSynth changedSynth(kSampleRate);
    changedSynth.Process(first, discardedLeft.data(), discardedRight.data(), kFrames);
    changedSynth.Process(second, changedLeft.data(), changedRight.data(), kFrames);
    rwwa::WeatherSynth freshSynth(kSampleRate);
    freshSynth.Process(second, freshLeft.data(), freshRight.data(), kFrames);
    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        EXPECT(changedLeft[frame] == freshLeft[frame]);
        EXPECT(changedRight[frame] == freshRight[frame]);
    }
}

void TestRainStartMatchesFreshAfterDryPreroll()
{
    constexpr std::size_t preRollFrames = 8192u;
    constexpr std::size_t probeFrames = 4096u;
    std::array<float, preRollFrames> discardedLeft{};
    std::array<float, preRollFrames> discardedRight{};
    std::array<float, probeFrames> primedLeft{};
    std::array<float, probeFrames> primedRight{};
    std::array<float, probeFrames> freshLeft{};
    std::array<float, probeFrames> freshRight{};

    rwwa::WeatherState dryWeather{};
    dryWeather.masterGainLinear = 0.0f;
    const rwwa::SceneSnapshot dry = MakeFeatureWeatherSnapshot(
        100u, rwwa::kResponseMaskRain, dryWeather);

    rwwa::WeatherState rainWeather{};
    rainWeather.rainIntensity = 0.7f;
    rainWeather.seed = dry.weather.seed;
    rainWeather.masterGainLinear = 1.0f;
    const rwwa::SceneSnapshot rain = MakeFeatureWeatherSnapshot(
        100u, rwwa::kResponseMaskRain, rainWeather);

    rwwa::WeatherSynth primedSynth(kSampleRate);
    primedSynth.Process(dry, discardedLeft.data(), discardedRight.data(), discardedLeft.size());
    primedSynth.Process(rain, primedLeft.data(), primedRight.data(), primedLeft.size());

    rwwa::WeatherSynth freshSynth(kSampleRate);
    freshSynth.Process(rain, freshLeft.data(), freshRight.data(), freshLeft.size());

    for (std::size_t frame = 0u; frame < probeFrames; ++frame)
    {
        EXPECT(primedLeft[frame] == freshLeft[frame]);
        EXPECT(primedRight[frame] == freshRight[frame]);
    }
}

void TestRainStartMatchesFreshAfterWindOnlyPreroll()
{
    constexpr std::size_t preRollFrames = 8192u;
    constexpr std::size_t drainFrames = 4096u;
    constexpr std::size_t probeFrames = 4096u;
    std::array<float, preRollFrames> windLeft{};
    std::array<float, preRollFrames> windRight{};
    std::array<float, drainFrames> drainLeft{};
    std::array<float, drainFrames> drainRight{};
    std::array<float, probeFrames> primedLeft{};
    std::array<float, probeFrames> primedRight{};
    std::array<float, probeFrames> freshLeft{};
    std::array<float, probeFrames> freshRight{};

    rwwa::WeatherState windWeather{};
    windWeather.windSpeedMetersPerSecond = 12.0f;
    windWeather.windGustiness = 0.5f;
    windWeather.masterGainLinear = 0.0f;
    const rwwa::SceneSnapshot windOnly = MakeFeatureWeatherSnapshot(
        200u, rwwa::kResponseMaskWind, windWeather);

    rwwa::WeatherState dryWeather{};
    dryWeather.seed = windWeather.seed;
    dryWeather.masterGainLinear = 0.0f;
    const rwwa::SceneSnapshot dry = MakeFeatureWeatherSnapshot(
        200u, rwwa::kResponseMaskWind, dryWeather);

    rwwa::WeatherState rainWeather{};
    rainWeather.rainIntensity = 0.7f;
    rainWeather.seed = windWeather.seed;
    rainWeather.masterGainLinear = 1.0f;
    const rwwa::SceneSnapshot rain = MakeFeatureWeatherSnapshot(
        300u, rwwa::kResponseMaskRain, rainWeather);

    rwwa::WeatherSynth primedSynth(kSampleRate);
    primedSynth.Process(windOnly, windLeft.data(), windRight.data(), windLeft.size());
    for (std::size_t block = 0u; block < 80u; ++block)
    {
        primedSynth.Process(dry, drainLeft.data(), drainRight.data(), drainLeft.size());
    }
    primedSynth.Process(rain, primedLeft.data(), primedRight.data(), primedLeft.size());

    rwwa::WeatherSynth freshSynth(kSampleRate);
    freshSynth.Process(rain, freshLeft.data(), freshRight.data(), freshLeft.size());

    for (std::size_t frame = 0u; frame < probeFrames; ++frame)
    {
        EXPECT(primedLeft[frame] == freshLeft[frame]);
        EXPECT(primedRight[frame] == freshRight[frame]);
    }
}

void AssertSignatureChangeMatchesNewVoice(
    std::uint32_t oldProfile,
    std::uint32_t oldMask,
    std::uint32_t newProfile,
    std::uint32_t newMask)
{
    rwwa::SceneSnapshot oldSnapshot{};
    oldSnapshot.weather.rainIntensity = 0.82f;
    oldSnapshot.weather.windSpeedMetersPerSecond = 12.0f;
    oldSnapshot.weather.windDirectionRadians = 0.3f;
    oldSnapshot.weather.windGustiness = 0.55f;
    oldSnapshot.weather.seed = 0x71c8a3d5u;
    oldSnapshot.weather.masterGainLinear = 1.0f;
    oldSnapshot.contributionCount = 1u;
    oldSnapshot.contributions[0].featureId = 42u;
    oldSnapshot.contributions[0].profileId = oldProfile;
    oldSnapshot.contributions[0].responseMask = oldMask;
    oldSnapshot.contributions[0].radius = 2.0f;
    oldSnapshot.contributions[0].gain = 0.0f;
    oldSnapshot.contributions[0].pan = 0.35f;
    oldSnapshot.contributions[0].azimuthRadians = 0.42f;

    rwwa::SceneSnapshot changedSignature = oldSnapshot;
    changedSignature.contributions[0].profileId = newProfile;
    changedSignature.contributions[0].responseMask = newMask;
    changedSignature.contributions[0].gain = 0.9f;

    rwwa::SceneSnapshot newIdentity = changedSignature;
    newIdentity.contributions[0].featureId = 9001u;

    constexpr std::size_t preRollFrames = 8192u;
    constexpr std::size_t probeFrames = 4096u;
    std::array<float, preRollFrames> discardedLeft{};
    std::array<float, preRollFrames> discardedRight{};
    std::array<float, probeFrames> changedLeft{};
    std::array<float, probeFrames> changedRight{};
    std::array<float, probeFrames> freshLeft{};
    std::array<float, probeFrames> freshRight{};

    rwwa::WeatherSynth changedSynth(kSampleRate);
    rwwa::WeatherSynth freshVoiceSynth(kSampleRate);
    changedSynth.Process(
        oldSnapshot, discardedLeft.data(), discardedRight.data(), discardedLeft.size());
    freshVoiceSynth.Process(
        oldSnapshot, discardedLeft.data(), discardedRight.data(), discardedLeft.size());
    changedSynth.Process(
        changedSignature, changedLeft.data(), changedRight.data(), changedLeft.size());
    freshVoiceSynth.Process(
        newIdentity, freshLeft.data(), freshRight.data(), freshLeft.size());

    for (std::size_t frame = 0u; frame < probeFrames; ++frame)
    {
        EXPECT(changedLeft[frame] == freshLeft[frame]);
        EXPECT(changedRight[frame] == freshRight[frame]);
    }
}

void TestVoiceSignatureChangesResetAcousticState()
{
    AssertSignatureChangeMatchesNewVoice(
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal),
        rwwa::kResponseMaskRain,
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Wood),
        rwwa::kResponseMaskRain);
    AssertSignatureChangeMatchesNewVoice(
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal),
        rwwa::kResponseMaskRain,
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal),
        rwwa::kResponseMaskWind);
}

void TestMasksAndWindDirection()
{
    rwwa::SceneSnapshot rainOnly = MakeSingleFeatureSnapshot(
        rwwa::ResponseProfile::Metal, rwwa::kResponseMaskRain);
    rainOnly.weather.windSpeedMetersPerSecond = 0.0f;
    rwwa::SceneSnapshot windOnly = MakeSingleFeatureSnapshot(
        rwwa::ResponseProfile::Wood, rwwa::kResponseMaskWind);
    windOnly.weather.rainIntensity = 0.0f;
    const AudioMetrics rainMetrics = RenderMetrics(rainOnly, 2u);
    const AudioMetrics windMetrics = RenderMetrics(windOnly, 2u);
    EXPECT(rainMetrics.rms > 0.001);
    EXPECT(windMetrics.rms > 0.001);
    EXPECT(std::abs(rainMetrics.rms - windMetrics.rms) > 0.0001);

    const AudioMetrics windFromLeft = RenderMetrics(MakeOpenWindSnapshot(-kPi * 0.5f, 0.4f), 2u);
    const AudioMetrics windFromRight = RenderMetrics(MakeOpenWindSnapshot(kPi * 0.5f, 0.4f), 2u);
    EXPECT(std::abs(windFromLeft.stereoCorrelation - windFromRight.stereoCorrelation) < 0.08);

    auto channelRms = [](const rwwa::SceneSnapshot& snapshot)
    {
        rwwa::WeatherSynth synth(kSampleRate);
        std::vector<float> left(kSampleRate * 2u);
        std::vector<float> right(left.size());
        synth.Process(snapshot, left.data(), right.data(), left.size());
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (std::size_t frame = kSampleRate; frame < left.size(); ++frame)
        {
            leftEnergy += static_cast<double>(left[frame]) * left[frame];
            rightEnergy += static_cast<double>(right[frame]) * right[frame];
        }
        return std::array<double, 2>{{leftEnergy, rightEnergy}};
    };
    const auto fromLeftEnergy = channelRms(MakeOpenWindSnapshot(-kPi * 0.5f, 0.4f));
    const auto fromRightEnergy = channelRms(MakeOpenWindSnapshot(kPi * 0.5f, 0.4f));
    EXPECT(fromLeftEnergy[0] > fromLeftEnergy[1]);
    EXPECT(fromRightEnergy[1] > fromRightEnergy[0]);
}

void TestLongTailAndZeroWeather()
{
    constexpr std::size_t blockSize = 4096u;
    std::array<float, blockSize> left{};
    std::array<float, blockSize> right{};
    const rwwa::SceneSnapshot wet = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Metal);
    rwwa::SceneSnapshot silent = wet;
    silent.weather.rainIntensity = 0.0f;
    silent.weather.windSpeedMetersPerSecond = 0.0f;

    rwwa::WeatherSynth synth(kSampleRate);
    synth.Process(wet, left.data(), right.data(), blockSize);
    for (std::size_t block = 0u; block < 80u; ++block)
    {
        synth.Process(silent, left.data(), right.data(), blockSize);
        for (std::size_t frame = 0u; frame < blockSize; ++frame)
        {
            EXPECT(std::isfinite(left[frame]));
            EXPECT(std::isfinite(right[frame]));
            EXPECT(left[frame] >= -1.0f && left[frame] <= 1.0f);
            EXPECT(right[frame] >= -1.0f && right[frame] <= 1.0f);
        }
    }
    for (std::size_t frame = 0u; frame < blockSize; ++frame)
    {
        EXPECT(left[frame] == 0.0f);
        EXPECT(right[frame] == 0.0f);
    }

    rwwa::WeatherState dryWeather{};
    dryWeather.rainIntensity = std::numeric_limits<float>::denorm_min();
    dryWeather.windSpeedMetersPerSecond = std::numeric_limits<float>::denorm_min();
    const rwwa::SceneSnapshot dry = rwwa::CompileScene({}, {}, dryWeather);
    rwwa::WeatherSynth drySynth(kSampleRate);
    drySynth.Process(dry, left.data(), right.data(), blockSize);
    for (std::size_t frame = 0u; frame < blockSize; ++frame)
    {
        EXPECT(left[frame] == 0.0f);
        EXPECT(right[frame] == 0.0f);
    }
}

void TestProfileDistinction()
{
    constexpr std::size_t kProfileCount =
        static_cast<std::size_t>(rwwa::ResponseProfile::Plastic) + 1u;
    std::array<std::array<float, kFrames>, kProfileCount> profileLeft{};
    std::array<std::array<float, kFrames>, kProfileCount> profileRight{};
    for (std::uint32_t profile = 0u; profile < kProfileCount; ++profile)
    {
        rwwa::SceneSnapshot snapshot = MakeSingleFeatureSnapshot(
            static_cast<rwwa::ResponseProfile>(profile), rwwa::kResponseMaskRain);
        snapshot.weather.windSpeedMetersPerSecond = 0.0f;
        Render(snapshot, profileLeft[profile], profileRight[profile]);
    }

    for (std::size_t first = 0u; first < kProfileCount; ++first)
    {
        for (std::size_t second = first + 1u; second < kProfileCount; ++second)
        {
            double absoluteDifference = 0.0;
            for (std::size_t frame = 0u; frame < kFrames; ++frame)
            {
                absoluteDifference += std::abs(static_cast<double>(
                    profileLeft[first][frame] - profileLeft[second][frame]));
                absoluteDifference += std::abs(static_cast<double>(
                    profileRight[first][frame] - profileRight[second][frame]));
            }
            EXPECT(absoluteDifference > 0.05);
        }
    }
}

void TestPerceptualGuardMetrics()
{
    rwwa::SceneSnapshot rain = MakeSingleFeatureSnapshot(
        rwwa::ResponseProfile::Metal, rwwa::kResponseMaskRain);
    rain.weather.windSpeedMetersPerSecond = 0.0f;
    const AudioMetrics rainMetrics = RenderMetrics(rain, 3u);
    const AudioMetrics steadyWind = RenderMetrics(MakeOpenWindSnapshot(0.4f, 0.0f), 3u);
    const AudioMetrics gustyWind = RenderMetrics(MakeOpenWindSnapshot(0.4f, 1.0f), 3u);

    std::cout << "rain metrics: rms=" << rainMetrics.rms
              << " crest=" << rainMetrics.crestFactor
              << " envCV=" << rainMetrics.envelopeCv
              << " corr=" << rainMetrics.stereoCorrelation
              << " diffRatio=" << rainMetrics.firstDifferenceRatio << '\n';
    std::cout << "wind metrics: rms=" << steadyWind.rms
              << " crest=" << steadyWind.crestFactor
              << " envCV=" << steadyWind.envelopeCv
              << " corr=" << steadyWind.stereoCorrelation
              << " diffRatio=" << steadyWind.firstDifferenceRatio << '\n';
    std::cout << "gust metrics: rms=" << gustyWind.rms
              << " crest=" << gustyWind.crestFactor
              << " envCV=" << gustyWind.envelopeCv
              << " corr=" << gustyWind.stereoCorrelation
              << " diffRatio=" << gustyWind.firstDifferenceRatio << '\n';

    EXPECT(rainMetrics.rms > 0.002 && rainMetrics.rms < 0.35);
    EXPECT(rainMetrics.crestFactor > 3.2);
    EXPECT(rainMetrics.envelopeCv > 0.035);
    EXPECT(rainMetrics.firstDifferenceRatio < 1.15);
    EXPECT(std::abs(rainMetrics.stereoCorrelation) < 0.98);

    EXPECT(steadyWind.rms > 0.002 && steadyWind.rms < 0.35);
    EXPECT(steadyWind.firstDifferenceRatio < 0.75);
    EXPECT(std::abs(steadyWind.stereoCorrelation) < 0.98);
    EXPECT(gustyWind.envelopeCv > steadyWind.envelopeCv + 0.025);
}
} // namespace

int main()
{
    TestListenerRelativePanning();
    TestWeatherAndMaskSanitization();
    TestActiveSelectionAndPriority();
    TestActiveSelectionFiltersInactiveWeatherMasks();
    TestMalformedSceneAndSmoothing();
    TestDeterminismBoundsAndSeed();
    TestBlockSizeDeterminism();
    TestSeedChangeResetsSequence();
    TestRainStartMatchesFreshAfterDryPreroll();
    TestRainStartMatchesFreshAfterWindOnlyPreroll();
    TestVoiceSignatureChangesResetAcousticState();
    TestMasksAndWindDirection();
    TestLongTailAndZeroWeather();
    TestProfileDistinction();
    TestPerceptualGuardMetrics();
    if (g_failureCount != 0)
    {
        std::cerr << g_failureCount << " expectation(s) failed\n";
        return 1;
    }
    return 0;
}
