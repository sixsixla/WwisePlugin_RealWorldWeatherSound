#include "RealWorldWeatherAcousticsEffectParams.h"

#include <AK/Tools/Common/AkAssert.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

// The production SoundEngine provides this symbol. The standalone bank-contract
// test links no SoundEngine binary, so supply Wwise's documented no-op test hook.
DEFINEDUMMYASSERTHOOK

static_assert(EFFECT_PARAM_INPUT_ROLE_ID == 0, "Effect InputRole ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_WET_MIX_ID == 1, "Effect WetMix ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_RESPONSE_GAIN_DB_ID == 2, "Effect ResponseGainDb ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_TRANSIENT_SENSITIVITY_ID == 3, "Effect TransientSensitivity ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_RAIN_INTENSITY_ID == 4, "Effect RainIntensity ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_WIND_SPEED_ID == 5, "Effect WindSpeed ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_WIND_DIRECTION_DEGREES_ID == 6, "Effect WindDirectionDegrees ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_WIND_GUSTINESS_ID == 7, "Effect WindGustiness ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_SEED_ID == 8, "Effect Seed ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_GEOMETRY_ENABLED_ID == 9, "Effect GeometryEnabled ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_LISTENER_X_ID == 10, "Effect ListenerX ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_FEATURE_COUNT_ID == 14, "Effect FeatureCount ID is part of the bank ABI.");
static_assert(EFFECT_PARAM_FEATURES_BEGIN_ID == 15, "Effect Feature slot start ID is part of the bank ABI.");
static_assert(EFFECT_FEATURE_SLOT_COUNT == 8, "Effect bank ABI stores exactly eight feature slots.");
static_assert(EFFECT_NUM_PARAMS == 71, "Effect parameter count is part of the bank ABI.");
static_assert(sizeof(AkReal32) == 4, "The bank contract requires 32-bit AkReal32 values.");
static_assert(sizeof(AkInt32) == 4, "The bank contract requires 32-bit AkInt32 values.");
static_assert(sizeof(bool) == 1, "The effect bank contract stores GeometryEnabled as one byte.");

namespace
{
constexpr std::size_t kEffectFloatCount = 43;
constexpr std::size_t kEffectIntCount = 27;
constexpr std::size_t kEffectBoolCount = 1;
constexpr std::size_t kEffectBankSize =
    kEffectFloatCount * sizeof(AkReal32) +
    kEffectIntCount * sizeof(AkInt32) +
    kEffectBoolCount * sizeof(bool);

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

std::vector<AkUInt8> MakeCompleteBank()
{
    std::vector<AkUInt8> block;
    block.reserve(kEffectBankSize);

    Append(block, AkInt32{1});
    Append(block, AkReal32{0.75f});
    Append(block, AkReal32{-6.5f});
    Append(block, AkReal32{0.35f});
    Append(block, AkReal32{0.8f});
    Append(block, AkReal32{18.5f});
    Append(block, AkReal32{270.0f});
    Append(block, AkReal32{0.65f});
    Append(block, AkInt32{24681357});
    Append(block, false);
    Append(block, AkReal32{1.25f});
    Append(block, AkReal32{-2.5f});
    Append(block, AkReal32{3.75f});
    Append(block, AkReal32{42.0f});
    Append(block, AkInt32{5});

    for (AkUInt32 slot = 0; slot < EFFECT_FEATURE_SLOT_COUNT; ++slot)
    {
        const AkReal32 slotValue = static_cast<AkReal32>(slot);
        Append(block, AkReal32{10.0f + slotValue});
        Append(block, AkReal32{-20.0f - slotValue});
        Append(block, AkReal32{30.0f + slotValue});
        Append(block, AkReal32{1.5f + slotValue});
        Append(block, static_cast<AkInt32>(slot % 5u));
        Append(block, static_cast<AkInt32>(slot % 4u));
        Append(block, static_cast<AkInt32>(100u + slot));
    }

    Expect(block.size() == kEffectBankSize, "complete effect fixture must be exactly 281 bytes");
    return block;
}

void TestDefaultsFromEmptyInit()
{
    RealWorldWeatherAcousticsEffectParams params;
    const AKRESULT result = params.Init(nullptr, nullptr, 0);

    Expect(result == AK_Success, "empty Init must load defaults");
    Expect(params.Values.iInputRole == 0, "default InputRole must be Rain");
    ExpectNear(params.Values.fWetMix, 1.0f, "default WetMix");
    ExpectNear(params.Values.fResponseGainDb, 10.0f, "default ResponseGainDb");
    ExpectNear(params.Values.fTransientSensitivity, 0.85f, "default TransientSensitivity");
    ExpectNear(params.Values.fRainIntensity, 0.9f, "default RainIntensity");
    ExpectNear(params.Values.fWindSpeed, 0.0f, "default WindSpeed");
    ExpectNear(params.Values.fWindDirectionDegrees, 0.0f, "default WindDirectionDegrees");
    ExpectNear(params.Values.fWindGustiness, 0.0f, "default WindGustiness");
    Expect(params.Values.iSeed == 1337, "default Seed");
    Expect(params.Values.bGeometryEnabled, "default GeometryEnabled");
    Expect(params.Values.iFeatureCount == 4, "default FeatureCount");
    ExpectNear(params.Values.Features[0].fZ, 5.5f, "default feature 1 Z");
    ExpectNear(params.Values.Features[1].fX, 5.5f, "default feature 2 X");
    ExpectNear(params.Values.Features[2].fZ, -5.5f, "default feature 3 Z");
    ExpectNear(params.Values.Features[3].fX, -5.5f, "default feature 4 X");
    const AkInt32 expectedProfiles[] = {3, 4, 0, 1};
    for (AkUInt32 slot = 0; slot < 4u; ++slot)
    {
        ExpectNear(params.Values.Features[slot].fRadius, 3.2f, "default audition feature radius");
        Expect(params.Values.Features[slot].iProfile == expectedProfiles[slot],
            "default audition feature profile");
        Expect(params.Values.Features[slot].iMask == 1, "default audition feature rain mask");
    }
    ExpectNear(params.Values.Features[7].fRadius, 2.0f, "default feature 8 radius");
    Expect(params.Values.Features[7].iProfile == 3, "default feature 8 profile");
    Expect(params.Values.Features[7].iMask == 3, "default feature 8 mask");
    Expect(params.Values.Features[7].iPriority == 1, "default feature 8 priority");
}

void TestCompleteBankMapping()
{
    const std::vector<AkUInt8> block = MakeCompleteBank();
    RealWorldWeatherAcousticsEffectParams params;
    const AKRESULT result = params.SetParamsBlock(block.data(), static_cast<AkUInt32>(block.size()));

    Expect(result == AK_Success, "281-byte effect bank must load successfully");
    Expect(params.Values.iInputRole == 1, "bank InputRole");
    ExpectNear(params.Values.fWetMix, 0.75f, "bank WetMix");
    ExpectNear(params.Values.fResponseGainDb, -6.5f, "bank ResponseGainDb");
    ExpectNear(params.Values.fTransientSensitivity, 0.35f, "bank TransientSensitivity");
    ExpectNear(params.Values.fRainIntensity, 0.8f, "bank RainIntensity");
    ExpectNear(params.Values.fWindSpeed, 18.5f, "bank WindSpeed");
    ExpectNear(params.Values.fWindDirectionDegrees, 270.0f, "bank WindDirectionDegrees");
    ExpectNear(params.Values.fWindGustiness, 0.65f, "bank WindGustiness");
    Expect(params.Values.iSeed == 24681357, "bank Seed");
    Expect(!params.Values.bGeometryEnabled, "bank GeometryEnabled");
    ExpectNear(params.Values.fListenerX, 1.25f, "bank ListenerX");
    ExpectNear(params.Values.fListenerY, -2.5f, "bank ListenerY");
    ExpectNear(params.Values.fListenerZ, 3.75f, "bank ListenerZ");
    ExpectNear(params.Values.fListenerYawDegrees, 42.0f, "bank ListenerYawDegrees");
    Expect(params.Values.iFeatureCount == 5, "bank FeatureCount");

    for (AkUInt32 slot = 0; slot < EFFECT_FEATURE_SLOT_COUNT; ++slot)
    {
        const RealWorldWeatherAcousticsEffectFeatureParams& feature = params.Values.Features[slot];
        const AkReal32 slotValue = static_cast<AkReal32>(slot);
        const std::string prefix = "bank feature " + std::to_string(slot + 1u) + ' ';
        ExpectNear(feature.fX, 10.0f + slotValue, prefix + "X");
        ExpectNear(feature.fY, -20.0f - slotValue, prefix + "Y");
        ExpectNear(feature.fZ, 30.0f + slotValue, prefix + "Z");
        ExpectNear(feature.fRadius, 1.5f + slotValue, prefix + "Radius");
        Expect(feature.iProfile == static_cast<AkInt32>(slot % 5u), prefix + "Profile");
        Expect(feature.iMask == static_cast<AkInt32>(slot % 4u), prefix + "Mask");
        Expect(feature.iPriority == static_cast<AkInt32>(100u + slot), prefix + "Priority");
    }
}

void TestRejectedBlocks()
{
    std::vector<AkUInt8> block = MakeCompleteBank();
    RealWorldWeatherAcousticsEffectParams params;

    Expect(
        params.SetParamsBlock(nullptr, static_cast<AkUInt32>(block.size())) == AK_InvalidParameter,
        "null effect bank block must be rejected");
    for (const std::size_t size : {0u, 280u, 282u})
    {
        std::vector<AkUInt8> rejected(size, 0);
        const void* data = rejected.empty() ? block.data() : rejected.data();
        const AKRESULT result = params.SetParamsBlock(data, static_cast<AkUInt32>(size));
        Expect(result == AK_InvalidParameter, "effect bank size " + std::to_string(size) + " must be rejected");
    }
}

void TestSetParamSizeChecks()
{
    RealWorldWeatherAcousticsEffectParams params;
    AkReal32 realValue = 0.5f;
    AkInt32 intValue = 1;
    bool boolValue = true;

    Expect(
        params.SetParam(EFFECT_PARAM_WET_MIX_ID, nullptr, sizeof(realValue)) == AK_InvalidParameter,
        "SetParam must reject null values");
    Expect(
        params.SetParam(EFFECT_NUM_PARAMS, &realValue, sizeof(realValue)) == AK_InvalidParameter,
        "SetParam must reject out-of-range parameter IDs");
    Expect(
        params.SetParam(EFFECT_PARAM_WET_MIX_ID, &realValue, sizeof(realValue) - 1u) == AK_InvalidParameter,
        "SetParam must reject wrong Real32 size");
    Expect(
        params.SetParam(EFFECT_PARAM_INPUT_ROLE_ID, &intValue, sizeof(intValue) - 1u) == AK_InvalidParameter,
        "SetParam must reject wrong Int32 size");
    Expect(
        params.SetParam(EFFECT_PARAM_GEOMETRY_ENABLED_ID, &boolValue, sizeof(boolValue)) == AK_Success,
        "GeometryEnabled SetParam must accept bool size");
    Expect(
        params.SetParam(EFFECT_PARAM_GEOMETRY_ENABLED_ID, &realValue, sizeof(realValue)) == AK_Success,
        "GeometryEnabled SetParam must accept Wwise Real32 automation size");
    Expect(params.Values.bGeometryEnabled, "non-zero Real32 GeometryEnabled must map to true");
    realValue = 0.0f;
    Expect(
        params.SetParam(EFFECT_PARAM_GEOMETRY_ENABLED_ID, &realValue, sizeof(realValue)) == AK_Success,
        "zero Real32 GeometryEnabled must be accepted");
    Expect(!params.Values.bGeometryEnabled, "zero Real32 GeometryEnabled must map to false");

    Expect(
        params.SetParam(EffectFeatureParameterId(0, EFFECT_PARAM_FEATURE_RADIUS_OFFSET), &realValue, sizeof(realValue) - 1u) ==
            AK_InvalidParameter,
        "SetParam must reject wrong feature Real32 size");
    Expect(
        params.SetParam(EffectFeatureParameterId(0, EFFECT_PARAM_FEATURE_PROFILE_OFFSET), &intValue, sizeof(intValue) - 1u) ==
            AK_InvalidParameter,
        "SetParam must reject wrong feature Int32 size");
}

void TestSetParamClamping()
{
    RealWorldWeatherAcousticsEffectParams params;
    AkInt32 intValue = -8;
    AkReal32 realValue = -1.0f;

    Expect(params.SetParam(EFFECT_PARAM_INPUT_ROLE_ID, &intValue, sizeof(intValue)) == AK_Success, "SetParam InputRole low");
    Expect(params.Values.iInputRole == 0, "InputRole must clamp low to Rain");
    intValue = 9;
    Expect(params.SetParam(EFFECT_PARAM_INPUT_ROLE_ID, &intValue, sizeof(intValue)) == AK_Success, "SetParam InputRole high");
    Expect(params.Values.iInputRole == 2, "InputRole must clamp high to Generic");

    Expect(params.SetParam(EFFECT_PARAM_WET_MIX_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WetMix low");
    ExpectNear(params.Values.fWetMix, 0.0f, "WetMix low clamp");
    realValue = 2.0f;
    Expect(params.SetParam(EFFECT_PARAM_WET_MIX_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WetMix high");
    ExpectNear(params.Values.fWetMix, 1.0f, "WetMix high clamp");

    realValue = -100.0f;
    Expect(params.SetParam(EFFECT_PARAM_RESPONSE_GAIN_DB_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam ResponseGainDb low");
    ExpectNear(params.Values.fResponseGainDb, -24.0f, "ResponseGainDb low clamp");
    realValue = 100.0f;
    Expect(params.SetParam(EFFECT_PARAM_RESPONSE_GAIN_DB_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam ResponseGainDb high");
    ExpectNear(params.Values.fResponseGainDb, 12.0f, "ResponseGainDb high clamp");

    realValue = std::numeric_limits<AkReal32>::quiet_NaN();
    Expect(params.SetParam(EFFECT_PARAM_TRANSIENT_SENSITIVITY_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam TransientSensitivity NaN");
    ExpectNear(params.Values.fTransientSensitivity, 0.85f, "TransientSensitivity NaN fallback");
    realValue = 2.0f;
    Expect(params.SetParam(EFFECT_PARAM_TRANSIENT_SENSITIVITY_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam TransientSensitivity high");
    ExpectNear(params.Values.fTransientSensitivity, 1.0f, "TransientSensitivity high clamp");

    realValue = -0.25f;
    Expect(params.SetParam(EFFECT_PARAM_RAIN_INTENSITY_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam RainIntensity low");
    ExpectNear(params.Values.fRainIntensity, 0.0f, "RainIntensity low clamp");
    realValue = 9.0f;
    Expect(params.SetParam(EFFECT_PARAM_WIND_SPEED_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WindSpeed");
    ExpectNear(params.Values.fWindSpeed, 9.0f, "WindSpeed in range");
    realValue = 99.0f;
    Expect(params.SetParam(EFFECT_PARAM_WIND_SPEED_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WindSpeed high");
    ExpectNear(params.Values.fWindSpeed, 40.0f, "WindSpeed high clamp");
    realValue = -1.0f;
    Expect(params.SetParam(EFFECT_PARAM_WIND_DIRECTION_DEGREES_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WindDirection low");
    ExpectNear(params.Values.fWindDirectionDegrees, 0.0f, "WindDirection low clamp");
    realValue = 999.0f;
    Expect(params.SetParam(EFFECT_PARAM_WIND_DIRECTION_DEGREES_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WindDirection high");
    ExpectNear(params.Values.fWindDirectionDegrees, 360.0f, "WindDirection high clamp");
    realValue = std::numeric_limits<AkReal32>::infinity();
    Expect(params.SetParam(EFFECT_PARAM_WIND_GUSTINESS_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam WindGustiness Inf");
    ExpectNear(params.Values.fWindGustiness, 0.0f, "WindGustiness Inf fallback");

    intValue = -1;
    Expect(params.SetParam(EFFECT_PARAM_SEED_ID, &intValue, sizeof(intValue)) == AK_Success, "SetParam Seed low");
    Expect(params.Values.iSeed == 0, "Seed low clamp");
    intValue = 99;
    Expect(params.SetParam(EFFECT_PARAM_FEATURE_COUNT_ID, &intValue, sizeof(intValue)) == AK_Success, "SetParam FeatureCount high");
    Expect(params.Values.iFeatureCount == 8, "FeatureCount high clamp");

    realValue = -25.0f;
    Expect(params.SetParam(EFFECT_PARAM_LISTENER_X_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam ListenerX");
    ExpectNear(params.Values.fListenerX, -25.0f, "ListenerX in range");
    realValue = -12.0f;
    Expect(params.SetParam(EFFECT_PARAM_LISTENER_YAW_DEGREES_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam ListenerYaw low");
    ExpectNear(params.Values.fListenerYawDegrees, -12.0f, "ListenerYaw in range");
    realValue = 720.0f;
    Expect(params.SetParam(EFFECT_PARAM_LISTENER_YAW_DEGREES_ID, &realValue, sizeof(realValue)) == AK_Success, "SetParam ListenerYaw high");
    ExpectNear(params.Values.fListenerYawDegrees, 180.0f, "ListenerYaw high clamp");

    const AkUInt32 slot = 7u;
    realValue = std::numeric_limits<AkReal32>::quiet_NaN();
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_X_OFFSET), &realValue, sizeof(realValue)) == AK_Success, "SetParam FeatureX NaN");
    ExpectNear(params.Values.Features[slot].fX, 0.0f, "FeatureX NaN fallback");
    realValue = 0.01f;
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_RADIUS_OFFSET), &realValue, sizeof(realValue)) == AK_Success, "SetParam FeatureRadius low");
    ExpectNear(params.Values.Features[slot].fRadius, 0.2f, "FeatureRadius low clamp");
    realValue = 20000.0f;
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_RADIUS_OFFSET), &realValue, sizeof(realValue)) == AK_Success, "SetParam FeatureRadius high");
    ExpectNear(params.Values.Features[slot].fRadius, 10000.0f, "FeatureRadius high clamp");
    for (AkInt32 profile = 0; profile <= 4; ++profile)
    {
        intValue = profile;
        Expect(
            params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_PROFILE_OFFSET), &intValue, sizeof(intValue)) == AK_Success,
            "SetParam FeatureProfile round-trip " + std::to_string(profile));
        Expect(
            params.Values.Features[slot].iProfile == profile,
            "FeatureProfile round-trip must preserve " + std::to_string(profile));
    }
    intValue = 99;
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_PROFILE_OFFSET), &intValue, sizeof(intValue)) == AK_Success, "SetParam FeatureProfile high");
    Expect(params.Values.Features[slot].iProfile == 4, "FeatureProfile high clamp");
    intValue = -7;
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_MASK_OFFSET), &intValue, sizeof(intValue)) == AK_Success, "SetParam FeatureMask low");
    Expect(params.Values.Features[slot].iMask == 0, "FeatureMask low clamp");
    intValue = 5000;
    Expect(params.SetParam(EffectFeatureParameterId(slot, EFFECT_PARAM_FEATURE_PRIORITY_OFFSET), &intValue, sizeof(intValue)) == AK_Success, "SetParam FeaturePriority high");
    Expect(params.Values.Features[slot].iPriority == 1000, "FeaturePriority high clamp");
}
} // namespace

int main()
{
    static_assert(kEffectBankSize == 281, "Effect bank block size is part of the ABI.");

    TestDefaultsFromEmptyInit();
    TestCompleteBankMapping();
    TestRejectedBlocks();
    TestSetParamSizeChecks();
    TestSetParamClamping();

    if (g_failureCount != 0)
    {
        std::cerr << g_failureCount << " effect bank contract assertion(s) failed.\n";
        return 1;
    }

    std::cout << "EffectParams bank ABI contract passed: 281-byte block, defaults, mapping, rejects, and clamps.\n";
    return 0;
}
