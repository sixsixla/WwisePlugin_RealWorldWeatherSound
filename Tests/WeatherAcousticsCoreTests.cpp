#ifdef NDEBUG
#undef NDEBUG
#endif

#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kFrames = 8192u;

rwwa::SceneSnapshot MakeSingleFeatureSnapshot(
    rwwa::ResponseProfile profile,
    rwwa::Vec3 position = {3.0f, 0.0f, 4.0f})
{
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        42u,
        position,
        2.0f,
        static_cast<std::uint32_t>(profile),
        rwwa::kResponseMaskRain,
        2};
    rwwa::WeatherState weather{};
    weather.rainIntensity = 0.8f;
    weather.seed = 0x12345678u;
    weather.geometryEnabled = true;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene(scene, {}, weather);
}

void TestListenerRelativePanning()
{
    const rwwa::SceneSnapshot right = MakeSingleFeatureSnapshot(
        rwwa::ResponseProfile::Metal,
        {5.0f, 0.0f, 0.0f});
    assert(right.contributionCount == 1u);
    assert(right.contributions[0].pan > 0.99f);

    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        7u,
        {0.0f, 0.0f, 5.0f},
        1.0f,
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Wood),
        rwwa::kResponseMaskRain,
        1};
    rwwa::ListenerState listener{};
    listener.yawRadians = kPi * 0.5f;
    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot yawed = rwwa::CompileScene(scene, listener, weather);
    assert(yawed.contributions[0].pan < -0.99f);
    assert(yawed.contributions[0].azimuthRadians < -1.56f);
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
            static_cast<std::uint32_t>(rwwa::ResponseProfile::Tile),
            rwwa::kResponseMaskRain,
            0};
    }
    scene.features[7].priority = 100;

    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot snapshot = rwwa::CompileScene(scene, {}, weather);
    assert(snapshot.contributionCount == rwwa::kActiveContributionCount);
    assert(snapshot.contributions[0].featureId == 8u);

    bool containsFarthestLowPriority = false;
    for (const rwwa::Contribution& contribution : snapshot.contributions)
    {
        containsFarthestLowPriority |= contribution.featureId == 7u;
    }
    assert(!containsFarthestLowPriority);
}

void TestSmoothingAndInputSafety()
{
    rwwa::SceneSnapshot previous = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Glass);
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        42u,
        {-3.0f, 0.0f, 4.0f},
        2.0f,
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Glass),
        rwwa::kResponseMaskRain,
        2};
    rwwa::WeatherState weather = previous.weather;
    weather.rainIntensity = 5.0f;
    weather.masterGainLinear = -1.0f;
    const rwwa::SceneSnapshot smoothed = rwwa::CompileScene(
        scene,
        {},
        weather,
        &previous,
        0.25f);
    assert(smoothed.weather.rainIntensity == 1.0f);
    assert(smoothed.weather.masterGainLinear == 0.0f);
    assert(smoothed.contributions[0].pan < previous.contributions[0].pan);
    assert(smoothed.contributions[0].pan > -1.0f);
    assert(std::isfinite(smoothed.contributions[0].gain));
}

