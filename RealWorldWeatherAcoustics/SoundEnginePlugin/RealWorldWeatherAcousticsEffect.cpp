#include "RealWorldWeatherAcousticsEffect.h"
#include "../RealWorldWeatherAcousticsConfig.h"
#define RWWA_RUNTIME_API_INTERNAL
#include "RealWorldWeatherAcousticsRuntimeAPI.h"

#include <AK/AkWwiseSDKVersion.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr std::size_t kMaximumChannelCount = 256u;
} // namespace

AK::IAkPlugin* CreateRealWorldWeatherAcousticsEffect(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsEffect());
}

AK::IAkPluginParam* CreateRealWorldWeatherAcousticsEffectParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, RealWorldWeatherAcousticsEffectParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(
    RealWorldWeatherAcousticsEffect,
    AkPluginTypeEffect,
    RealWorldWeatherAcousticsConfig::CompanyID,
    RealWorldWeatherAcousticsConfig::EffectPluginID)

RealWorldWeatherAcousticsEffect::RealWorldWeatherAcousticsEffect()
    : m_pParams(nullptr)
    , m_pProcessor(nullptr)
    , m_pInputScratch(nullptr)
    , m_scratchFrameCapacity(0u)
    , m_scratchChannelCapacity(0u)
    , m_retainedRuntimeRevision(0u)
    , m_runtimeClaimed(false)
    , m_hasRetainedRuntimeScene(false)
{
}

RealWorldWeatherAcousticsEffect::~RealWorldWeatherAcousticsEffect() = default;

