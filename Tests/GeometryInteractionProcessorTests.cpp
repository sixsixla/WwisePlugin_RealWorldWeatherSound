#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kSampleRate = 48000u;
constexpr std::size_t kFrameCount = 16384u;

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

struct StereoBuffer
{
    std::vector<float> left;
    std::vector<float> right;
};

rwwa::SceneSnapshot MakeSnapshot(
    rwwa::ResponseProfile profile,
    std::uint32_t responseMask = rwwa::kResponseMaskBoth,
    float pan = 0.35f)
{
    rwwa::SceneSnapshot snapshot{};
    snapshot.contributionCount = 1u;
    snapshot.contributions[0].featureId = 91u;
    snapshot.contributions[0].profileId = static_cast<std::uint32_t>(profile);
    snapshot.contributions[0].responseMask = responseMask;
    snapshot.contributions[0].radius = 2.0f;
    snapshot.contributions[0].gain = 0.86f;
    snapshot.contributions[0].pan = pan;
    return snapshot;
}

StereoBuffer MakeInput(std::size_t frameCount = kFrameCount)
{
    StereoBuffer result{std::vector<float>(frameCount), std::vector<float>(frameCount)};
    for (std::size_t frame = 0u; frame < frameCount; ++frame)
    {
        const float time = static_cast<float>(frame) / static_cast<float>(kSampleRate);
        const float impulse = frame % 401u == 0u
            ? ((frame / 401u) % 2u == 0u ? 0.42f : -0.42f)
            : 0.0f;
        result.left[frame] =
            0.13f * std::sin(2.0f * kPi * 110.0f * time) +
            0.055f * std::sin(2.0f * kPi * 740.0f * time) + impulse;
        result.right[frame] =
            0.12f * std::sin(2.0f * kPi * 127.0f * time + 0.2f) +
            0.050f * std::sin(2.0f * kPi * 930.0f * time) - 0.72f * impulse;
    }
    return result;
}

rwwa::InteractionSettings MakeSettings(rwwa::InputRole role)
{
    rwwa::InteractionSettings settings{};
    settings.inputRole = role;
    settings.wetMix = 0.82f;
    settings.responseGainLinear = 0.78f;
    settings.transientSensitivity = 0.74f;
    return settings;
}

StereoBuffer Render(
    const rwwa::SceneSnapshot& snapshot,
    const rwwa::InteractionSettings& settings,
    const StereoBuffer& input)
{
    StereoBuffer output = input;
    float* channels[] = {output.left.data(), output.right.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);
    processor.Process(snapshot, settings, channels, 2u, output.left.size());
    return output;
}

double Difference(const StereoBuffer& left, const StereoBuffer& right)
{
    EXPECT(left.left.size() == right.left.size());
    EXPECT(left.right.size() == right.right.size());
    double difference = 0.0;
    for (std::size_t frame = 0u; frame < left.left.size(); ++frame)
    {
        difference += std::abs(static_cast<double>(left.left[frame] - right.left[frame]));
        difference += std::abs(static_cast<double>(left.right[frame] - right.right[frame]));
    }
    return difference;
}

double WetEnergy(const StereoBuffer& output, const StereoBuffer& dry, std::size_t channel)
{
    const std::vector<float>& outputSamples = channel == 0u ? output.left : output.right;
    const std::vector<float>& drySamples = channel == 0u ? dry.left : dry.right;
    double energy = 0.0;
    for (std::size_t frame = 0u; frame < outputSamples.size(); ++frame)
    {
        const double wet = static_cast<double>(outputSamples[frame] - drySamples[frame]);
        energy += wet * wet;
    }
    return energy;
}

void TestDeterminism()
{
    const StereoBuffer input = MakeInput();
    const rwwa::SceneSnapshot snapshot = MakeSnapshot(rwwa::ResponseProfile::Metal);
    const rwwa::InteractionSettings settings = MakeSettings(rwwa::InputRole::Generic);
    const StereoBuffer first = Render(snapshot, settings, input);
    const StereoBuffer second = Render(snapshot, settings, input);

    EXPECT(first.left == second.left);
    EXPECT(first.right == second.right);
    EXPECT(Difference(first, input) > 0.01);
}

void TestWetZeroIsSampleTransparent()
{
    StereoBuffer buffer = MakeInput(2048u);
    const StereoBuffer original = buffer;
    float* channels[] = {buffer.left.data(), buffer.right.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);

    rwwa::InteractionSettings settings = MakeSettings(rwwa::InputRole::RainBed);
    settings.wetMix = 0.0f;
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Glass),
        settings,
        channels,
        2u,
        buffer.left.size());

    EXPECT(std::memcmp(
        buffer.left.data(), original.left.data(), buffer.left.size() * sizeof(float)) == 0);
    EXPECT(std::memcmp(
        buffer.right.data(), original.right.data(), buffer.right.size() * sizeof(float)) == 0);

    settings.wetMix = 1.0f;
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Glass),
        settings,
        channels,
        2u,
        buffer.left.size());
    const StereoBuffer wetBuffer = buffer;
    settings.wetMix = 0.0f;
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Glass),
        settings,
        channels,
        2u,
        buffer.left.size());
    EXPECT(std::memcmp(
        buffer.left.data(), wetBuffer.left.data(), buffer.left.size() * sizeof(float)) == 0);
    EXPECT(std::memcmp(
        buffer.right.data(), wetBuffer.right.data(), buffer.right.size() * sizeof(float)) == 0);
}