void TestSceneInputBoundaries()
{
    rwwa::SceneInput oversized{};
    oversized.featureCount = 1000u;
    for (std::size_t index = 0u; index < oversized.features.size(); ++index)
    {
        oversized.features[index] = {
            static_cast<std::uint64_t>(index + 1u),
            {static_cast<float>(index), 0.0f, 3.0f},
            1.0f,
            static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal),
            rwwa::kResponseMaskRain,
            static_cast<std::int32_t>(index)};
    }

    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot clamped = rwwa::CompileScene(oversized, {}, weather);
    assert(clamped.contributionCount == rwwa::kActiveContributionCount);

    weather.geometryEnabled = false;
    const rwwa::SceneSnapshot disabled = rwwa::CompileScene(oversized, {}, weather);
    assert(disabled.contributionCount == 0u);

    rwwa::SceneInput malformed{};
    malformed.featureCount = 4u;
    malformed.features[0] = {
        11u, {0.0f, 0.0f, 1.0f}, -1.0f, 0u, rwwa::kResponseMaskRain, 0};
    malformed.features[1] = {
        12u,
        {0.0f, 0.0f, 1.0f},
        std::numeric_limits<float>::quiet_NaN(),
        0u,
        rwwa::kResponseMaskRain,
        0};
    malformed.features[2] = {
        13u,
        {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f},
        1.0f,
        0u,
        rwwa::kResponseMaskRain,
        0};
    malformed.features[3] = {
        14u, {0.0f, 0.0f, 1.0f}, 1.0f, 0u, rwwa::kResponseMaskRain, 0};
    weather.geometryEnabled = true;
    const rwwa::SceneSnapshot filtered = rwwa::CompileScene(malformed, {}, weather);
    assert(filtered.contributionCount == 1u);
    assert(filtered.contributions[0].featureId == 14u);
}

void TestInvalidProfileFallback()
{
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        91u, {0.0f, 0.0f, 2.0f}, 1.0f, 0xffffffffu, rwwa::kResponseMaskRain, 0};
    rwwa::WeatherState weather{};
    weather.rainIntensity = 1.0f;
    const rwwa::SceneSnapshot snapshot = rwwa::CompileScene(scene, {}, weather);
    assert(snapshot.contributionCount == 1u);
    assert(snapshot.contributions[0].profileId ==
        static_cast<std::uint32_t>(rwwa::ResponseProfile::Metal));
}

void Render(
    const rwwa::SceneSnapshot& snapshot,
    std::array<float, kFrames>& left,
    std::array<float, kFrames>& right)
{
    rwwa::RainSynth synth(48000u);
    synth.Process(snapshot, left.data(), right.data(), left.size());
}

void TestDeterminismAndBounds()
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
        assert(leftA[frame] == leftB[frame]);
        assert(rightA[frame] == rightB[frame]);
        assert(std::isfinite(leftA[frame]));
        assert(std::isfinite(rightA[frame]));
        assert(leftA[frame] >= -1.0f && leftA[frame] <= 1.0f);
        assert(rightA[frame] >= -1.0f && rightA[frame] <= 1.0f);
        nonSilent |= leftA[frame] != 0.0f || rightA[frame] != 0.0f;
    }
    assert(nonSilent);
}

void TestBlockSizeDeterminism()
{
    const rwwa::SceneSnapshot snapshot = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Tile);
    std::array<float, kFrames> wholeLeft{};
    std::array<float, kFrames> wholeRight{};
    std::array<float, kFrames> blockedLeft{};
    std::array<float, kFrames> blockedRight{};

    rwwa::RainSynth wholeSynth(48000u);
    wholeSynth.Process(snapshot, wholeLeft.data(), wholeRight.data(), kFrames);

    constexpr std::array<std::size_t, 7> blockPattern{{1u, 7u, 64u, 255u, 511u, 1024u, 37u}};
    rwwa::RainSynth blockedSynth(48000u);
    std::size_t offset = 0u;
    std::size_t patternIndex = 0u;
    while (offset < kFrames)
    {
        const std::size_t blockSize = std::min(
            blockPattern[patternIndex % blockPattern.size()],
            kFrames - offset);
        blockedSynth.Process(
            snapshot,
            blockedLeft.data() + offset,
            blockedRight.data() + offset,
            blockSize);
        offset += blockSize;
        ++patternIndex;
    }

    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        assert(wholeLeft[frame] == blockedLeft[frame]);
        assert(wholeRight[frame] == blockedRight[frame]);
    }
}