AKRESULT RealWorldWeatherAcousticsEffect::Init(
    AK::IAkPluginMemAlloc* in_pAllocator,
    AK::IAkEffectPluginContext* in_pEffectPluginContext,
    AK::IAkPluginParam* in_pParams,
    AkAudioFormat& io_rFormat)
{
    if (in_pAllocator == nullptr || in_pEffectPluginContext == nullptr || in_pParams == nullptr)
    {
        return AK_InvalidParameter;
    }

    m_pParams = static_cast<RealWorldWeatherAcousticsEffectParams*>(in_pParams);
    m_pProcessor = AK_PLUGIN_NEW(
        in_pAllocator,
        rwwa::GeometryInteractionProcessor(io_rFormat.uSampleRate));
    if (m_pProcessor == nullptr)
    {
        return AK_InsufficientMemory;
    }

    AK::IAkGlobalPluginContext* globalContext = in_pEffectPluginContext->GlobalContext();
    const AkUInt32 channelCount = io_rFormat.GetNumChannels();
    const AkUInt32 maximumFrameCount = globalContext != nullptr
        ? globalContext->GetMaxBufferLength()
        : 0u;
    if (channelCount == 0u || channelCount > kMaximumChannelCount || maximumFrameCount == 0u)
    {
        AK_PLUGIN_DELETE(in_pAllocator, m_pProcessor);
        m_pProcessor = nullptr;
        return AK_InvalidParameter;
    }

    const std::size_t scratchSampleCount =
        static_cast<std::size_t>(channelCount) * maximumFrameCount;
    if (scratchSampleCount > (std::numeric_limits<std::size_t>::max)() / sizeof(AkReal32))
    {
        AK_PLUGIN_DELETE(in_pAllocator, m_pProcessor);
        m_pProcessor = nullptr;
        return AK_InsufficientMemory;
    }
    m_pInputScratch = static_cast<AkReal32*>(AK_PLUGIN_ALLOC(
        in_pAllocator,
        scratchSampleCount * sizeof(AkReal32)));
    if (m_pInputScratch == nullptr)
    {
        AK_PLUGIN_DELETE(in_pAllocator, m_pProcessor);
        m_pProcessor = nullptr;
        return AK_InsufficientMemory;
    }
    m_scratchFrameCapacity = maximumFrameCount;
    m_scratchChannelCapacity = channelCount;

    bool usesRuntimeScene = false;
    std::uint64_t runtimeRevision = 0u;
    m_sceneSnapshot = BuildSceneSnapshot(usesRuntimeScene, runtimeRevision);
    m_pParams->m_paramChangeHandler.ResetAllParamChanges();
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsEffect::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    if (m_pInputScratch != nullptr)
    {
        AK_PLUGIN_FREE(in_pAllocator, m_pInputScratch);
        m_pInputScratch = nullptr;
    }
    m_scratchFrameCapacity = 0u;
    m_scratchChannelCapacity = 0u;
    if (m_pProcessor != nullptr)
    {
        AK_PLUGIN_DELETE(in_pAllocator, m_pProcessor);
        m_pProcessor = nullptr;
    }
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsEffect::Reset()
{
    if (m_pProcessor != nullptr)
    {
        m_pProcessor->Reset();
    }
    bool usesRuntimeScene = false;
    std::uint64_t runtimeRevision = 0u;
    m_sceneSnapshot = BuildSceneSnapshot(usesRuntimeScene, runtimeRevision);
    return AK_Success;
}

AKRESULT RealWorldWeatherAcousticsEffect::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeEffect;
    out_rPluginInfo.bIsInPlace = true;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void RealWorldWeatherAcousticsEffect::Execute(AkAudioBuffer* io_pBuffer)
{
    if (io_pBuffer == nullptr || m_pParams == nullptr || m_pProcessor == nullptr ||
        io_pBuffer->uValidFrames == 0)
    {
        return;
    }

    const AkUInt32 channelCount = io_pBuffer->NumChannels();
    const AkUInt32 frameCount = io_pBuffer->uValidFrames;
    if (channelCount == 0u || channelCount > kMaximumChannelCount ||
        channelCount > m_scratchChannelCapacity || frameCount > m_scratchFrameCapacity ||
        m_pInputScratch == nullptr)
    {
        return;
    }

    std::array<AkReal32*, kMaximumChannelCount> channelBuffers{};
    for (AkUInt32 channel = 0; channel < channelCount; ++channel)
    {
        channelBuffers[channel] = io_pBuffer->GetChannel(channel);
        if (channelBuffers[channel] == nullptr)
        {
            continue;
        }
        AkReal32* scratch = m_pInputScratch +
            static_cast<std::size_t>(channel) * m_scratchFrameCapacity;
        std::copy_n(channelBuffers[channel], frameCount, scratch);
    }

    bool usesRuntimeScene = false;
    std::uint64_t runtimeRevision = 0u;
    m_sceneSnapshot = BuildSceneSnapshot(usesRuntimeScene, runtimeRevision);
    const rwwa::InteractionSettings settings = BuildInteractionSettings();
    m_pProcessor->Process(
        m_sceneSnapshot,
        settings,
        channelBuffers.data(),
        channelCount,
        frameCount);

    rwwa::runtime::EffectBlockDiagnostics diagnostics{};
    for (AkUInt32 channel = 0u; channel < channelCount; ++channel)
    {
        if (channelBuffers[channel] == nullptr)
        {
            continue;
        }
        const AkReal32* scratch = m_pInputScratch +
            static_cast<std::size_t>(channel) * m_scratchFrameCapacity;
        for (AkUInt32 frame = 0u; frame < frameCount; ++frame)
        {
            rwwa::runtime::AccumulateEffectSampleDiagnostics(
                scratch[frame],
                channelBuffers[channel][frame],
                diagnostics);
        }
    }

    diagnostics.framesProcessed = frameCount;
    diagnostics.runtimeSceneRevision = runtimeRevision;
    diagnostics.usedRuntimeScene = usesRuntimeScene;
    diagnostics.wetBypass = !std::isfinite(settings.wetMix) || settings.wetMix <= 0.0f;
    diagnostics.geometryDisabled = !m_sceneSnapshot.weather.geometryEnabled;
    rwwa::runtime::PublishEffectBlockDiagnostics(diagnostics);
    m_pParams->m_paramChangeHandler.ResetAllParamChanges();
}

AKRESULT RealWorldWeatherAcousticsEffect::TimeSkip(AkUInt32 in_uFrames)
{
    if (m_pParams == nullptr || m_pProcessor == nullptr)
    {
        return AK_DataReady;
    }

    constexpr AkUInt32 kTimeSkipBlockSize = 256u;
    std::array<AkReal32, kTimeSkipBlockSize> silence{};
    AkReal32* channelBuffers[] = {silence.data()};
    bool usesRuntimeScene = false;
    std::uint64_t runtimeRevision = 0u;
    m_sceneSnapshot = BuildSceneSnapshot(usesRuntimeScene, runtimeRevision);
    const rwwa::InteractionSettings settings = BuildInteractionSettings();

    while (in_uFrames > 0)
    {
        const AkUInt32 framesThisBlock = (std::min)(in_uFrames, kTimeSkipBlockSize);
        std::fill_n(silence.data(), framesThisBlock, 0.0f);
        m_pProcessor->Process(
            m_sceneSnapshot,
            settings,
            channelBuffers,
            1u,
            framesThisBlock);
        in_uFrames -= framesThisBlock;
    }

    m_pParams->m_paramChangeHandler.ResetAllParamChanges();
    return AK_DataReady;
}

rwwa::SceneSnapshot RealWorldWeatherAcousticsEffect::BuildSceneSnapshot(
    bool& out_usesRuntimeScene,
    std::uint64_t& out_runtimeRevision) noexcept
{
    out_usesRuntimeScene = false;
    out_runtimeRevision = 0u;
    if (m_pParams == nullptr)
    {
        rwwa::SceneSnapshot empty{};
        empty.weather.geometryEnabled = false;
        return empty;
    }

    const RealWorldWeatherAcousticsEffectParameterValues& values = m_pParams->Values;
    rwwa::ListenerState listener{};
    listener.position = {values.fListenerX, values.fListenerY, values.fListenerZ};
    listener.yawRadians = values.fListenerYawDegrees * 0.01745329251994329577f;

    rwwa::WeatherState weather{};
    weather.rainIntensity = values.fRainIntensity;
    weather.windSpeedMetersPerSecond = values.fWindSpeed;
    weather.windDirectionRadians = values.fWindDirectionDegrees * 0.01745329251994329577f;
    weather.windGustiness = values.fWindGustiness;
    weather.seed = static_cast<AkUInt32>(values.iSeed);
    weather.geometryEnabled = values.bGeometryEnabled;
    weather.masterGainLinear = 1.0f;

    rwwa::SceneInput scene{};
    scene.featureCount = static_cast<AkUInt32>(values.iFeatureCount);
    for (AkUInt32 slot = 0; slot < scene.featureCount; ++slot)
    {
        const RealWorldWeatherAcousticsEffectFeatureParams& source = values.Features[slot];
        rwwa::SphereFeature& target = scene.features[slot];
        target.id = static_cast<std::uint64_t>(slot + 1u);
        target.position = {source.fX, source.fY, source.fZ};
        target.radius = source.fRadius;
        target.profileId = static_cast<std::uint32_t>(source.iProfile);
        target.responseMask = static_cast<std::uint32_t>(source.iMask);
        target.priority = static_cast<std::int32_t>(source.iPriority);
    }

    const rwwa::runtime::CompiledSceneSelection selection =
        rwwa::runtime::CompileSceneWithRuntimeOverride(
            scene,
            listener,
            weather,
            m_retainedRuntimeScene,
            m_retainedRuntimeRevision,
            m_runtimeClaimed,
            m_hasRetainedRuntimeScene);
    out_usesRuntimeScene = selection.usesRuntimeScene;
    out_runtimeRevision = selection.runtimeRevision;
    return selection.snapshot;
}

rwwa::InteractionSettings RealWorldWeatherAcousticsEffect::BuildInteractionSettings() const noexcept
{
    rwwa::InteractionSettings settings{};
    if (m_pParams == nullptr)
    {
        return settings;
    }

    const RealWorldWeatherAcousticsEffectParameterValues& values = m_pParams->Values;
    switch (values.iInputRole)
    {
    case 0: settings.inputRole = rwwa::InputRole::RainBed; break;
    case 1: settings.inputRole = rwwa::InputRole::WindBed; break;
    default: settings.inputRole = rwwa::InputRole::Generic; break;
    }
    settings.wetMix = values.fWetMix;
    settings.responseGainLinear = AK_DBTOLIN(values.fResponseGainDb);
    settings.transientSensitivity = values.fTransientSensitivity;
    return settings;
}
