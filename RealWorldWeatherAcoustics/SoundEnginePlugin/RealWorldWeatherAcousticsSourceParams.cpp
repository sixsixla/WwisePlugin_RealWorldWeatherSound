/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2026 Audiokinetic Inc.
*******************************************************************************/

#include "RealWorldWeatherAcousticsSourceParams.h"

#include <AK/Tools/Common/AkBankReadHelpers.h>
#include <cmath>

namespace
{
static const AkReal32 kDefaultDuration = 60.0f;
static const AkReal32 kDefaultMasterGainDb = -12.0f;
static const AkReal32 kDefaultRainIntensity = 0.25f;
static const AkReal32 kDefaultWindSpeed = 12.0f;
static const AkReal32 kDefaultWindDirectionDegrees = 0.0f;
static const AkReal32 kDefaultWindGustiness = 0.55f;
static const AkInt32 kDefaultSeed = 1337;
static const AkReal32 kDefaultFeatureRadius = 2.0f;
static const AkUInt32 kLegacyBankBlockSize =
    39u * static_cast<AkUInt32>(sizeof(AkReal32)) +
    26u * static_cast<AkUInt32>(sizeof(AkInt32)) +
    static_cast<AkUInt32>(sizeof(bool));
static const AkUInt32 kCurrentBankBlockSize =
    kLegacyBankBlockSize + 3u * static_cast<AkUInt32>(sizeof(AkReal32));

AkReal32 ClampFinite(AkReal32 in_value, AkReal32 in_minimum, AkReal32 in_maximum, AkReal32 in_fallback)
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
}

RealWorldWeatherAcousticsSourceParams::RealWorldWeatherAcousticsSourceParams()
{
    SetDefaults();
}

RealWorldWeatherAcousticsSourceParams::~RealWorldWeatherAcousticsSourceParams()
{
}

RealWorldWeatherAcousticsSourceParams::RealWorldWeatherAcousticsSourceParams(const RealWorldWeatherAcousticsSourceParams& in_rParams)
{
    Values = in_rParams.Values;
    m_paramChangeHandler.SetAllParamChanges();
}

AK::IAkPluginParam* RealWorldWeatherAcousticsSourceParams::Clone(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsSourceParams(*this));
}

AKRESULT RealWorldWeatherAcousticsSourceParams::Init(AK::IAkPluginMemAlloc* in_pAllocator, const void* in_pParamsBlock, AkUInt32 in_ulBlockSize)
{
    if (in_ulBlockSize == 0)
    {
        SetDefaults();
        m_paramChangeHandler.SetAllParamChanges();
        return AK_Success;
    }

    return SetParamsBlock(in_pParamsBlock, in_ulBlockSize);
}