void TestSeedChangeResetsSequence()
{
    rwwa::SceneSnapshot first = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Glass);
    rwwa::SceneSnapshot second = first;
    second.weather.seed = 0x87654321u;
    std::array<float, kFrames> firstLeft{};
    std::array<float, kFrames> firstRight{};
    std::array<float, kFrames> changedLeft{};
    std::array<float, kFrames> changedRight{};
    std::array<float, kFrames> freshLeft{};
    std::array<float, kFrames> freshRight{};

    rwwa::RainSynth changedSynth(48000u);
    changedSynth.Process(first, firstLeft.data(), firstRight.data(), kFrames);
    changedSynth.Process(second, changedLeft.data(), changedRight.data(), kFrames);
    rwwa::RainSynth freshSynth(48000u);
    freshSynth.Process(second, freshLeft.data(), freshRight.data(), kFrames);

    bool seedsDiffer = false;
    for (std::size_t frame = 0u; frame < kFrames; ++frame)
    {
        assert(changedLeft[frame] == freshLeft[frame]);
        assert(changedRight[frame] == freshRight[frame]);
        seedsDiffer |= firstLeft[frame] != changedLeft[frame] ||
            firstRight[frame] != changedRight[frame];
    }
    assert(seedsDiffer);
}

void TestLongTailAndDenormalSilence()
{
    constexpr std::size_t blockSize = 4096u;
    std::array<float, blockSize> left{};
    std::array<float, blockSize> right{};
    const rwwa::SceneSnapshot wet = MakeSingleFeatureSnapshot(rwwa::ResponseProfile::Metal);
    rwwa::SceneSnapshot silent = wet;
    silent.weather.rainIntensity = 0.0f;

    rwwa::RainSynth synth(48000u);
    synth.Process(wet, left.data(), right.data(), blockSize);
    for (std::size_t block = 0u; block < 80u; ++block)
    {
        synth.Process(silent, left.data(), right.data(), blockSize);
        for (std::size_t frame = 0u; frame < blockSize; ++frame)
        {
            assert(std::isfinite(left[frame]));
            assert(std::isfinite(right[frame]));
            assert(left[frame] >= -1.0f && left[frame] <= 1.0f);
            assert(right[frame] >= -1.0f && right[frame] <= 1.0f);
        }
    }
    for (std::size_t frame = 0u; frame < blockSize; ++frame)
    {
        assert(left[frame] == 0.0f);
        assert(right[frame] == 0.0f);
    }

    rwwa::SceneSnapshot denormalInput = wet;
    denormalInput.weather.rainIntensity = std::numeric_limits<float>::denorm_min();
    rwwa::RainSynth denormalSynth(48000u);
    denormalSynth.Process(denormalInput, left.data(), right.data(), blockSize);
    for (std::size_t frame = 0u; frame < blockSize; ++frame)
    {
        assert(left[frame] == 0.0f);
        assert(right[frame] == 0.0f);
    }
}

void TestProfileDistinction()
{
    std::array<std::array<float, kFrames>, 4> profileLeft{};
    std::array<std::array<float, kFrames>, 4> profileRight{};
    for (std::uint32_t profile = 0u; profile < 4u; ++profile)
    {
        const rwwa::SceneSnapshot snapshot = MakeSingleFeatureSnapshot(
            static_cast<rwwa::ResponseProfile>(profile));
        Render(snapshot, profileLeft[profile], profileRight[profile]);
    }

    for (std::size_t first = 0u; first < 4u; ++first)
    {
        for (std::size_t second = first + 1u; second < 4u; ++second)
        {
            double absoluteDifference = 0.0;
            for (std::size_t frame = 0u; frame < kFrames; ++frame)
            {
                absoluteDifference += std::abs(static_cast<double>(
                    profileLeft[first][frame] - profileLeft[second][frame]));
                absoluteDifference += std::abs(static_cast<double>(
                    profileRight[first][frame] - profileRight[second][frame]));
            }
            assert(absoluteDifference > 0.01);
        }
    }
}
} // namespace

int main()
{
    TestListenerRelativePanning();
    TestActiveSelectionAndPriority();
    TestSmoothingAndInputSafety();
    TestSceneInputBoundaries();
    TestInvalidProfileFallback();
    TestDeterminismAndBounds();
    TestBlockSizeDeterminism();
    TestSeedChangeResetsSequence();
    TestLongTailAndDenormalSilence();
    TestProfileDistinction();
    return 0;
}
