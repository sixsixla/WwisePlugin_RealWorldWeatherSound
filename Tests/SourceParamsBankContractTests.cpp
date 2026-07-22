#include "RealWorldWeatherAcousticsSourceParams.h"

#include <AK/Tools/Common/AkAssert.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// The production SoundEngine provides this symbol. The standalone Debug test
// links no SoundEngine binary, so supply Wwise's documented no-op test hook.
DEFINEDUMMYASSERTHOOK

static_assert(PARAM_WIND_SPEED_ID == 66, "Wind speed parameter ID is part of the bank ABI.");
static_assert(PARAM_WIND_DIRECTION_DEGREES_ID == 67, "Wind direction parameter ID is part of the bank ABI.");
static_assert(PARAM_WIND_GUSTINESS_ID == 68, "Wind gustiness parameter ID is part of the bank ABI.");
static_assert(NUM_PARAMS == 69, "The Wwise parameter count is part of the bank ABI.");
static_assert(sizeof(AkReal32) == 4, "The bank contract requires 32-bit AkReal32 values.");
static_assert(sizeof(AkInt32) == 4, "The bank contract requires 32-bit AkInt32 values.");
static_assert(sizeof(bool) == 1, "The legacy bank contract stores geometry enabled as one byte.");

namespace
{
constexpr std::size_t kLegacyBankSize = 261;
constexpr std::size_t kCurrentBankSize = 273;
constexpr std::size_t kGeometryEnabledOffset = 16;

int g_failureCount = 0;

void Fail(const std::string& in_message)
{
    ++g_failureCount;
    std::cerr << "FAIL: " << in_message << '\n';
}

void Expect(bool in_condition, const std::string& in_message)
{
    if (!in_condition)
    {
        Fail(in_message);
    }
}

void ExpectNear(AkReal32 in_actual, AkReal32 in_expected, const std::string& in_name)
{
    if (std::fabs(in_actual - in_expected) > 0.00001f)
    {
        Fail(in_name + " expected " + std::to_string(in_expected) +
             ", got " + std::to_string(in_actual));
    }
}

template <typename T>
void Append(std::vector<AkUInt8>& io_block, const T& in_value)
{
    const std::size_t offset = io_block.size();
    io_block.resize(offset + sizeof(T));
    std::memcpy(io_block.data() + offset, &in_value, sizeof(T));
}

std::vector<AkUInt8> MakeLegacyBank()
{
    std::vector<AkUInt8> block;
    block.reserve(kCurrentBankSize);

    Append(block, AkReal32{37.5f});
    Append(block, AkReal32{-9.0f});
    Append(block, AkReal32{0.73f});
    Append(block, AkInt32{2468});
    Append(block, true);
    Append(block, AkReal32{1.25f});
    Append(block, AkReal32{-2.5f});
    Append(block, AkReal32{3.75f});
    Append(block, AkReal32{42.0f});
    Append(block, AkInt32{3});

    for (AkUInt32 slot = 0; slot < FEATURE_SLOT_COUNT; ++slot)
    {
        const AkReal32 slotValue = static_cast<AkReal32>(slot);
        Append(block, AkReal32{10.0f + slotValue});
        Append(block, AkReal32{-20.0f - slotValue});
        Append(block, AkReal32{30.0f + slotValue});
        Append(block, AkReal32{1.5f + slotValue});
        Append(block, static_cast<AkInt32>(slot % 4u));
        Append(block, static_cast<AkInt32>(slot % 4u));
        Append(block, static_cast<AkInt32>(100u + slot));
    }

    Expect(block.size() == kLegacyBankSize, "legacy fixture must be exactly 261 bytes");
    return block;
}

std::vector<AkUInt8> MakeCurrentBank()
{
    std::vector<AkUInt8> block = MakeLegacyBank();
    Append(block, AkReal32{17.5f});
    Append(block, AkReal32{225.0f});
    Append(block, AkReal32{0.8f});
    Expect(block.size() == kCurrentBankSize, "current fixture must be exactly 273 bytes");
    return block;
}

void TestRejectedSizes()
{
    for (const std::size_t size : {260u, 262u, 272u, 274u})
    {
        std::vector<AkUInt8> block(size, 0);
        RealWorldWeatherAcousticsSourceParams params;
        const AKRESULT result = params.SetParamsBlock(block.data(), static_cast<AkUInt32>(block.size()));
        Expect(result == AK_InvalidParameter, "bank size " + std::to_string(size) + " must be rejected");
    }
}

void TestLegacyBank()
{
    const std::vector<AkUInt8> block = MakeLegacyBank();
    RealWorldWeatherAcousticsSourceParams params;
    const AKRESULT result = params.SetParamsBlock(block.data(), static_cast<AkUInt32>(block.size()));

    Expect(result == AK_Success, "261-byte legacy bank must load successfully");
    ExpectNear(params.Values.fRainIntensity, 0.73f, "legacy rain intensity");
    Expect(params.Values.iSeed == 2468, "legacy seed must be preserved");
    Expect(params.Values.bGeometryEnabled, "legacy geometry-enabled byte must be preserved");
    Expect(params.Values.iFeatureCount == 3, "legacy feature count must be preserved");
    ExpectNear(params.Values.Features[0].fX, 10.0f, "legacy feature 0 X");
    ExpectNear(params.Values.Features[3].fRadius, 4.5f, "legacy feature 3 radius");
    Expect(params.Values.Features[3].iProfile == 3, "legacy feature 3 profile must be preserved");
    Expect(params.Values.Features[6].iMask == 2, "legacy feature 6 response mask must be preserved");
    Expect(params.Values.Features[7].iPriority == 107, "legacy feature 7 priority must be preserved");
    ExpectNear(params.Values.fWindSpeed, 0.0f, "legacy wind speed");
    ExpectNear(params.Values.fWindDirectionDegrees, 0.0f, "legacy wind direction");
    ExpectNear(params.Values.fWindGustiness, 0.0f, "legacy wind gustiness");
}

void TestCurrentBank()
{
    const std::vector<AkUInt8> block = MakeCurrentBank();
    RealWorldWeatherAcousticsSourceParams params;
    const AKRESULT result = params.SetParamsBlock(block.data(), static_cast<AkUInt32>(block.size()));

    Expect(result == AK_Success, "273-byte current bank must load successfully");
    ExpectNear(params.Values.fRainIntensity, 0.73f, "current rain intensity");
    Expect(params.Values.bGeometryEnabled, "current geometry-enabled byte must be preserved");
    ExpectNear(params.Values.Features[1].fZ, 31.0f, "current feature 1 Z");
    ExpectNear(params.Values.fWindSpeed, 17.5f, "current wind speed");
    ExpectNear(params.Values.fWindDirectionDegrees, 225.0f, "current wind direction");
    ExpectNear(params.Values.fWindGustiness, 0.8f, "current wind gustiness");
}

void TestGeometryDisabledBank()
{
    std::vector<AkUInt8> block = MakeCurrentBank();
    block[kGeometryEnabledOffset] = 0;

    RealWorldWeatherAcousticsSourceParams params;
    const AKRESULT result = params.SetParamsBlock(block.data(), static_cast<AkUInt32>(block.size()));

    Expect(result == AK_Success, "273-byte geometry-disabled bank must load successfully");
    Expect(!params.Values.bGeometryEnabled, "geometry-disabled bank byte must remain false");
}
}

int main()
{
    TestRejectedSizes();
    TestLegacyBank();
    TestCurrentBank();
    TestGeometryDisabledBank();

    if (g_failureCount != 0)
    {
        std::cerr << g_failureCount << " bank contract assertion(s) failed.\n";
        return 1;
    }

    std::cout << "SourceParams bank ABI contract passed: 261-byte legacy and 273-byte current blocks.\n";
    return 0;
}