void TestDisabledOrEmptyGeometryIsSampleTransparent()
{
    StereoBuffer buffer = MakeInput(2048u);
    buffer.left[11] = 2.0f;
    buffer.right[29] = std::numeric_limits<float>::quiet_NaN();
    float* channels[] = {buffer.left.data(), buffer.right.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);
    const rwwa::InteractionSettings settings = MakeSettings(rwwa::InputRole::Generic);

    rwwa::SceneSnapshot disabled = MakeSnapshot(rwwa::ResponseProfile::Metal);
    disabled.weather.geometryEnabled = false;
    StereoBuffer original = buffer;
    processor.Process(disabled, settings, channels, 2u, buffer.left.size());
    EXPECT(std::memcmp(
        buffer.left.data(), original.left.data(), buffer.left.size() * sizeof(float)) == 0);
    EXPECT(std::memcmp(
        buffer.right.data(), original.right.data(), buffer.right.size() * sizeof(float)) == 0);

    rwwa::SceneSnapshot empty{};
    original = buffer;
    processor.Process(empty, settings, channels, 2u, buffer.left.size());
    EXPECT(std::memcmp(
        buffer.left.data(), original.left.data(), buffer.left.size() * sizeof(float)) == 0);
    EXPECT(std::memcmp(
        buffer.right.data(), original.right.data(), buffer.right.size() * sizeof(float)) == 0);
}

void TestSilenceIsStable()
{
    constexpr std::size_t frames = 32768u;
    StereoBuffer silence{std::vector<float>(frames), std::vector<float>(frames)};
    float* channels[] = {silence.left.data(), silence.right.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Wood),
        MakeSettings(rwwa::InputRole::WindBed),
        channels,
        2u,
        frames);

    for (std::size_t frame = 0u; frame < frames; ++frame)
    {
        EXPECT(silence.left[frame] == 0.0f);
        EXPECT(silence.right[frame] == 0.0f);
        EXPECT(std::isfinite(silence.left[frame]));
        EXPECT(std::isfinite(silence.right[frame]));
    }
}

void TestProfilesAndRolesAreDistinct()
{
    const StereoBuffer input = MakeInput();
    const rwwa::InteractionSettings generic = MakeSettings(rwwa::InputRole::Generic);
    std::array<StereoBuffer, 4> profiles{};
    for (std::uint32_t profile = 0u; profile < profiles.size(); ++profile)
    {
        profiles[profile] = Render(
            MakeSnapshot(static_cast<rwwa::ResponseProfile>(profile)),
            generic,
            input);
    }
    for (std::size_t first = 0u; first < profiles.size(); ++first)
    {
        for (std::size_t second = first + 1u; second < profiles.size(); ++second)
        {
            EXPECT(Difference(profiles[first], profiles[second]) > 0.01);
        }
    }

    const rwwa::SceneSnapshot snapshot = MakeSnapshot(rwwa::ResponseProfile::Tile);
    const StereoBuffer rain = Render(snapshot, MakeSettings(rwwa::InputRole::RainBed), input);
    const StereoBuffer wind = Render(snapshot, MakeSettings(rwwa::InputRole::WindBed), input);
    const StereoBuffer mixed = Render(snapshot, MakeSettings(rwwa::InputRole::Generic), input);
    EXPECT(Difference(rain, wind) > 0.1);
    EXPECT(Difference(rain, mixed) > 0.05);
    EXPECT(Difference(wind, mixed) > 0.05);

    rwwa::InteractionSettings lowTransient = MakeSettings(rwwa::InputRole::RainBed);
    lowTransient.transientSensitivity = 0.0f;
    rwwa::InteractionSettings highTransient = lowTransient;
    highTransient.transientSensitivity = 1.0f;
    EXPECT(Difference(
        Render(snapshot, lowTransient, input),
        Render(snapshot, highTransient, input)) > 0.01);
}

void TestPanningAndExtraChannels()
{
    const StereoBuffer input = MakeInput();
    const rwwa::InteractionSettings settings = MakeSettings(rwwa::InputRole::Generic);
    const StereoBuffer pannedRight = Render(
        MakeSnapshot(rwwa::ResponseProfile::Metal, rwwa::kResponseMaskBoth, 1.0f),
        settings,
        input);
    EXPECT(WetEnergy(pannedRight, input, 1u) > WetEnergy(pannedRight, input, 0u) * 5.0);

    constexpr std::size_t frames = 4096u;
    StereoBuffer multi = MakeInput(frames);
    std::vector<float> center(frames, 0.03125f);
    const std::vector<float> originalCenter = center;
    float* channels[] = {multi.left.data(), multi.right.data(), center.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Wood),
        settings,
        channels,
        3u,
        frames);
    EXPECT(center == originalCenter);
}

