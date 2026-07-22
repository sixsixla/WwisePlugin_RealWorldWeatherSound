#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
void WriteLittleEndian16(std::ofstream& stream, std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)};
    stream.write(bytes, sizeof(bytes));
}

void WriteLittleEndian32(std::ofstream& stream, std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)};
    stream.write(bytes, sizeof(bytes));
}

bool WriteStereoWave(
    const std::filesystem::path& outputPath,
    const std::vector<float>& left,
    const std::vector<float>& right,
    std::uint32_t sampleRate)
{
    if (left.size() != right.size())
    {
        return false;
    }

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(left.size() * 4u);
    std::ofstream stream(outputPath, std::ios::binary);
    if (!stream)
    {
        return false;
    }

    stream.write("RIFF", 4);
    WriteLittleEndian32(stream, 36u + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    WriteLittleEndian32(stream, 16u);
    WriteLittleEndian16(stream, 1u);
    WriteLittleEndian16(stream, 2u);
    WriteLittleEndian32(stream, sampleRate);
    WriteLittleEndian32(stream, sampleRate * 4u);
    WriteLittleEndian16(stream, 4u);
    WriteLittleEndian16(stream, 16u);
    stream.write("data", 4);
    WriteLittleEndian32(stream, dataBytes);

    for (std::size_t frame = 0u; frame < left.size(); ++frame)
    {
        const float safeLeft = std::clamp(left[frame], -1.0f, 1.0f);
        const float safeRight = std::clamp(right[frame], -1.0f, 1.0f);
        const auto leftPcm = static_cast<std::int16_t>(std::lrint(safeLeft * 32767.0f));
        const auto rightPcm = static_cast<std::int16_t>(std::lrint(safeRight * 32767.0f));
        WriteLittleEndian16(stream, static_cast<std::uint16_t>(leftPcm));
        WriteLittleEndian16(stream, static_cast<std::uint16_t>(rightPcm));
    }
    return stream.good();
}

rwwa::SceneSnapshot MakeOpenWindPreset()
{
    rwwa::WeatherState weather{};
    weather.windSpeedMetersPerSecond = 13.0f;
    weather.windDirectionRadians = 0.65f;
    weather.windGustiness = 0.72f;
    weather.seed = 0xa632f17bu;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene({}, {}, weather);
}

rwwa::SceneSnapshot MakeRainMetalPreset()
{
    rwwa::SceneInput scene{};
    scene.featureCount = 1u;
    scene.features[0] = {
        1u, {2.5f, 1.0f, 4.0f}, 3.0f, 0u, rwwa::kResponseMaskRain, 3};
    rwwa::WeatherState weather{};
    weather.rainIntensity = 0.86f;
    weather.seed = 0x71e408d3u;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene(scene, {}, weather);
}

rwwa::SceneSnapshot MakeWeatherRingPreset()
{
    rwwa::SceneInput scene{};
    scene.featureCount = 4u;
    scene.features[0] = {1u, {-4.0f, 1.0f, 5.0f}, 2.2f, 0u, rwwa::kResponseMaskBoth, 2};
    scene.features[1] = {2u, {4.0f, 0.5f, 5.0f}, 2.5f, 1u, rwwa::kResponseMaskBoth, 2};
    scene.features[2] = {3u, {-2.0f, 2.0f, -3.0f}, 1.8f, 2u, rwwa::kResponseMaskBoth, 1};
    scene.features[3] = {4u, {3.0f, 1.0f, -2.0f}, 2.0f, 3u, rwwa::kResponseMaskBoth, 1};

    rwwa::ListenerState listener{};
    listener.yawRadians = 0.2f;
    rwwa::WeatherState weather{};
    weather.rainIntensity = 0.76f;
    weather.windSpeedMetersPerSecond = 10.5f;
    weather.windDirectionRadians = 0.8f;
    weather.windGustiness = 0.68f;
    weather.seed = 0x4a17c9e3u;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene(scene, listener, weather);
}

bool MakePreset(std::string_view name, rwwa::SceneSnapshot& snapshot)
{
    if (name == "open-wind")
    {
        snapshot = MakeOpenWindPreset();
        return true;
    }
    if (name == "rain-metal")
    {
        snapshot = MakeRainMetalPreset();
        return true;
    }
    if (name == "weather-ring")
    {
        snapshot = MakeWeatherRingPreset();
        return true;
    }
    return false;
}

void PrintUsage()
{
    std::cerr
        << "Usage:\n"
        << "  rwwa_offline_renderer <output.wav>\n"
        << "  rwwa_offline_renderer --preset open-wind|rain-metal|weather-ring --output <output.wav>\n";
}
} // namespace

int main(int argc, char** argv)
{
    std::string preset = "weather-ring";
    std::filesystem::path outputPath;
    if (argc == 2)
    {
        outputPath = argv[1];
    }
    else
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--preset" && index + 1 < argc)
            {
                preset = argv[++index];
            }
            else if (argument == "--output" && index + 1 < argc)
            {
                outputPath = argv[++index];
            }
            else
            {
                PrintUsage();
                return 2;
            }
        }
    }

    rwwa::SceneSnapshot snapshot{};
    if (outputPath.empty() || !MakePreset(preset, snapshot))
    {
        if (!MakePreset(preset, snapshot))
        {
            std::cerr << "Unknown preset: " << preset << '\n';
        }
        PrintUsage();
        return 2;
    }

    constexpr std::uint32_t sampleRate = 48000u;
    constexpr std::size_t frameCount = sampleRate * 4u;
    std::vector<float> left(frameCount);
    std::vector<float> right(frameCount);
    rwwa::WeatherSynth synth(sampleRate);
    synth.Process(snapshot, left.data(), right.data(), frameCount);

    std::error_code error;
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error)
        {
            std::cerr << "Failed to create output directory: " << error.message() << '\n';
            return 1;
        }
    }

    if (!WriteStereoWave(outputPath, left, right, sampleRate))
    {
        std::cerr << "Failed to write WAV: " << outputPath.string() << '\n';
        return 1;
    }

    std::cout << "Wrote deterministic " << preset << " stereo fixture: "
              << outputPath.string() << '\n';
    return 0;
}
