#include "RealWorldWeatherAcousticsEffectParams.h"

#include <AK/Tools/Common/AkBankReadHelpers.h>

#include <cmath>

namespace
{
constexpr AkInt32 kDefaultInputRole = 0;
constexpr AkReal32 kDefaultWetMix = 1.0f;
constexpr AkReal32 kDefaultResponseGainDb = 10.0f;
constexpr AkReal32 kDefaultTransientSensitivity = 0.85f;
constexpr AkReal32 kDefaultRainIntensity = 0.9f;
constexpr AkReal32 kDefaultWindSpeed = 0.0f;
constexpr AkReal32 kDefaultWindDirectionDegrees = 0.0f;
constexpr AkReal32 kDefaultWindGustiness = 0.0f;
constexpr AkInt32 kDefaultSeed = 1337;
constexpr AkReal32 kDefaultFeatureRadius = 2.0f;
constexpr AkReal32 kDefaultAuditionFeatureRadius = 3.2f;
constexpr AkInt32 kMaximumProfileId = 4; // Plastic is appended after Metal/Wood/Glass/Tile.

constexpr AkUInt32 kEffectBankBlockSize =
    43u * static_cast<AkUInt32>(sizeof(AkReal32)) +
    27u * static_cast<AkUInt32>(sizeof(AkInt32)) +
    static_cast<AkUInt32>(sizeof(bool));

AkReal32 ClampFinite(
    AkReal32 in_value,
    AkReal32 in_minimum,
    AkReal32 in_maximum,
    AkReal32 in_fallback)
{
    if (!std::isfinite(in_value))
    {
        return in_fallback;
    }
    if (in_value < in_minimum)
    {
        return in_minimum;
    }
    if (in_value > in_maximum)
    {
        return in_maximum;
    }
    return in_value;
}

AkInt32 ClampInt(AkInt32 in_value, AkInt32 in_minimum, AkInt32 in_maximum)
{
    if (in_value < in_minimum)
    {
        return in_minimum;
    }
    if (in_value > in_maximum)
    {
        return in_maximum;
    }
    return in_value;
}

template <typename T>
bool ReadScalar(const void* in_pValue, AkUInt32 in_ulParamSize, T& out_value)
{
    if (in_ulParamSize != sizeof(T))
    {
        return false;
    }
    out_value = *static_cast<const T*>(in_pValue);
    return true;
}
} // namespace

RealWorldWeatherAcousticsEffectParams::RealWorldWeatherAcousticsEffectParams()
{
    SetDefaults();
}

RealWorldWeatherAcousticsEffectParams::RealWorldWeatherAcousticsEffectParams(
    const RealWorldWeatherAcousticsEffectParams& in_rParams)
    : Values(in_rParams.Values)
{
    m_paramChangeHandler.SetAllParamChanges();
}

RealWorldWeatherAcousticsEffectParams::~RealWorldWeatherAcousticsEffectParams() = default;

AK::IAkPluginParam* RealWorldWeatherAcousticsEffectParams::Clone(
    AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsEffectParams(*this));
}

AKRESULT RealWorldWeatherAcousticsEffectParams::Init(
    AK::IAkPluginMemAlloc* in_pAllocator,
    const void* in_pParamsBlock,
    AkUInt32 in_ulBlockSize)
{
    (void)in_pAllocator;
    if (in_ulBlockSize == 0)
    {
        SetDefaults();
        m_paramChangeHandler.SetAllParamChanges();
        return AK_Success;
    }
    return SetParamsBlock(in_pParamsBlock, in_ulBlockSize);
}

