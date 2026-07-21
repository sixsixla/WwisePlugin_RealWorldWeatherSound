#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

rwwa::SceneSnapshot MakeFixture()
{
    rwwa::SceneInput scene{};
    scene.featureCount = 4u;
    scene.features[0] = {1u, {-4.0f, 1.0f, 5.0f}, 2.2f, 0u, rwwa::kResponseMaskRain, 2};
    scene.features[1] = {2u, {4.0f, 0.5f, 5.0f}, 2.5f, 1u, rwwa::kResponseMaskRain, 2};
    scene.features[2] = {3u, {-2.0f, 2.0f, -3.0f}, 1.8f, 2u, rwwa::kResponseMaskRain, 1};
    scene.features[3] = {4u, {3.0f, 1.0f, -2.0f}, 2.0f, 3u, rwwa::kResponseMaskRain, 1};

    rwwa::ListenerState listener{};
    listener.yawRadians = 0.2f;
    rwwa::WeatherState weather{};
    weather.rainIntensity = 0.82f;
    weather.seed = 0x4a17c9e3u;
    weather.geometryEnabled = true;
    weather.masterGainLinear = 1.0f;
    return rwwa::CompileScene(scene, listener, weather);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: rwwa_offline_renderer <output.wav>\n";
        return 2;
    }

    constexpr std::uint32_t sampleRate = 48000u;
    constexpr std::size_t frameCount = sampleRate * 3u;
    std::vector<float> left(frameCount);
    std::vector<float> right(frameCount);
    rwwa::RainSynth synth(sampleRate);
    synth.Process(MakeFixture(), left.data(), right.data(), frameCount);

    const std::filesystem::path outputPath(argv[1]);
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

    std::cout << "Wrote deterministic stereo fixture: " << outputPath.string() << '\n';
    return 0;
}