AKRESULT RealWorldWeatherAcousticsSourceParams::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsSourceParams::SetParamsBlock(const void* in_pParamsBlock, AkUInt32 in_ulBlockSize)
{
    const bool hasWindParameters = in_ulBlockSize == kCurrentBankBlockSize;
    if (in_pParamsBlock == nullptr ||
        (in_ulBlockSize != kLegacyBankBlockSize && !hasWindParameters))
    {
        return AK_InvalidParameter;
    }

    AKRESULT eResult = AK_Success;
    AkUInt8* pParamsBlock = (AkUInt8*)in_pParamsBlock;

    RealWorldWeatherAcousticsParameterValues values;
    values.fDuration = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.fMasterGainDb = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.fRainIntensity = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.iSeed = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
    values.bGeometryEnabled = READBANKDATA(bool, pParamsBlock, in_ulBlockSize);
    values.fListenerX = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.fListenerY = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.fListenerZ = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.fListenerYawDegrees = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    values.iFeatureCount = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
    for (AkUInt32 slot = 0; slot < FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsFeatureParams& feature = values.Features[slot];
        feature.fX = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        feature.fY = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        feature.fZ = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        feature.fRadius = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        feature.iProfile = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
        feature.iMask = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
        feature.iPriority = READBANKDATA(AkInt32, pParamsBlock, in_ulBlockSize);
    }
    if (hasWindParameters)
    {
        values.fWindSpeed = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        values.fWindDirectionDegrees = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
        values.fWindGustiness = READBANKDATA(AkReal32, pParamsBlock, in_ulBlockSize);
    }
    else
    {
        // v0.1 banks had no wind fields. Keeping wind disabled preserves their
        // rain-only behavior after loading them with the v0.2 SoundEngine plug-in.
        values.fWindSpeed = 0.0f;
        values.fWindDirectionDegrees = kDefaultWindDirectionDegrees;
        values.fWindGustiness = 0.0f;
    }
    CHECKBANKDATASIZE(in_ulBlockSize, eResult);

    if (eResult != AK_Success)
    {
        return eResult;
    }

    Values = values;
    Validate();
    m_paramChangeHandler.SetAllParamChanges();

    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsSourceParams::SetParam(AkPluginParamID in_paramID, const void* in_pValue, AkUInt32 in_ulParamSize)
{
    if (in_pValue == nullptr)
    {
        return AK_InvalidParameter;
    }

    switch (in_paramID)
    {
    case PARAM_DURATION_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fDuration = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_MASTER_GAIN_DB_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fMasterGainDb = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_RAIN_INTENSITY_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fRainIntensity = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_SEED_ID:
        if (in_ulParamSize != sizeof(AkInt32)) return AK_InvalidParameter;
        Values.iSeed = *static_cast<const AkInt32*>(in_pValue);
        break;
    case PARAM_GEOMETRY_ENABLED_ID:
        if (in_ulParamSize != sizeof(bool)) return AK_InvalidParameter;
        Values.bGeometryEnabled = *static_cast<const bool*>(in_pValue);
        break;
    case PARAM_LISTENER_X_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fListenerX = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_LISTENER_Y_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fListenerY = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_LISTENER_Z_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fListenerZ = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_LISTENER_YAW_DEGREES_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fListenerYawDegrees = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_FEATURE_COUNT_ID:
        if (in_ulParamSize != sizeof(AkInt32)) return AK_InvalidParameter;
        Values.iFeatureCount = *static_cast<const AkInt32*>(in_pValue);
        break;
    case PARAM_WIND_SPEED_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fWindSpeed = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_WIND_DIRECTION_DEGREES_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fWindDirectionDegrees = *static_cast<const AkReal32*>(in_pValue);
        break;
    case PARAM_WIND_GUSTINESS_ID:
        if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
        Values.fWindGustiness = *static_cast<const AkReal32*>(in_pValue);
        break;
    default:
        if (in_paramID < PARAM_FEATURES_BEGIN_ID || in_paramID >= PARAM_WIND_SPEED_ID)
        {
            return AK_InvalidParameter;
        }

        {
            const AkUInt32 relativeId = in_paramID - PARAM_FEATURES_BEGIN_ID;
            const AkUInt32 slot = relativeId / PARAM_FEATURE_PARAMETER_COUNT;
            const AkUInt32 offset = relativeId % PARAM_FEATURE_PARAMETER_COUNT;
            RealWorldWeatherAcousticsFeatureParams& feature = Values.Features[slot];
            if (offset <= PARAM_FEATURE_RADIUS_OFFSET)
            {
                if (in_ulParamSize != sizeof(AkReal32)) return AK_InvalidParameter;
                const AkReal32 value = *static_cast<const AkReal32*>(in_pValue);
                switch (offset)
                {
                case PARAM_FEATURE_X_OFFSET: feature.fX = value; break;
                case PARAM_FEATURE_Y_OFFSET: feature.fY = value; break;
                case PARAM_FEATURE_Z_OFFSET: feature.fZ = value; break;
                case PARAM_FEATURE_RADIUS_OFFSET: feature.fRadius = value; break;
                }
            }
            else
            {
                if (in_ulParamSize != sizeof(AkInt32)) return AK_InvalidParameter;
                const AkInt32 value = *static_cast<const AkInt32*>(in_pValue);
                switch (offset)
                {
                case PARAM_FEATURE_PROFILE_OFFSET: feature.iProfile = value; break;
                case PARAM_FEATURE_MASK_OFFSET: feature.iMask = value; break;
                case PARAM_FEATURE_PRIORITY_OFFSET: feature.iPriority = value; break;
                }
            }
        }
        break;
    }

    Validate();
    m_paramChangeHandler.SetParamChange(in_paramID);
    return AK_Success;
}

void RealWorldWeatherAcousticsSourceParams::SetDefaults()
{
    Values.fDuration = kDefaultDuration;
    Values.fMasterGainDb = kDefaultMasterGainDb;
    Values.fRainIntensity = kDefaultRainIntensity;
    Values.iSeed = kDefaultSeed;
    Values.bGeometryEnabled = true;
    Values.fListenerX = 0.0f;
    Values.fListenerY = 0.0f;
    Values.fListenerZ = 0.0f;
    Values.fListenerYawDegrees = 0.0f;
    Values.iFeatureCount = 4;
    Values.fWindSpeed = kDefaultWindSpeed;
    Values.fWindDirectionDegrees = kDefaultWindDirectionDegrees;
    Values.fWindGustiness = kDefaultWindGustiness;

    for (AkUInt32 slot = 0; slot < FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsFeatureParams& feature = Values.Features[slot];
        feature.fX = 0.0f;
        feature.fY = 0.0f;
        feature.fZ = 0.0f;
        feature.fRadius = kDefaultFeatureRadius;
        feature.iProfile = static_cast<AkInt32>(slot % 4u);
        feature.iMask = 3;
        feature.iPriority = 1;
    }

    Values.Features[0].fZ = 6.0f;
    Values.Features[1].fX = 6.0f;
    Values.Features[2].fZ = -6.0f;
    Values.Features[3].fX = -6.0f;
}

void RealWorldWeatherAcousticsSourceParams::Validate()
{
    Values.fDuration = ClampFinite(Values.fDuration, 0.001f, 3600.0f, kDefaultDuration);
    Values.fMasterGainDb = ClampFinite(Values.fMasterGainDb, -96.3f, 12.0f, kDefaultMasterGainDb);
    Values.fRainIntensity = ClampFinite(Values.fRainIntensity, 0.0f, 1.0f, kDefaultRainIntensity);
    Values.iSeed = ClampInt(Values.iSeed, 0, 2147483647);
    Values.fListenerX = ClampFinite(Values.fListenerX, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerY = ClampFinite(Values.fListenerY, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerZ = ClampFinite(Values.fListenerZ, -10000.0f, 10000.0f, 0.0f);
    Values.fListenerYawDegrees = ClampFinite(Values.fListenerYawDegrees, -180.0f, 180.0f, 0.0f);
    Values.iFeatureCount = ClampInt(Values.iFeatureCount, 0, static_cast<AkInt32>(FEATURE_SLOT_COUNT));
    Values.fWindSpeed = ClampFinite(Values.fWindSpeed, 0.0f, 40.0f, kDefaultWindSpeed);
    Values.fWindDirectionDegrees = ClampFinite(Values.fWindDirectionDegrees, 0.0f, 360.0f, kDefaultWindDirectionDegrees);
    Values.fWindGustiness = ClampFinite(Values.fWindGustiness, 0.0f, 1.0f, kDefaultWindGustiness);

    for (AkUInt32 slot = 0; slot < FEATURE_SLOT_COUNT; ++slot)
    {
        RealWorldWeatherAcousticsFeatureParams& feature = Values.Features[slot];
        feature.fX = ClampFinite(feature.fX, -10000.0f, 10000.0f, 0.0f);
        feature.fY = ClampFinite(feature.fY, -10000.0f, 10000.0f, 0.0f);
        feature.fZ = ClampFinite(feature.fZ, -10000.0f, 10000.0f, 0.0f);
        feature.fRadius = ClampFinite(feature.fRadius, 0.2f, 10000.0f, kDefaultFeatureRadius);
        feature.iProfile = ClampInt(feature.iProfile, 0, 3);
        feature.iMask = ClampInt(feature.iMask, 0, 3);
        feature.iPriority = ClampInt(feature.iPriority, 0, 1000);
    }
}
