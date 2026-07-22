#ifndef RealWorldWeatherAcousticsEffectParams_H
#define RealWorldWeatherAcousticsEffectParams_H

#include <AK/Plugin/PluginServices/AkFXParameterChangeHandler.h>
#include <AK/SoundEngine/Common/IAkPlugin.h>

enum RealWorldWeatherAcousticsEffectParameterId : AkPluginParamID
{
    EFFECT_PARAM_INPUT_ROLE_ID = 0,
    EFFECT_PARAM_WET_MIX_ID = 1,
    EFFECT_PARAM_RESPONSE_GAIN_DB_ID = 2,
    EFFECT_PARAM_TRANSIENT_SENSITIVITY_ID = 3,
    EFFECT_PARAM_RAIN_INTENSITY_ID = 4,
    EFFECT_PARAM_WIND_SPEED_ID = 5,
    EFFECT_PARAM_WIND_DIRECTION_DEGREES_ID = 6,
    EFFECT_PARAM_WIND_GUSTINESS_ID = 7,
    EFFECT_PARAM_SEED_ID = 8,
    EFFECT_PARAM_GEOMETRY_ENABLED_ID = 9,
    EFFECT_PARAM_LISTENER_X_ID = 10,
    EFFECT_PARAM_LISTENER_Y_ID = 11,
    EFFECT_PARAM_LISTENER_Z_ID = 12,
    EFFECT_PARAM_LISTENER_YAW_DEGREES_ID = 13,
    EFFECT_PARAM_FEATURE_COUNT_ID = 14,
    EFFECT_PARAM_FEATURES_BEGIN_ID = 15,
};

enum RealWorldWeatherAcousticsEffectFeatureParameterOffset : AkUInt32
{
    EFFECT_PARAM_FEATURE_X_OFFSET = 0,
    EFFECT_PARAM_FEATURE_Y_OFFSET = 1,
    EFFECT_PARAM_FEATURE_Z_OFFSET = 2,
    EFFECT_PARAM_FEATURE_RADIUS_OFFSET = 3,
    EFFECT_PARAM_FEATURE_PROFILE_OFFSET = 4,
    EFFECT_PARAM_FEATURE_MASK_OFFSET = 5,
    EFFECT_PARAM_FEATURE_PRIORITY_OFFSET = 6,
    EFFECT_PARAM_FEATURE_PARAMETER_COUNT = 7,
};

static const AkUInt32 EFFECT_FEATURE_SLOT_COUNT = 8;
static const AkUInt32 EFFECT_NUM_PARAMS =
    EFFECT_PARAM_FEATURES_BEGIN_ID + EFFECT_FEATURE_SLOT_COUNT * EFFECT_PARAM_FEATURE_PARAMETER_COUNT;

inline AkPluginParamID EffectFeatureParameterId(AkUInt32 in_slot, AkUInt32 in_offset)
{
    return static_cast<AkPluginParamID>(
        EFFECT_PARAM_FEATURES_BEGIN_ID + in_slot * EFFECT_PARAM_FEATURE_PARAMETER_COUNT + in_offset);
}

struct RealWorldWeatherAcousticsEffectFeatureParams
{
    AkReal32 fX;
    AkReal32 fY;
    AkReal32 fZ;
    AkReal32 fRadius;
    AkInt32 iProfile;
    AkInt32 iMask;
    AkInt32 iPriority;
};

struct RealWorldWeatherAcousticsEffectParameterValues
{
    AkInt32 iInputRole;
    AkReal32 fWetMix;
    AkReal32 fResponseGainDb;
    AkReal32 fTransientSensitivity;
    AkReal32 fRainIntensity;
    AkReal32 fWindSpeed;
    AkReal32 fWindDirectionDegrees;
    AkReal32 fWindGustiness;
    AkInt32 iSeed;
    bool bGeometryEnabled;
    AkReal32 fListenerX;
    AkReal32 fListenerY;
    AkReal32 fListenerZ;
    AkReal32 fListenerYawDegrees;
    AkInt32 iFeatureCount;
    RealWorldWeatherAcousticsEffectFeatureParams Features[EFFECT_FEATURE_SLOT_COUNT];
};

class RealWorldWeatherAcousticsEffectParams final : public AK::IAkPluginParam
{
public:
    RealWorldWeatherAcousticsEffectParams();
    RealWorldWeatherAcousticsEffectParams(const RealWorldWeatherAcousticsEffectParams& in_rParams);
    ~RealWorldWeatherAcousticsEffectParams();

    IAkPluginParam* Clone(AK::IAkPluginMemAlloc* in_pAllocator) override;
    AKRESULT Init(
        AK::IAkPluginMemAlloc* in_pAllocator,
        const void* in_pParamsBlock,
        AkUInt32 in_ulBlockSize) override;
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;
    AKRESULT SetParamsBlock(const void* in_pParamsBlock, AkUInt32 in_ulBlockSize) override;
    AKRESULT SetParam(
        AkPluginParamID in_paramID,
        const void* in_pValue,
        AkUInt32 in_ulParamSize) override;

    AK::AkFXParameterChangeHandler<EFFECT_NUM_PARAMS> m_paramChangeHandler;
    RealWorldWeatherAcousticsEffectParameterValues Values;

private:
    void SetDefaults();
    void Validate();
};

#endif // RealWorldWeatherAcousticsEffectParams_H