AKRESULT RealWorldWeatherAcousticsEffectParams::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsEffectParams::SetParamsBlock(
    const void* in_pParamsBlock,
    AkUInt32 in_ulBlockSize)
{
    if (in_pParamsBlock == nullptr || in_ulBlockSize != kEffectBankBlockSize)
    {
        return AK_InvalidParameter;
    }

    AKRESULT result = AK_Success;
    AkUInt8* paramsBlock = static_cast<AkUInt8*>(const_cast<void*>(in_pParamsBlock));
    RealWorldWeatherAcousticsEffectParameterValues values{};

    values.iInputRole = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);
    values.fWetMix = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fResponseGainDb = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fTransientSensitivity = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fRainIntensity = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fWindSpeed = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fWindDirectionDegrees = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fWindGustiness = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.iSeed = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);
    values.bGeometryEnabled = READBANKDATA(bool, paramsBlock, in_ulBlockSize);
    values.fListenerX = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fListenerY = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fListenerZ = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.fListenerYawDegrees = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
    values.iFeatureCount = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);

    for (AkUInt32 slot = 0; slot < EFFECT_FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsEffectFeatureParams& feature = values.Features[slot];
        feature.fX = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
        feature.fY = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
        feature.fZ = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
        feature.fRadius = READBANKDATA(AkReal32, paramsBlock, in_ulBlockSize);
        feature.iProfile = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);
        feature.iMask = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);
        feature.iPriority = READBANKDATA(AkInt32, paramsBlock, in_ulBlockSize);
    }

    CHECKBANKDATASIZE(in_ulBlockSize, result);
    if (result != AK_Success)
    {
        return result;
    }

    Values = values;
    Validate();
    m_paramChangeHandler.SetAllParamChanges();
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsEffectParams::SetParam(
    AkPluginParamID in_paramID,
    const void* in_pValue,
    AkUInt32 in_ulParamSize)
{
    if (in_pValue == nullptr || in_paramID >= EFFECT_NUM_PARAMS)
    {
        return AK_InvalidParameter;
    }

    bool validSize = true;
    switch (in_paramID)
    {
    case EFFECT_PARAM_INPUT_ROLE_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.iInputRole);
        break;
    case EFFECT_PARAM_WET_MIX_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fWetMix);
        break;
    case EFFECT_PARAM_RESPONSE_GAIN_DB_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fResponseGainDb);
        break;
    case EFFECT_PARAM_TRANSIENT_SENSITIVITY_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fTransientSensitivity);
        break;
    case EFFECT_PARAM_RAIN_INTENSITY_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fRainIntensity);
        break;
    case EFFECT_PARAM_WIND_SPEED_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fWindSpeed);
        break;
    case EFFECT_PARAM_WIND_DIRECTION_DEGREES_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fWindDirectionDegrees);
        break;
    case EFFECT_PARAM_WIND_GUSTINESS_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fWindGustiness);
        break;
    case EFFECT_PARAM_SEED_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.iSeed);
        break;
    case EFFECT_PARAM_GEOMETRY_ENABLED_ID:
        if (in_ulParamSize == sizeof(AkReal32))
        {
            Values.bGeometryEnabled = *static_cast<const AkReal32*>(in_pValue) != 0.0f;
        }
        else
        {
            validSize = ReadScalar(in_pValue, in_ulParamSize, Values.bGeometryEnabled);
        }
        break;
    case EFFECT_PARAM_LISTENER_X_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fListenerX);
        break;
    case EFFECT_PARAM_LISTENER_Y_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fListenerY);
        break;
    case EFFECT_PARAM_LISTENER_Z_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fListenerZ);
        break;
    case EFFECT_PARAM_LISTENER_YAW_DEGREES_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.fListenerYawDegrees);
        break;
    case EFFECT_PARAM_FEATURE_COUNT_ID:
        validSize = ReadScalar(in_pValue, in_ulParamSize, Values.iFeatureCount);
        break;
    default:
    {
        const AkUInt32 relativeId = in_paramID - EFFECT_PARAM_FEATURES_BEGIN_ID;
        const AkUInt32 slot = relativeId / EFFECT_PARAM_FEATURE_PARAMETER_COUNT;
        const AkUInt32 offset = relativeId % EFFECT_PARAM_FEATURE_PARAMETER_COUNT;
        RealWorldWeatherAcousticsEffectFeatureParams& feature = Values.Features[slot];

        if (offset <= EFFECT_PARAM_FEATURE_RADIUS_OFFSET)
        {
            AkReal32 value = 0.0f;
            validSize = ReadScalar(in_pValue, in_ulParamSize, value);
            if (validSize)
            {
                switch (offset)
                {
                case EFFECT_PARAM_FEATURE_X_OFFSET: feature.fX = value; break;
                case EFFECT_PARAM_FEATURE_Y_OFFSET: feature.fY = value; break;
                case EFFECT_PARAM_FEATURE_Z_OFFSET: feature.fZ = value; break;
                case EFFECT_PARAM_FEATURE_RADIUS_OFFSET: feature.fRadius = value; break;
                }
            }
        }
        else
        {
            AkInt32 value = 0;
            validSize = ReadScalar(in_pValue, in_ulParamSize, value);
            if (validSize)
            {
                switch (offset)
                {
                case EFFECT_PARAM_FEATURE_PROFILE_OFFSET: feature.iProfile = value; break;
                case EFFECT_PARAM_FEATURE_MASK_OFFSET: feature.iMask = value; break;
                case EFFECT_PARAM_FEATURE_PRIORITY_OFFSET: feature.iPriority = value; break;
                }
            }
        }
        break;
    }
    }

    if (!validSize)
    {
        return AK_InvalidParameter;
    }

    Validate();
    m_paramChangeHandler.SetParamChange(in_paramID);
    return AK_Success;
}

