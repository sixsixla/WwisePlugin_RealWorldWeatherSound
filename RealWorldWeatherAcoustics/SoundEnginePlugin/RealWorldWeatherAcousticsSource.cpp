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

#include "RealWorldWeatherAcousticsSource.h"
#include "../RealWorldWeatherAcousticsConfig.h"

#include <AK/AkWwiseSDKVersion.h>

AK::IAkPlugin* CreateRealWorldWeatherAcousticsSource(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsSource());
}

AK::IAkPluginParam* CreateRealWorldWeatherAcousticsSourceParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsSourceParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(RealWorldWeatherAcousticsSource, AkPluginTypeSource, RealWorldWeatherAcousticsConfig::CompanyID, RealWorldWeatherAcousticsConfig::PluginID)

RealWorldWeatherAcousticsSource::RealWorldWeatherAcousticsSource()
    : m_pParams(nullptr)
    , m_pSynth(nullptr)
{
}

RealWorldWeatherAcousticsSource::~RealWorldWeatherAcousticsSource()
{
}

AKRESULT RealWorldWeatherAcousticsSource::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkSourcePluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    if (in_pAllocator == nullptr || in_pContext == nullptr || in_pParams == nullptr)
    {
        return AK_InvalidParameter;
    }

    m_pParams = static_cast<RealWorldWeatherAcousticsSourceParams*>(in_pParams);
    in_rFormat.channelConfig.SetStandard(AK_SPEAKER_SETUP_STEREO);

    m_pSynth = AK_PLUGIN_NEW(in_pAllocator, rwwa::RainSynth(in_rFormat.uSampleRate));
    if (m_pSynth == nullptr)
    {
        return AK_InsufficientMemory;
    }

    m_durationHandler.Setup(m_pParams->Values.fDuration, in_pContext->GetNumLoops(), in_rFormat.uSampleRate);
    m_pSynth->Reset(static_cast<AkUInt32>(m_pParams->Values.iSeed));
    m_sceneSnapshot = {};
    m_pParams->m_paramChangeHandler.ResetAllParamChanges();

    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsSource::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    if (m_pSynth != nullptr)
    {
        AK_PLUGIN_DELETE(in_pAllocator, m_pSynth);
        m_pSynth = nullptr;
    }
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsSource::Reset()
{
    m_durationHandler.Reset();
    if (m_pSynth != nullptr && m_pParams != nullptr)
    {
        m_pSynth->Reset(static_cast<AkUInt32>(m_pParams->Values.iSeed));
    }
    m_sceneSnapshot = {};
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsSource::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeSource;
    out_rPluginInfo.bIsInPlace = true;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void RealWorldWeatherAcousticsSource::Execute(AkAudioBuffer* out_pBuffer)
{
    if (out_pBuffer == nullptr || m_pParams == nullptr || m_pSynth == nullptr)
    {
        return;
    }

    m_durationHandler.SetDuration(m_pParams->Values.fDuration);
    m_durationHandler.ProduceBuffer(out_pBuffer);

    if (out_pBuffer->uValidFrames == 0)
    {
        return;
    }

    if (out_pBuffer->NumChannels() != 2)
    {
        for (AkUInt32 channel = 0; channel < out_pBuffer->NumChannels(); ++channel)
        {
            AkReal32* buffer = out_pBuffer->GetChannel(channel);
            for (AkUInt16 frame = 0; frame < out_pBuffer->uValidFrames; ++frame)
            {
                buffer[frame] = 0.0f;
            }
        }
        return;
    }

    const RealWorldWeatherAcousticsParameterValues& values = m_pParams->Values;
    rwwa::ListenerState listener{};
    listener.position = {values.fListenerX, values.fListenerY, values.fListenerZ};
    listener.yawRadians = values.fListenerYawDegrees * 0.01745329251994329577f;

    rwwa::WeatherState weather{};
    weather.rainIntensity = values.fRainIntensity;
    weather.seed = static_cast<AkUInt32>(values.iSeed);
    weather.geometryEnabled = values.bGeometryEnabled;
    weather.masterGainLinear = AK_DBTOLIN(values.fMasterGainDb);

    rwwa::SceneInput scene{};
    scene.featureCount = static_cast<AkUInt32>(values.iFeatureCount);
    for (AkUInt32 slot = 0; slot < scene.featureCount; ++slot)
    {
        const RealWorldWeatherAcousticsFeatureParams& source = values.Features[slot];
        rwwa::SphereFeature& target = scene.features[slot];
        target.id = static_cast<std::uint64_t>(slot + 1u);
        target.position = {source.fX, source.fY, source.fZ};
        target.radius = source.fRadius;
        target.profileId = static_cast<std::uint32_t>(source.iProfile);
        target.responseMask = static_cast<std::uint32_t>(source.iMask);
        target.priority = static_cast<std::int32_t>(source.iPriority);
    }

    m_sceneSnapshot = rwwa::CompileScene(scene, listener, weather);
    m_pSynth->Process(
        m_sceneSnapshot,
        out_pBuffer->GetChannel(0),
        out_pBuffer->GetChannel(1),
        out_pBuffer->uValidFrames);
    m_pParams->m_paramChangeHandler.ResetAllParamChanges();
}

AkReal32 RealWorldWeatherAcousticsSource::GetDuration() const
{
    return m_durationHandler.GetDuration() * 1000.0f;
}
