#include "rwwa/WeatherAcousticsCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct WaveData
{
    std::uint32_t sampleRate = 0u;
    std::vector<std::vector<float>> channels{};
};

std::uint16_t ReadLittleEndian16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadLittleEndian32(const unsigned char* bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8u) |
        (static_cast<std::uint32_t>(bytes[2]) << 16u) |
        (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

void WriteLittleEndian16(std::ofstream& stream, std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu)};
    stream.write(bytes, sizeof(bytes));
}

bool ReadWave(const std::filesystem::path& inputPath, WaveData& output, std::string& error)
{
    std::ifstream stream(inputPath, std::ios::binary);
    if (!stream)
    {
        error = "could not open input WAV";
        return false;
    }

    unsigned char riffHeader[12]{};
    stream.read(reinterpret_cast<char*>(riffHeader), sizeof(riffHeader));
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(riffHeader)) ||
        std::memcmp(riffHeader, "RIFF", 4) != 0 ||
        std::memcmp(riffHeader + 8, "WAVE", 4) != 0)
    {
        error = "input is not a RIFF/WAVE file";
        return false;
    }

    std::vector<unsigned char> formatChunk;
    std::vector<unsigned char> dataChunk;
    while (stream && (formatChunk.empty() || dataChunk.empty()))
    {
        unsigned char chunkHeader[8]{};
        stream.read(reinterpret_cast<char*>(chunkHeader), sizeof(chunkHeader));
        if (stream.gcount() != static_cast<std::streamsize>(sizeof(chunkHeader)))
        {
            break;
        }
        const std::uint32_t chunkSize = ReadLittleEndian32(chunkHeader + 4);
        if (chunkSize > 1024u * 1024u * 1024u)
        {
            error = "input WAV contains an unreasonable chunk size";
            return false;
        }

        std::vector<unsigned char>* destination = nullptr;
        if (std::memcmp(chunkHeader, "fmt ", 4) == 0)
            destination = &formatChunk;
        else if (std::memcmp(chunkHeader, "data", 4) == 0)
            destination = &dataChunk;

        if (destination != nullptr)
        {
            destination->resize(chunkSize);
            if (chunkSize > 0u)
            {
                stream.read(reinterpret_cast<char*>(destination->data()), chunkSize);
                if (stream.gcount() != static_cast<std::streamsize>(chunkSize))
                {
                    error = "input WAV chunk is truncated";
                    return false;
                }
            }
        }
        else
        {
            stream.seekg(chunkSize, std::ios::cur);
        }
        if ((chunkSize & 1u) != 0u)
            stream.seekg(1, std::ios::cur);
    }

    if (formatChunk.size() < 16u || dataChunk.empty())
    {
        error = "input WAV is missing fmt or data";
        return false;
    }

    const std::uint16_t formatTag = ReadLittleEndian16(formatChunk.data());
    const std::uint16_t channelCount = ReadLittleEndian16(formatChunk.data() + 2u);
    const std::uint32_t sampleRate = ReadLittleEndian32(formatChunk.data() + 4u);
    const std::uint16_t blockAlign = ReadLittleEndian16(formatChunk.data() + 12u);
    const std::uint16_t bitsPerSample = ReadLittleEndian16(formatChunk.data() + 14u);
    if (channelCount == 0u || channelCount > 16u || sampleRate < 8000u || sampleRate > 192000u)
    {
        error = "input WAV channel count or sample rate is unsupported";
        return false;
    }

    const bool integerPcm = formatTag == 1u &&
        (bitsPerSample == 16u || bitsPerSample == 24u || bitsPerSample == 32u);
    const bool floatPcm = formatTag == 3u && bitsPerSample == 32u;
    const std::uint16_t bytesPerSample = static_cast<std::uint16_t>(bitsPerSample / 8u);
    if ((!integerPcm && !floatPcm) ||
        blockAlign != static_cast<std::uint16_t>(channelCount * bytesPerSample) ||
        dataChunk.size() % blockAlign != 0u)
    {
        error = "input WAV must be PCM16/24/32 or Float32 with an interleaved standard layout";
        return false;
    }

    const std::size_t frameCount = dataChunk.size() / blockAlign;
    output = {};
    output.sampleRate = sampleRate;
    output.channels.assign(channelCount, std::vector<float>(frameCount));
    const unsigned char* source = dataChunk.data();
    for (std::size_t frame = 0u; frame < frameCount; ++frame)
    {
        for (std::size_t channel = 0u; channel < channelCount; ++channel)
        {
            float sample = 0.0f;
            if (floatPcm)
            {
                std::memcpy(&sample, source, sizeof(float));
                if (!std::isfinite(sample))
                    sample = 0.0f;
            }
            else if (bitsPerSample == 16u)
            {
                const auto value = static_cast<std::int16_t>(ReadLittleEndian16(source));
                sample = static_cast<float>(value) / 32768.0f;
            }
            else if (bitsPerSample == 24u)
            {
                std::int32_t value = static_cast<std::int32_t>(source[0]) |
                    (static_cast<std::int32_t>(source[1]) << 8u) |
                    (static_cast<std::int32_t>(source[2]) << 16u);
                if ((value & 0x00800000) != 0)
                    value |= static_cast<std::int32_t>(0xff000000u);
                sample = static_cast<float>(value) / 8388608.0f;
            }
            else
            {
                const auto value = static_cast<std::int32_t>(ReadLittleEndian32(source));
                sample = static_cast<float>(static_cast<double>(value) / 2147483648.0);
            }
            output.channels[channel][frame] = std::clamp(sample, -4.0f, 4.0f);
            source += bytesPerSample;
        }
    }
    return true;
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
        << "  rwwa_offline_renderer --preset open-wind|rain-metal|weather-ring --output <output.wav>\n"
        << "  rwwa_offline_renderer --input <bed.wav> --role rain|wind|generic "
           "--preset rain-metal|weather-ring --wet <0..1> --output <output.wav>\n";
}