void TestMonoProcessing()
{
    constexpr std::size_t frames = 4096u;
    StereoBuffer stereoInput = MakeInput(frames);
    std::vector<float> mono = stereoInput.left;
    const std::vector<float> dry = mono;
    float* channels[] = {mono.data()};
    rwwa::GeometryInteractionProcessor processor(kSampleRate);
    processor.Process(
        MakeSnapshot(rwwa::ResponseProfile::Tile),
        MakeSettings(rwwa::InputRole::RainBed),
        channels,
        1u,
        frames);

    double difference = 0.0;
    for (std::size_t frame = 0u; frame < frames; ++frame)
    {
        EXPECT(std::isfinite(mono[frame]));
        EXPECT(mono[frame] >= -1.0f && mono[frame] <= 1.0f);
        difference += std::abs(static_cast<double>(mono[frame] - dry[frame]));
    }
    EXPECT(difference > 0.01);
}

void TestBoundaryAndNonFiniteInputs()
{
    constexpr std::size_t frames = 1024u;
    std::array<float, frames> left{};
    std::array<float, frames> right{};
    for (std::size_t frame = 0u; frame < frames; ++frame)
    {
        left[frame] = frame % 3u == 0u
            ? std::numeric_limits<float>::quiet_NaN()
            : (frame % 3u == 1u ? std::numeric_limits<float>::infinity() : 8.0f);
        right[frame] = frame % 2u == 0u ? -8.0f : 8.0f;
    }

    rwwa::SceneSnapshot malformed = MakeSnapshot(rwwa::ResponseProfile::Metal);
    malformed.contributionCount = std::numeric_limits<std::uint32_t>::max();
    malformed.contributions[0].profileId = std::numeric_limits<std::uint32_t>::max();
    malformed.contributions[0].responseMask = std::numeric_limits<std::uint32_t>::max();
    malformed.contributions[0].gain = std::numeric_limits<float>::quiet_NaN();
    malformed.contributions[0].pan = std::numeric_limits<float>::infinity();

    rwwa::InteractionSettings settings{};
    settings.inputRole = static_cast<rwwa::InputRole>(999u);
    settings.wetMix = 8.0f;
    settings.responseGainLinear = std::numeric_limits<float>::infinity();
    settings.transientSensitivity = std::numeric_limits<float>::quiet_NaN();
    float* channels[] = {left.data(), right.data()};
    rwwa::GeometryInteractionProcessor processor(1u);
    EXPECT(processor.SampleRate() == 8000u);
    processor.Process(malformed, settings, channels, 2u, frames);
    for (std::size_t frame = 0u; frame < frames; ++frame)
    {
        EXPECT(std::isfinite(left[frame]));
        EXPECT(std::isfinite(right[frame]));
        EXPECT(left[frame] >= -1.0f && left[frame] <= 1.0f);
        EXPECT(right[frame] >= -1.0f && right[frame] <= 1.0f);
    }

    processor.Process(malformed, settings, nullptr, 2u, frames);
    float* nullChannels[] = {nullptr, nullptr};
    processor.Process(malformed, settings, nullChannels, 2u, frames);
}

void TestBlockContinuity()
{
    const StereoBuffer input = MakeInput();
    StereoBuffer whole = input;
    StereoBuffer blocked = input;
    const rwwa::SceneSnapshot snapshot = MakeSnapshot(rwwa::ResponseProfile::Glass);
    const rwwa::InteractionSettings settings = MakeSettings(rwwa::InputRole::Generic);

    rwwa::GeometryInteractionProcessor wholeProcessor(kSampleRate);
    float* wholeChannels[] = {whole.left.data(), whole.right.data()};
    wholeProcessor.Process(snapshot, settings, wholeChannels, 2u, kFrameCount);

    constexpr std::array<std::size_t, 8> blockPattern{{1u, 3u, 17u, 64u, 255u, 512u, 1024u, 37u}};
    rwwa::GeometryInteractionProcessor blockedProcessor(kSampleRate);
    std::size_t offset = 0u;
    std::size_t blockIndex = 0u;
    while (offset < kFrameCount)
    {
        const std::size_t blockSize = std::min(
            blockPattern[blockIndex % blockPattern.size()], kFrameCount - offset);
        float* blockedChannels[] = {
            blocked.left.data() + offset,
            blocked.right.data() + offset};
        blockedProcessor.Process(snapshot, settings, blockedChannels, 2u, blockSize);
        offset += blockSize;
        ++blockIndex;
    }

    EXPECT(whole.left == blocked.left);
    EXPECT(whole.right == blocked.right);
}
} // namespace

int main()
{
    TestDeterminism();
    TestWetZeroIsSampleTransparent();
    TestDisabledOrEmptyGeometryIsSampleTransparent();
    TestSilenceIsStable();
    TestProfilesAndRolesAreDistinct();
    TestPanningAndExtraChannels();
    TestMonoProcessing();
    TestBoundaryAndNonFiniteInputs();
    TestBlockContinuity();
    if (g_failureCount != 0)
    {
        std::cerr << g_failureCount << " expectation(s) failed\n";
        return 1;
    }
    return 0;
}
