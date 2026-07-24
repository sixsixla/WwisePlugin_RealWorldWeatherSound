#ifndef RealWorldWeatherAcousticsRuntimeAPI_H
#define RealWorldWeatherAcousticsRuntimeAPI_H

#include <stdint.h>

#define RWWA_RUNTIME_SCENE_ABI_VERSION 1u
#define RWWA_RUNTIME_SCENE_MAX_FEATURES 8u
#define RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION 1u

#define RWWA_RUNTIME_PROFILE_METAL 0u
#define RWWA_RUNTIME_PROFILE_WOOD 1u
#define RWWA_RUNTIME_PROFILE_GLASS 2u
#define RWWA_RUNTIME_PROFILE_TILE 3u
#define RWWA_RUNTIME_PROFILE_PLASTIC 4u
#define RWWA_RUNTIME_PROFILE_MAX RWWA_RUNTIME_PROFILE_PLASTIC

#if defined(_WIN32)
#if defined(RWWA_RUNTIME_API_EXPORTS)
#define RWWA_RUNTIME_API __declspec(dllexport)
#else
#define RWWA_RUNTIME_API
#endif
#define RWWA_RUNTIME_CALL __cdecl
#elif defined(__GNUC__) && defined(RWWA_RUNTIME_API_EXPORTS)
#define RWWA_RUNTIME_API __attribute__((visibility("default")))
#define RWWA_RUNTIME_CALL
#else
#define RWWA_RUNTIME_API
#define RWWA_RUNTIME_CALL
#endif

typedef int32_t RWWA_RuntimeStatus;

#define RWWA_RUNTIME_STATUS_OK ((RWWA_RuntimeStatus)0)
#define RWWA_RUNTIME_STATUS_CLAMPED ((RWWA_RuntimeStatus)1)
#define RWWA_RUNTIME_STATUS_NULL_ARGUMENT ((RWWA_RuntimeStatus)-1)
#define RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI ((RWWA_RuntimeStatus)-2)
#define RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL ((RWWA_RuntimeStatus)-3)
#define RWWA_RUNTIME_STATUS_OUT_OF_RANGE ((RWWA_RuntimeStatus)-4)
#define RWWA_RUNTIME_STATUS_NO_SCENE ((RWWA_RuntimeStatus)-5)
#define RWWA_RUNTIME_STATUS_BUSY ((RWWA_RuntimeStatus)-6)
#define RWWA_RUNTIME_STATUS_INVALID_ARGUMENT ((RWWA_RuntimeStatus)-7)
#define RWWA_RUNTIME_STATUS_UNCLAIMED ((RWWA_RuntimeStatus)-8)

#pragma pack(push, 8)

typedef struct RWWA_RuntimeFeatureV1
{
    uint64_t id;
    float x;
    float y;
    float z;
    float radius;
    uint32_t profile;
    uint32_t mask;
    int32_t priority;
    uint32_t reserved0;
} RWWA_RuntimeFeatureV1;

typedef struct RWWA_RuntimeSceneV1
{
    uint32_t abiVersion;
    uint32_t structSize;
    uint64_t revision;
    uint32_t valid;
    uint32_t geometryEnabled;

    float listenerX;
    float listenerY;
    float listenerZ;
    float listenerYawRadians;

    float rainIntensity;
    float windSpeedMetersPerSecond;
    float windDirectionRadians;
    float windGustiness;
    uint32_t weatherSeed;
    float weatherMasterGainLinear;

    uint32_t featureCount;
    uint32_t reserved0;
    RWWA_RuntimeFeatureV1 features[RWWA_RUNTIME_SCENE_MAX_FEATURES];
} RWWA_RuntimeSceneV1;

typedef struct RWWA_RuntimeDiagnosticsV1
{
    uint32_t abiVersion;
    uint32_t structSize;

    uint64_t effectExecuteCount;
    uint64_t framesProcessed;
    uint64_t runtimeSceneBlockCount;
    uint64_t authoredFallbackBlockCount;
    uint64_t wetBypassBlockCount;
    uint64_t geometryDisabledBlockCount;
    /* These five last-block fields form one coherent tuple:
       lastRuntimeSceneRevision, lastInputPeak, lastOutputPeak,
       lastWetDifferencePeak, and lastBlockUsedRuntimeScene. With concurrent
       publishers the tuple is from the most recent successful no-wait
       try-commit, so it may lag the counters/maxima but never mixes blocks. */
    uint64_t lastRuntimeSceneRevision;

    float lastInputPeak;
    float maxInputPeak;
    float lastOutputPeak;
    float maxOutputPeak;
    float lastWetDifferencePeak;
    float maxWetDifferencePeak;
    uint32_t lastBlockUsedRuntimeScene;
    uint32_t nonFiniteSampleCount;
} RWWA_RuntimeDiagnosticsV1;

#pragma pack(pop)

#if defined(__cplusplus) && defined(RWWA_RUNTIME_API_INTERNAL)
#include "rwwa/WeatherAcousticsCore.h"

namespace rwwa::runtime
{
struct CompiledSceneSelection
{
    SceneSnapshot snapshot{};
    std::uint64_t runtimeRevision = 0u;
    bool usesRuntimeScene = false;
};

CompiledSceneSelection CompileSceneWithRuntimeOverride(
    const SceneInput& fallbackScene,
    const ListenerState& fallbackListener,
    const WeatherState& fallbackWeather,
    SceneSnapshot& retainedRuntimeScene,
    std::uint64_t& retainedRuntimeRevision,
    bool& runtimeClaimed,
    bool& hasRetainedRuntimeScene) noexcept;

struct EffectBlockDiagnostics
{
    std::uint32_t framesProcessed = 0u;
    std::uint64_t runtimeSceneRevision = 0u;
    bool usedRuntimeScene = false;
    bool wetBypass = false;
    bool geometryDisabled = false;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float wetDifferencePeak = 0.0f;
    std::uint32_t nonFiniteSampleCount = 0u;
};

void AccumulateEffectSampleDiagnostics(
    float inputSample,
    float outputSample,
    EffectBlockDiagnostics& block) noexcept;

// Called from the audio thread. Uses lock-free atomics only.
void PublishEffectBlockDiagnostics(const EffectBlockDiagnostics& block) noexcept;

#if defined(RWWA_RUNTIME_TESTING)
using RuntimeTestHook = void (*)() noexcept;
void SetSceneWriteTestHook(RuntimeTestHook hook) noexcept;
void SetDiagnosticsPublishTestHook(RuntimeTestHook hook) noexcept;
void SetDiagnosticsGetSnapshotTestHook(RuntimeTestHook hook) noexcept;
void SetDiagnosticsMidTupleTestHook(RuntimeTestHook hook) noexcept;
#endif
} // namespace rwwa::runtime
#endif

#ifdef __cplusplus
extern "C"
{
#endif

RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_SetV1(const RWWA_RuntimeSceneV1* scene);

RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_ClearV1(void);

RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_GetV1(RWWA_RuntimeSceneV1* outScene);

// Diagnostics Reset/Get are control-thread APIs. If they overlap a publish or
// reset they return BUSY; callers may retry.
RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeDiagnostics_ResetV1(void);

RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeDiagnostics_GetV1(RWWA_RuntimeDiagnosticsV1* outDiagnostics);

#ifdef __cplusplus
}
#endif

#endif // RealWorldWeatherAcousticsRuntimeAPI_H