bool ParseRole(std::string_view name, rwwa::InputRole& role)
{
    if (name == "rain")
        role = rwwa::InputRole::RainBed;
    else if (name == "wind")
        role = rwwa::InputRole::WindBed;
    else if (name == "generic")
        role = rwwa::InputRole::Generic;
    else
        return false;
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    std::string preset = "weather-ring";
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    rwwa::InputRole inputRole = rwwa::InputRole::Generic;
    float wetMix = 0.65f;
    float responseGainDb = 0.0f;
    float transientSensitivity = 0.5f;
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
            else if (argument == "--input" && index + 1 < argc)
            {
                inputPath = argv[++index];
            }
            else if (argument == "--role" && index + 1 < argc)
            {
                if (!ParseRole(argv[++index], inputRole))
                {
                    std::cerr << "Unknown input role: " << argv[index] << '\n';
                    return 2;
                }
            }
            else if (argument == "--wet" && index + 1 < argc)
            {
                wetMix = std::stof(argv[++index]);
            }
            else if (argument == "--response-gain-db" && index + 1 < argc)
            {
                responseGainDb = std::stof(argv[++index]);
            }
            else if (argument == "--transient" && index + 1 < argc)
            {
                transientSensitivity = std::stof(argv[++index]);
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

    std::uint32_t sampleRate = 48000u;
    std::vector<float> left;
    std::vector<float> right;
    if (inputPath.empty())
    {
        const std::size_t frameCount = static_cast<std::size_t>(sampleRate) * 4u;
        left.resize(frameCount);
        right.resize(frameCount);
        rwwa::WeatherSynth synth(sampleRate);
        synth.Process(snapshot, left.data(), right.data(), frameCount);
    }
    else
    {
        WaveData wave{};
        std::string readError;
        if (!ReadWave(inputPath, wave, readError))
        {
            std::cerr << "Failed to read input WAV '" << inputPath.string() << "': "
                      << readError << '\n';
            return 1;
        }
        sampleRate = wave.sampleRate;
        const std::size_t frameCount = wave.channels.front().size();
        rwwa::GeometryInteractionProcessor processor(sampleRate);
        rwwa::InteractionSettings settings{};
        settings.inputRole = inputRole;
        settings.wetMix = std::clamp(wetMix, 0.0f, 1.0f);
        settings.responseGainLinear = std::pow(10.0f, std::clamp(responseGainDb, -24.0f, 12.0f) / 20.0f);
        settings.transientSensitivity = std::clamp(transientSensitivity, 0.0f, 1.0f);

        constexpr std::size_t blockFrames = 256u;
        std::vector<float*> channelPointers(wave.channels.size());
        for (std::size_t offset = 0u; offset < frameCount; offset += blockFrames)
        {
            const std::size_t count = std::min(blockFrames, frameCount - offset);
            for (std::size_t channel = 0u; channel < wave.channels.size(); ++channel)
                channelPointers[channel] = wave.channels[channel].data() + offset;
            processor.Process(snapshot, settings, channelPointers.data(), channelPointers.size(), count);
        }

        left = wave.channels[0];
        right = wave.channels.size() > 1u ? wave.channels[1] : wave.channels[0];
    }

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

    if (inputPath.empty())
    {
        std::cout << "Wrote deterministic " << preset << " stereo fixture: "
                  << outputPath.string() << '\n';
    }
    else
    {
        std::cout << "Processed input WAV through the " << preset
                  << " geometry interaction preset: " << outputPath.string() << '\n';
    }
    return 0;
}