void RealWorldWeatherAcousticsEffectParams::SetDefaults()
{
    Values.iInputRole = kDefaultInputRole;
    Values.fWetMix = kDefaultWetMix;
    Values.fResponseGainDb = kDefaultResponseGainDb;
    Values.fTransientSensitivity = kDefaultTransientSensitivity;
    Values.fRainIntensity = kDefaultRainIntensity;
    Values.fWindSpeed = kDefaultWindSpeed;
    Values.fWindDirectionDegrees = kDefaultWindDirectionDegrees;
    Values.fWindGustiness = kDefaultWindGustiness;
    Values.iSeed = kDefaultSeed;
    Values.bGeometryEnabled = true;
    Values.fListenerX = 0.0f;
    Values.fListenerY = 0.0f;
    Values.fListenerZ = 0.0f;
    Values.fListenerYawDegrees = 0.0f;
    Values.iFeatureCount = 4;

    for (AkUInt32 slot = 0; slot < EFFECT_FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsEffectFeatureParams& feature = Values.Features[slot];
        feature.fX = 0.0f;
        feature.fY = 0.0f;
        feature.fZ = 0.0f;
        feature.fRadius = kDefaultFeatureRadius;
        feature.iProfile = static_cast<AkInt32>(slot % 4u);
        feature.iMask = 3;
        feature.iPriority = 1;
    }

    Values.Features[0].fZ = 5.5f;
    Values.Features[0].fRadius = kDefaultAuditionFeatureRadius;
    Values.Features[0].iProfile = 3;
    Values.Features[0].iMask = 1;
    Values.Features[1].fX = 5.5f;
    Values.Features[1].fRadius = kDefaultAuditionFeatureRadius;
    Values.Features[1].iProfile = 4;
    Values.Features[1].iMask = 1;
    Values.Features[2].fZ = -5.5f;
    Values.Features[2].fRadius = kDefaultAuditionFeatureRadius;
    Values.Features[2].iProfile = 0;
    Values.Features[2].iMask = 1;
    Values.Features[3].fX = -5.5f;
    Values.Features[3].fRadius = kDefaultAuditionFeatureRadius;
    Values.Features[3].iProfile = 1;
    Values.Features[3].iMask = 1;
}

void RealWorldWeatherAcousticsEffectParams::Validate()
{
    Values.iInputRole = ClampInt(Values.iInputRole, 0, 2);
    Values.fWetMix = ClampFinite(Values.fWetMix, 0.0f, 1.0f, kDefaultWetMix);
    Values.fResponseGainDb = ClampFinite(Values.fResponseGainDb, -24.0f, 12.0f, kDefaultResponseGainDb);
    Values.fTransientSensitivity = ClampFinite(
        Values.fTransientSensitivity, 0.0f, 1.0f, kDefaultTransientSensitivity);
    Values.fRainIntensity = ClampFinite(Values.fRainIntensity, 0.0f, 1.0f, kDefaultRainIntensity);
    Values.fWindSpeed = ClampFinite(Values.fWindSpeed, 0.0f, 40.0f, kDefaultWindSpeed);
    Values.fWindDirectionDegrees = ClampFinite(
        Values.fWindDirectionDegrees, 0.0f, 360.0f, kDefaultWindDirectionDegrees);
    Values.fWindGustiness = ClampFinite(Values.fWindGustiness, 0.0f, 1.0f, kDefaultWindGustiness);
    Values.iSeed = ClampInt(Values.iSeed, 0, 2147483647);
    Values.fListenerX = ClampFinite(Values.fListenerX, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerY = ClampFinite(Values.fListenerY, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerZ = ClampFinite(Values.fListenerZ, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerYawDegrees = ClampFinite(Values.fListenerYawDegrees, -180.0f, 180.0f, 0.0f);
    Values.iFeatureCount = ClampInt(
        Values.iFeatureCount, 0, static_cast<AkInt32>(EFFECT_FEATURE_SLOT_COUNT));

    for (AkUInt32 slot = 0; slot < EFFECT_FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsEffectFeatureParams& feature = Values.Features[slot];
        feature.fX = ClampFinite(feature.fX, -10000.0f, 10000.0f, 0.0f);
        feature.fY = ClampFinite(feature.fY, -10000.0f, 10000.0f, 0.0f);
        feature.fZ = ClampFinite(feature.fZ, -10000.0f, 10000.0f, 0.0f);
        feature.fRadius = ClampFinite(feature.fRadius, 0.2f, 10000.0f, kDefaultFeatureRadius);
        feature.iProfile = ClampInt(feature.iProfile, 0, kMaximumProfileId);
        feature.iMask = ClampInt(feature.iMask, 0, 3);
        feature.iPriority = ClampInt(feature.iPriority, 0, 1000);
    }
}
