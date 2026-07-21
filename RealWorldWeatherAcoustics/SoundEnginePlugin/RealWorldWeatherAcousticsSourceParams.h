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

#ifndef RealWorldWeatherAcousticsSourceParams_H
#define RealWorldWeatherAcousticsSourceParams_H

#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Plugin/PluginServices/AkFXParameterChangeHandler.h>

enum RealWorldWeatherAcousticsParameterId : AkPluginParamID
{
    PARAM_DURATION_ID = 0,
    PARAM_MASTER_GAIN_DB_ID = 1,
    PARAM_RAIN_INTENSITY_ID = 2,
    PARAM_SEED_ID = 3,
    PARAM_GEOMETRY_ENABLED_ID = 4,
    PARAM_LISTENER_X_ID = 5,
    PARAM_LISTENER_Y_ID = 6,
    PARAM_LISTENER_Z_ID = 7,
    PARAM_LISTENER_YAW_DEGREES_ID = 8,
    PARAM_FEATURE_COUNT_ID = 9,
    PARAM_FEATURES_BEGIN_ID = 10,
};

enum RealWorldWeatherAcousticsFeatureParameterOffset : AkUInt32
{
    PARAM_FEATURE_X_OFFSET = 0,
    PARAM_FEATURE_Y_OFFSET = 1,
    PARAM_FEATURE_Z_OFFSET = 2,
    PARAM_FEATURE_RADIUS_OFFSET = 3,
    PARAM_FEATURE_PROFILE_OFFSET = 4,
    PARAM_FEATURE_MASK_OFFSET = 5,
    PARAM_FEATURE_PRIORITY_OFFSET = 6,
    PARAM_FEATURE_PARAMETER_COUNT = 7,
};

static const AkUInt32 FEATURE_SLOT_COUNT = 8;
static const AkUInt32 NUM_PARAMS = PARAM_FEATURES_BEGIN_ID + FEATURE_SLOT_COUNT * PARAM_FEATURE_PARAMETER_COUNT;

inline AkPluginParamID FeatureParameterId(AkUInt32 in_slot, AkUInt32 in_offset)
{
    return static_cast<AkPluginParamID>(PARAM_FEATURES_BEGIN_ID + in_slot * PARAM_FEATURE_PARAMETER_COUNT + in_offset);
}

struct RealWorldWeatherAcousticsFeatureParams
{
    AkReal32 fX;
    AkReal32 fY;
    AkReal32 fZ;
    AkReal32 fRadius;
    AkInt32 iProfile;
    AkInt32 iMask;
    AkInt32 iPriority;
};

struct RealWorldWeatherAcousticsParameterValues
{
    AkReal32 fDuration;
    AkReal32 fMasterGainDb;
    AkReal32 fRainIntensity;
    AkInt32 iSeed;
    bool bGeometryEnabled;
    AkReal32 fListenerX;
    AkReal32 fListenerY;
    AkReal32 fListenerZ;
    AkReal32 fListenerYawDegrees;
    AkInt32 iFeatureCount;
    RealWorldWeatherAcousticsFeatureParams Features[FEATURE_SLOT_COUNT];
};

struct RealWorldWeatherAcousticsSourceParams
    : public AK::IAkPluginParam
{
    RealWorldWeatherAcousticsSourceParams();
    RealWorldWeatherAcousticsSourceParams(const RealWorldWeatherAcousticsSourceParams& in_rParams);

    ~RealWorldWeatherAcousticsSourceParams();

    /// Create a duplicate of the parameter node instance in its current state.
    IAkPluginParam* Clone(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /// Initialize the plug-in parameter node interface.
    /// Initializes the internal parameter structure to default values or with the provided parameter block if it is valid.
    AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, const void* in_pParamsBlock, AkUInt32 in_ulBlockSize) override;

    /// Called by the sound engine when a parameter node is terminated.
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /// Set all plug-in parameters at once using a parameter block.
    AKRESULT SetParamsBlock(const void* in_pParamsBlock, AkUInt32 in_ulBlockSize) override;

    /// Update a single parameter at a time and perform the necessary actions on the parameter changes.
    AKRESULT SetParam(AkPluginParamID in_paramID, const void* in_pValue, AkUInt32 in_ulParamSize) override;

    AK::AkFXParameterChangeHandler<NUM_PARAMS> m_paramChangeHandler;

    RealWorldWeatherAcousticsParameterValues Values;

private:
    void SetDefaults();
    void Validate();
};

#endif // RealWorldWeatherAcousticsSourceParams_H
