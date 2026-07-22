#ifndef RealWorldWeatherAcousticsEffect_H
#define RealWorldWeatherAcousticsEffect_H

#include "RealWorldWeatherAcousticsEffectParams.h"
#include "rwwa/WeatherAcousticsCore.h"

class RealWorldWeatherAcousticsEffect final : public AK::IAkInPlaceEffectPlugin
{
public:
    RealWorldWeatherAcousticsEffect();
    ~RealWorldWeatherAcousticsEffect();

    AKRESULT Init(
        AK::IAkPluginMemAlloc* in_pAllocator,
        AK::IAkEffectPluginContext* in_pEffectPluginContext,
        AK::IAkPluginParam* in_pParams,
        AkAudioFormat& io_rFormat) override;
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;
    AKRESULT Reset() override;
    AKRESULT GetPluginInfo(AkPluginInfo& out_rPluginInfo) override;
    void Execute(AkAudioBuffer* io_pBuffer) override;
    AKRESULT TimeSkip(AkUInt32 in_uFrames) override;

private:
    rwwa::SceneSnapshot BuildSceneSnapshot(
        bool& out_usesRuntimeScene,
        std::uint64_t& out_runtimeRevision) noexcept;
    rwwa::InteractionSettings BuildInteractionSettings() const noexcept;

    RealWorldWeatherAcousticsEffectParams* m_pParams;
    rwwa::GeometryInteractionProcessor* m_pProcessor;
    AkReal32* m_pInputScratch;
    AkUInt32 m_scratchFrameCapacity;
    AkUInt32 m_scratchChannelCapacity;
    rwwa::SceneSnapshot m_sceneSnapshot;
    rwwa::SceneSnapshot m_retainedRuntimeScene;
    std::uint64_t m_retainedRuntimeRevision;
    bool m_runtimeClaimed;
    bool m_hasRetainedRuntimeScene;
};

#endif // RealWorldWeatherAcousticsEffect_H
