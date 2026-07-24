#define RWWA_RUNTIME_API_INTERNAL
#include "RealWorldWeatherAcousticsRuntimeAPI.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>

namespace
{
int g_failureCount = 0;
std::atomic<bool> g_sceneWriteHookEntered{false};
std::atomic<bool> g_releaseSceneWriteHook{false};
std::atomic<bool> g_diagnosticsPublishHookEntered{false};
std::atomic<bool> g_releaseDiagnosticsPublishHook{false};
std::atomic<bool> g_diagnosticsGetSnapshotHookEntered{false};
std::atomic<bool> g_releaseDiagnosticsGetSnapshotHook{false};
std::atomic<bool> g_diagnosticsMidTupleHookEntered{false};
std::atomic<bool> g_releaseDiagnosticsMidTupleHook{false};

void SceneWriteHook() noexcept
{
    g_sceneWriteHookEntered.store(true, std::memory_order_release);
    while (!g_releaseSceneWriteHook.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

void DiagnosticsPublishHook() noexcept
{
    g_diagnosticsPublishHookEntered.store(true, std::memory_order_release);
    while (!g_releaseDiagnosticsPublishHook.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

void DiagnosticsGetSnapshotHook() noexcept
{
    g_diagnosticsGetSnapshotHookEntered.store(true, std::memory_order_release);
    while (!g_releaseDiagnosticsGetSnapshotHook.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

void DiagnosticsMidTupleHook() noexcept
{
    g_diagnosticsMidTupleHookEntered.store(true, std::memory_order_release);
    while (!g_releaseDiagnosticsMidTupleHook.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

void WaitUntil(const std::atomic<bool>& value)
{
    while (!value.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}

void Expect(bool condition, const char* expression, const char* file, int line)
{
    if (!condition)
    {
        ++g_failureCount;
        std::cerr << file << ':' << line << ": EXPECT failed: " << expression << '\n';
    }
}

#define EXPECT(expression) Expect((expression), #expression, __FILE__, __LINE__)

static_assert(std::is_standard_layout_v<RWWA_RuntimeFeatureV1>);
static_assert(std::is_standard_layout_v<RWWA_RuntimeSceneV1>);
static_assert(sizeof(RWWA_RuntimeFeatureV1) == 40u);
static_assert(offsetof(RWWA_RuntimeFeatureV1, id) == 0u);
static_assert(offsetof(RWWA_RuntimeFeatureV1, x) == 8u);
static_assert(offsetof(RWWA_RuntimeFeatureV1, priority) == 32u);
static_assert(offsetof(RWWA_RuntimeFeatureV1, reserved0) == 36u);
static_assert(sizeof(RWWA_RuntimeSceneV1) == 392u);
static_assert(offsetof(RWWA_RuntimeSceneV1, revision) == 8u);
static_assert(offsetof(RWWA_RuntimeSceneV1, valid) == 16u);
static_assert(offsetof(RWWA_RuntimeSceneV1, listenerX) == 24u);
static_assert(offsetof(RWWA_RuntimeSceneV1, rainIntensity) == 40u);
static_assert(offsetof(RWWA_RuntimeSceneV1, featureCount) == 64u);
static_assert(offsetof(RWWA_RuntimeSceneV1, features) == 72u);
static_assert(std::is_standard_layout_v<RWWA_RuntimeDiagnosticsV1>);
static_assert(sizeof(RWWA_RuntimeDiagnosticsV1) == 96u);
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, effectExecuteCount) == 8u);
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, lastRuntimeSceneRevision) == 56u);
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, lastInputPeak) == 64u);
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, lastBlockUsedRuntimeScene) == 88u);
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, nonFiniteSampleCount) == 92u);

RWWA_RuntimeSceneV1 MakeScene(std::uint64_t revision, std::uint32_t featureCount = 1u)
{
    RWWA_RuntimeSceneV1 scene{};
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    scene.structSize = static_cast<std::uint32_t>(sizeof(scene));
    scene.revision = revision;
    scene.valid = 1u;
    scene.geometryEnabled = 1u;
    scene.listenerX = 1.0f;
    scene.listenerY = 2.0f;
    scene.listenerZ = 3.0f;
    scene.listenerYawRadians = 0.25f;
    scene.rainIntensity = 0.75f;
    scene.windSpeedMetersPerSecond = 12.0f;
    scene.windDirectionRadians = -0.5f;
    scene.windGustiness = 0.4f;
    scene.weatherSeed = static_cast<std::uint32_t>(revision);
    scene.weatherMasterGainLinear = 1.0f;
    scene.featureCount = featureCount;
    for (std::uint32_t index = 0u; index < featureCount && index < 8u; ++index)
    {
        RWWA_RuntimeFeatureV1& feature = scene.features[index];
        feature.id = revision * 100u + index;
        feature.x = static_cast<float>(index + 1u);
        feature.y = 0.0f;
        feature.z = static_cast<float>(index + 2u);
        feature.radius = static_cast<float>(index + 1u);
        feature.profile = index % (RWWA_RUNTIME_PROFILE_MAX + 1u);
        feature.mask = 3u;
        feature.priority = static_cast<std::int32_t>(index);
    }
    return scene;
}

RWWA_RuntimeSceneV1 MakeGetBuffer()
{
    RWWA_RuntimeSceneV1 scene{};
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    scene.structSize = static_cast<std::uint32_t>(sizeof(scene));
    return scene;
}

RWWA_RuntimeDiagnosticsV1 MakeDiagnosticsGetBuffer()
{
    RWWA_RuntimeDiagnosticsV1 diagnostics{};
    diagnostics.abiVersion = RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION;
    diagnostics.structSize = static_cast<std::uint32_t>(sizeof(diagnostics));
    return diagnostics;
}

void ClearEventually()
{
    while (RWWA_RuntimeScene_ClearV1() == RWWA_RUNTIME_STATUS_BUSY)
    {
    }
}

void SetEventually(const RWWA_RuntimeSceneV1& scene)
{
    RWWA_RuntimeStatus status = RWWA_RUNTIME_STATUS_BUSY;
    while (status == RWWA_RUNTIME_STATUS_BUSY)
    {
        status = RWWA_RuntimeScene_SetV1(&scene);
    }
    EXPECT(status == RWWA_RUNTIME_STATUS_OK || status == RWWA_RUNTIME_STATUS_CLAMPED);
}

void TestAbiValidationAndLimits()
{
    EXPECT(RWWA_RuntimeScene_SetV1(nullptr) == RWWA_RUNTIME_STATUS_NULL_ARGUMENT);
    EXPECT(RWWA_RuntimeScene_GetV1(nullptr) == RWWA_RUNTIME_STATUS_NULL_ARGUMENT);

    RWWA_RuntimeSceneV1 scene = MakeScene(1u);
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION + 1u;
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI);
    scene = MakeScene(1u);
    scene.structSize = static_cast<std::uint32_t>(sizeof(scene) - 1u);
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL);
    scene = MakeScene(1u);
    scene.valid = 0u;
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_INVALID_ARGUMENT);
    scene = MakeScene(1u);
    scene.featureCount = RWWA_RUNTIME_SCENE_MAX_FEATURES + 1u;
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_OUT_OF_RANGE);

    RWWA_RuntimeSceneV1 output = MakeGetBuffer();
    output.abiVersion = 0u;
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI);
    output = MakeGetBuffer();
    output.structSize = static_cast<std::uint32_t>(sizeof(output) - 1u);
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL);

    scene = MakeScene(8u, RWWA_RUNTIME_SCENE_MAX_FEATURES);
    scene.reserved0 = 0xffffffffu;
    for (RWWA_RuntimeFeatureV1& feature : scene.features)
    {
        feature.reserved0 = 0xffffffffu;
    }
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_OK);
    output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(output.abiVersion == RWWA_RUNTIME_SCENE_ABI_VERSION);
    EXPECT(output.structSize == sizeof(output));
    EXPECT(output.revision == scene.revision);
    EXPECT(output.valid == 1u);
    EXPECT(output.geometryEnabled == scene.geometryEnabled);
    EXPECT(output.listenerX == scene.listenerX);
    EXPECT(output.listenerY == scene.listenerY);
    EXPECT(output.listenerZ == scene.listenerZ);
    EXPECT(output.listenerYawRadians == scene.listenerYawRadians);
    EXPECT(output.rainIntensity == scene.rainIntensity);
    EXPECT(output.windSpeedMetersPerSecond == scene.windSpeedMetersPerSecond);
    EXPECT(output.windDirectionRadians == scene.windDirectionRadians);
    EXPECT(output.windGustiness == scene.windGustiness);
    EXPECT(output.weatherSeed == scene.weatherSeed);
    EXPECT(output.weatherMasterGainLinear == scene.weatherMasterGainLinear);
    EXPECT(output.featureCount == RWWA_RUNTIME_SCENE_MAX_FEATURES);
    EXPECT(output.reserved0 == 0u);
    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        EXPECT(output.features[index].id == scene.features[index].id);
        EXPECT(output.features[index].x == scene.features[index].x);
        EXPECT(output.features[index].y == scene.features[index].y);
        EXPECT(output.features[index].z == scene.features[index].z);
        EXPECT(output.features[index].radius == scene.features[index].radius);
        EXPECT(output.features[index].profile == scene.features[index].profile);
        EXPECT(output.features[index].mask == scene.features[index].mask);
        EXPECT(output.features[index].priority == scene.features[index].priority);
        EXPECT(output.features[index].reserved0 == 0u);
    }
}

void TestSetReadClear()
{
    ClearEventually();
    RWWA_RuntimeSceneV1 output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_NO_SCENE);
    EXPECT(output.valid == 0u);

    RWWA_RuntimeSceneV1 input = MakeScene(0x123456789abcdef0ull, 2u);
    input.reserved0 = 99u;
    input.features[0].reserved0 = 88u;
    input.features[7] = {999u, {}};
    input.features[7].x = 9.0f;
    input.features[7].radius = 9.0f;
    EXPECT(RWWA_RuntimeScene_SetV1(&input) == RWWA_RUNTIME_STATUS_OK);
    output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(output.abiVersion == RWWA_RUNTIME_SCENE_ABI_VERSION);
    EXPECT(output.structSize == sizeof(output));
    EXPECT(output.revision == input.revision);
    EXPECT(output.valid == 1u);
    EXPECT(output.listenerX == input.listenerX);
    EXPECT(output.weatherSeed == input.weatherSeed);
    EXPECT(output.featureCount == input.featureCount);
    EXPECT(output.features[1].id == input.features[1].id);
    EXPECT(output.reserved0 == 0u);
    EXPECT(output.features[0].reserved0 == 0u);
    for (std::uint32_t index = input.featureCount;
         index < RWWA_RUNTIME_SCENE_MAX_FEATURES;
         ++index)
    {
        EXPECT(output.features[index].id == 0u);
        EXPECT(output.features[index].x == 0.0f);
        EXPECT(output.features[index].y == 0.0f);
        EXPECT(output.features[index].z == 0.0f);
        EXPECT(output.features[index].radius == 0.0f);
        EXPECT(output.features[index].profile == 0u);
        EXPECT(output.features[index].mask == 0u);
        EXPECT(output.features[index].priority == 0);
        EXPECT(output.features[index].reserved0 == 0u);
    }

    ClearEventually();
    output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_NO_SCENE);
    EXPECT(output.valid == 0u);
    EXPECT(output.geometryEnabled == 0u);
    EXPECT(output.featureCount == 0u);
    EXPECT(output.revision == 0u);
}

void TestSanitization()
{
    RWWA_RuntimeSceneV1 scene = MakeScene(17u, 1u);
    scene.valid = 9u;
    scene.geometryEnabled = 4u;
    scene.listenerX = std::numeric_limits<float>::infinity();
    scene.listenerY = -2.0e6f;
    scene.listenerYawRadians = 100.0f;
    scene.rainIntensity = -1.0f;
    scene.windSpeedMetersPerSecond = 100.0f;
    scene.windDirectionRadians = std::numeric_limits<float>::quiet_NaN();
    scene.windGustiness = 2.0f;
    scene.weatherMasterGainLinear = std::numeric_limits<float>::infinity();
    scene.features[0].x = std::numeric_limits<float>::quiet_NaN();
    scene.features[0].radius = -2.0f;
    scene.features[0].profile = 99u;
    scene.features[0].mask = 0xffffu;
    scene.features[0].priority = 2000;
    EXPECT(RWWA_RuntimeScene_SetV1(&scene) == RWWA_RUNTIME_STATUS_CLAMPED);

    RWWA_RuntimeSceneV1 output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(output.valid == 1u);
    EXPECT(output.geometryEnabled == 1u);
    EXPECT(output.listenerX == 0.0f);
    EXPECT(output.listenerY == -1.0e6f);
    EXPECT(output.listenerYawRadians >= -3.141593f);
    EXPECT(output.listenerYawRadians <= 3.141593f);
    EXPECT(output.rainIntensity == 0.0f);
    EXPECT(output.windSpeedMetersPerSecond == 60.0f);
    EXPECT(output.windDirectionRadians == 0.0f);
    EXPECT(output.windGustiness == 1.0f);
    EXPECT(output.weatherMasterGainLinear == 0.0f);
    EXPECT(output.features[0].x == 0.0f);
    EXPECT(output.features[0].radius == 0.01f);
    EXPECT(output.features[0].profile == 0u);
    EXPECT(output.features[0].mask == 3u);
    EXPECT(output.features[0].priority == 1000);
}

void TestConcurrentConsistency()
{
    constexpr std::uint64_t kRevisionBase = 0x1234000000000000ull;
    SetEventually(MakeScene(kRevisionBase + 1u, RWWA_RUNTIME_SCENE_MAX_FEATURES));
    std::atomic<bool> writerDone{false};
    std::atomic<int> inconsistentReads{0};

    std::thread writer([&writerDone, kRevisionBase]()
    {
        for (std::uint64_t offset = 2u; offset <= 50000u; ++offset)
        {
            const std::uint64_t revision = kRevisionBase + offset;
            SetEventually(MakeScene(revision, RWWA_RUNTIME_SCENE_MAX_FEATURES));
        }
        writerDone.store(true, std::memory_order_release);
    });

    std::thread reader([&writerDone, &inconsistentReads]()
    {
        do
        {
            RWWA_RuntimeSceneV1 output = MakeGetBuffer();
            const RWWA_RuntimeStatus status = RWWA_RuntimeScene_GetV1(&output);
            if (status == RWWA_RUNTIME_STATUS_BUSY)
            {
                continue;
            }
            if (status != RWWA_RUNTIME_STATUS_OK ||
                output.valid != 1u ||
                output.featureCount != RWWA_RUNTIME_SCENE_MAX_FEATURES ||
                output.weatherSeed != static_cast<std::uint32_t>(output.revision) ||
                output.features[0].id != output.revision * 100u ||
                output.features[7].id != output.revision * 100u + 7u)
            {
                inconsistentReads.fetch_add(1, std::memory_order_relaxed);
            }
        } while (!writerDone.load(std::memory_order_acquire));
    });

    writer.join();
    reader.join();
    EXPECT(inconsistentReads.load(std::memory_order_relaxed) == 0);
}

void TestFirstClaimTransitionNeverFallsBack()
{
    rwwa::SceneInput fallbackScene{};
    fallbackScene.featureCount = 1u;
    fallbackScene.features[0] = {111u, {0.0f, 0.0f, 2.0f}, 1.0f, 0u, 3u, 0};
    rwwa::WeatherState fallbackWeather{};
    fallbackWeather.rainIntensity = 0.8f;
    fallbackWeather.geometryEnabled = true;
    fallbackWeather.masterGainLinear = 1.0f;

    RWWA_RuntimeSceneV1 output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_UNCLAIMED);

    rwwa::SceneSnapshot retainedRuntimeScene{};
    std::uint64_t retainedRuntimeRevision = 0u;
    bool runtimeClaimed = false;
    bool hasRetainedRuntimeScene = false;
    rwwa::runtime::CompiledSceneSelection selected =
        rwwa::runtime::CompileSceneWithRuntimeOverride(
            fallbackScene,
            {},
            fallbackWeather,
            retainedRuntimeScene,
            retainedRuntimeRevision,
            runtimeClaimed,
            hasRetainedRuntimeScene);
    EXPECT(!selected.usesRuntimeScene);
    EXPECT(selected.snapshot.contributionCount == 1u);
    EXPECT(selected.snapshot.contributions[0].featureId == 111u);

    RWWA_RuntimeSceneV1 firstRuntimeScene = MakeScene(5u);
    g_sceneWriteHookEntered.store(false, std::memory_order_relaxed);
    g_releaseSceneWriteHook.store(false, std::memory_order_relaxed);
    rwwa::runtime::SetSceneWriteTestHook(SceneWriteHook);
    std::atomic<RWWA_RuntimeStatus> setStatus{RWWA_RUNTIME_STATUS_BUSY};
    std::thread writer([&]()
    {
        setStatus.store(
            RWWA_RuntimeScene_SetV1(&firstRuntimeScene),
            std::memory_order_release);
    });
    WaitUntil(g_sceneWriteHookEntered);

    output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_BUSY);
    selected = rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);
    EXPECT(selected.usesRuntimeScene);
    EXPECT(selected.snapshot.contributionCount == 0u);
    EXPECT(!selected.snapshot.weather.geometryEnabled);

    g_releaseSceneWriteHook.store(true, std::memory_order_release);
    writer.join();
    rwwa::runtime::SetSceneWriteTestHook(nullptr);
    EXPECT(setStatus.load(std::memory_order_acquire) == RWWA_RUNTIME_STATUS_OK);

    output = MakeGetBuffer();
    EXPECT(RWWA_RuntimeScene_GetV1(&output) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(output.revision == firstRuntimeScene.revision);
    selected = rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);
    EXPECT(selected.usesRuntimeScene);
    EXPECT(selected.runtimeRevision == firstRuntimeScene.revision);
    EXPECT(selected.snapshot.contributionCount == 1u);
    EXPECT(selected.snapshot.contributions[0].featureId == firstRuntimeScene.features[0].id);
}

void TestRuntimeOverrideAndFallbackSelection()
{
    rwwa::SceneInput fallbackScene{};
    fallbackScene.featureCount = 1u;
    fallbackScene.features[0] = {111u, {0.0f, 0.0f, 2.0f}, 1.0f, 0u, 3u, 0};
    rwwa::WeatherState fallbackWeather{};
    fallbackWeather.rainIntensity = 0.8f;
    fallbackWeather.geometryEnabled = true;
    fallbackWeather.masterGainLinear = 1.0f;

    rwwa::SceneSnapshot retainedRuntimeScene{};
    std::uint64_t retainedRuntimeRevision = 0u;
    bool runtimeClaimed = false;
    bool hasRetainedRuntimeScene = false;
    rwwa::runtime::CompiledSceneSelection selected{};

    ClearEventually();
    selected = rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);
    EXPECT(selected.usesRuntimeScene);
    EXPECT(selected.snapshot.contributionCount == 0u);
    EXPECT(!selected.snapshot.weather.geometryEnabled);

    RWWA_RuntimeSceneV1 runtime = MakeScene(7u);
    runtime.listenerX = 0.0f;
    runtime.listenerY = 0.0f;
    runtime.listenerZ = 0.0f;
    runtime.rainIntensity = 1.0f;
    runtime.windSpeedMetersPerSecond = 0.0f;
    runtime.features[0].id = 222u;
    runtime.features[0].x = 0.0f;
    runtime.features[0].y = 0.0f;
    runtime.features[0].z = 1.0f;
    SetEventually(runtime);
    selected = rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);
    EXPECT(selected.usesRuntimeScene);
    EXPECT(selected.runtimeRevision == 7u);
    EXPECT(selected.snapshot.contributionCount == 1u);
    EXPECT(selected.snapshot.contributions[0].featureId == 222u);

    ClearEventually();
    selected = rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);
    EXPECT(selected.usesRuntimeScene);
    EXPECT(selected.runtimeRevision == 0u);
    EXPECT(selected.snapshot.contributionCount == 0u);
    EXPECT(!selected.snapshot.weather.geometryEnabled);
}

void TestClaimedConcurrencyNeverFallsBack()
{
    rwwa::SceneInput fallbackScene{};
    fallbackScene.featureCount = 1u;
    fallbackScene.features[0] = {111u, {0.0f, 0.0f, 2.0f}, 1.0f, 0u, 3u, 0};
    rwwa::WeatherState fallbackWeather{};
    fallbackWeather.rainIntensity = 1.0f;
    fallbackWeather.geometryEnabled = true;
    fallbackWeather.masterGainLinear = 1.0f;

    SetEventually(MakeScene(1u));
    rwwa::SceneSnapshot retainedRuntimeScene{};
    std::uint64_t retainedRuntimeRevision = 0u;
    bool runtimeClaimed = false;
    bool hasRetainedRuntimeScene = false;
    rwwa::runtime::CompileSceneWithRuntimeOverride(
        fallbackScene,
        {},
        fallbackWeather,
        retainedRuntimeScene,
        retainedRuntimeRevision,
        runtimeClaimed,
        hasRetainedRuntimeScene);

    std::atomic<bool> writerDone{false};
    std::atomic<int> fallbackSelections{0};
    std::thread writer([&writerDone]()
    {
        for (std::uint64_t revision = 2u; revision <= 50000u; ++revision)
        {
            SetEventually(MakeScene(revision));
        }
        writerDone.store(true, std::memory_order_release);
    });
    std::thread reader([&]()
    {
        do
        {
            const rwwa::runtime::CompiledSceneSelection selected =
                rwwa::runtime::CompileSceneWithRuntimeOverride(
                    fallbackScene,
                    {},
                    fallbackWeather,
                    retainedRuntimeScene,
                    retainedRuntimeRevision,
                    runtimeClaimed,
                    hasRetainedRuntimeScene);
            if (!selected.usesRuntimeScene ||
                (selected.snapshot.contributionCount != 0u &&
                 selected.snapshot.contributions[0].featureId == 111u))
            {
                fallbackSelections.fetch_add(1, std::memory_order_relaxed);
            }
        } while (!writerDone.load(std::memory_order_acquire));
    });
    writer.join();
    reader.join();
    EXPECT(fallbackSelections.load(std::memory_order_relaxed) == 0);
}

void TestDiagnosticsAbiAndBehavior()
{
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(nullptr) == RWWA_RUNTIME_STATUS_NULL_ARGUMENT);
    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    diagnostics.abiVersion = 0u;
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) ==
        RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI);
    diagnostics = MakeDiagnosticsGetBuffer();
    diagnostics.structSize = static_cast<std::uint32_t>(sizeof(diagnostics) - 1u);
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) ==
        RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL);

    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 0u);
    EXPECT(diagnostics.framesProcessed == 0u);

    rwwa::runtime::EffectBlockDiagnostics authored{};
    authored.framesProcessed = 128u;
    authored.wetBypass = true;
    authored.inputPeak = 0.3f;
    authored.outputPeak = 0.3f;
    authored.wetDifferencePeak = 0.7f;
    authored.nonFiniteSampleCount = 3u;
    rwwa::runtime::PublishEffectBlockDiagnostics(authored);
    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.lastWetDifferencePeak == 0.7f);
    EXPECT(diagnostics.maxWetDifferencePeak == 0.7f);

    rwwa::runtime::EffectBlockDiagnostics disabled{};
    disabled.framesProcessed = 64u;
    disabled.runtimeSceneRevision = 99u;
    disabled.usedRuntimeScene = true;
    disabled.geometryDisabled = true;
    disabled.inputPeak = 0.4f;
    disabled.outputPeak = 0.2f;
    disabled.wetDifferencePeak = 0.8f;
    disabled.nonFiniteSampleCount = 4u;
    rwwa::runtime::PublishEffectBlockDiagnostics(disabled);
    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.lastWetDifferencePeak == 0.8f);
    EXPECT(diagnostics.maxWetDifferencePeak == 0.8f);

    rwwa::runtime::EffectBlockDiagnostics wet{};
    wet.framesProcessed = 32u;
    wet.runtimeSceneRevision = 100u;
    wet.usedRuntimeScene = true;
    wet.inputPeak = 0.5f;
    wet.outputPeak = 0.7f;
    wet.wetDifferencePeak = 0.25f;
    rwwa::runtime::PublishEffectBlockDiagnostics(wet);

    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 3u);
    EXPECT(diagnostics.framesProcessed == 224u);
    EXPECT(diagnostics.runtimeSceneBlockCount == 2u);
    EXPECT(diagnostics.authoredFallbackBlockCount == 1u);
    EXPECT(diagnostics.wetBypassBlockCount == 1u);
    EXPECT(diagnostics.geometryDisabledBlockCount == 1u);
    EXPECT(diagnostics.lastRuntimeSceneRevision == 100u);
    EXPECT(diagnostics.lastInputPeak == 0.5f);
    EXPECT(diagnostics.maxInputPeak == 0.5f);
    EXPECT(diagnostics.lastOutputPeak == 0.7f);
    EXPECT(diagnostics.maxOutputPeak == 0.7f);
    EXPECT(diagnostics.lastWetDifferencePeak == 0.25f);
    EXPECT(diagnostics.maxWetDifferencePeak == 0.8f);
    EXPECT(diagnostics.lastBlockUsedRuntimeScene == 1u);
    EXPECT(diagnostics.nonFiniteSampleCount == 7u);

    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 0u);
    EXPECT(diagnostics.maxWetDifferencePeak == 0.0f);
    EXPECT(diagnostics.nonFiniteSampleCount == 0u);
}

void TestEffectSampleDiagnosticsDetectsNonFiniteValues()
{
    rwwa::runtime::EffectBlockDiagnostics block{};
    rwwa::runtime::AccumulateEffectSampleDiagnostics(0.25f, -0.5f, block);
    EXPECT(block.nonFiniteSampleCount == 0u);
    EXPECT(block.inputPeak == 0.25f);
    EXPECT(block.outputPeak == 0.5f);
    EXPECT(block.wetDifferencePeak == 0.75f);

    rwwa::runtime::AccumulateEffectSampleDiagnostics(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, block);
    rwwa::runtime::AccumulateEffectSampleDiagnostics(
        0.0f, std::numeric_limits<float>::infinity(), block);
    rwwa::runtime::AccumulateEffectSampleDiagnostics(
        (std::numeric_limits<float>::max)(),
        -(std::numeric_limits<float>::max)(),
        block);
    EXPECT(block.nonFiniteSampleCount == 3u);

    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    rwwa::runtime::PublishEffectBlockDiagnostics(block);
    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.nonFiniteSampleCount == 3u);
}

void TestDiagnosticsOverlapReturnsBusyWithoutPartialState()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    rwwa::runtime::EffectBlockDiagnostics block{};
    block.framesProcessed = 64u;
    block.runtimeSceneRevision = 0x123456789abcdef0ull;
    block.usedRuntimeScene = true;
    block.inputPeak = 0.25f;
    block.outputPeak = 0.5f;
    block.wetDifferencePeak = 0.125f;
    block.nonFiniteSampleCount = 2u;

    g_diagnosticsPublishHookEntered.store(false, std::memory_order_relaxed);
    g_releaseDiagnosticsPublishHook.store(false, std::memory_order_relaxed);
    rwwa::runtime::SetDiagnosticsPublishTestHook(DiagnosticsPublishHook);
    std::thread publisher([&block]()
    {
        rwwa::runtime::PublishEffectBlockDiagnostics(block);
    });
    WaitUntil(g_diagnosticsPublishHookEntered);

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_BUSY);
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_BUSY);

    g_releaseDiagnosticsPublishHook.store(true, std::memory_order_release);
    publisher.join();
    rwwa::runtime::SetDiagnosticsPublishTestHook(nullptr);

    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 1u);
    EXPECT(diagnostics.framesProcessed == block.framesProcessed);
    EXPECT(diagnostics.runtimeSceneBlockCount == 1u);
    EXPECT(diagnostics.lastRuntimeSceneRevision == block.runtimeSceneRevision);
    EXPECT(diagnostics.lastInputPeak == block.inputPeak);
    EXPECT(diagnostics.lastOutputPeak == block.outputPeak);
    EXPECT(diagnostics.lastWetDifferencePeak == block.wetDifferencePeak);
    EXPECT(diagnostics.nonFiniteSampleCount == block.nonFiniteSampleCount);

    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 0u);
    EXPECT(diagnostics.framesProcessed == 0u);
    EXPECT(diagnostics.nonFiniteSampleCount == 0u);
}

void TestDiagnosticsCompletedPublishInvalidatesInFlightGet()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    g_diagnosticsGetSnapshotHookEntered.store(false, std::memory_order_relaxed);
    g_releaseDiagnosticsGetSnapshotHook.store(false, std::memory_order_relaxed);
    rwwa::runtime::SetDiagnosticsGetSnapshotTestHook(DiagnosticsGetSnapshotHook);

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    std::atomic<RWWA_RuntimeStatus> getStatus{RWWA_RUNTIME_STATUS_OK};
    std::thread getter([&diagnostics, &getStatus]()
    {
        getStatus.store(
            RWWA_RuntimeDiagnostics_GetV1(&diagnostics),
            std::memory_order_release);
    });
    WaitUntil(g_diagnosticsGetSnapshotHookEntered);

    rwwa::runtime::EffectBlockDiagnostics block{};
    block.framesProcessed = 32u;
    block.usedRuntimeScene = true;
    block.runtimeSceneRevision = 0xfedcba9876543210ull;
    block.inputPeak = 0.25f;
    block.outputPeak = 0.5f;
    block.wetDifferencePeak = 0.25f;
    rwwa::runtime::PublishEffectBlockDiagnostics(block);

    g_releaseDiagnosticsGetSnapshotHook.store(true, std::memory_order_release);
    getter.join();
    rwwa::runtime::SetDiagnosticsGetSnapshotTestHook(nullptr);
    EXPECT(getStatus.load(std::memory_order_acquire) == RWWA_RUNTIME_STATUS_BUSY);

    diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 1u);
    EXPECT(diagnostics.framesProcessed == block.framesProcessed);
    EXPECT(diagnostics.lastRuntimeSceneRevision == block.runtimeSceneRevision);
}

void TestDiagnosticsLastTupleTryCommitIsCoherent()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    g_diagnosticsMidTupleHookEntered.store(false, std::memory_order_relaxed);
    g_releaseDiagnosticsMidTupleHook.store(false, std::memory_order_relaxed);
    rwwa::runtime::SetDiagnosticsMidTupleTestHook(DiagnosticsMidTupleHook);

    rwwa::runtime::EffectBlockDiagnostics blockA{};
    blockA.framesProcessed = 11u;
    blockA.runtimeSceneRevision = 0xaaaaaaaa55555555ull;
    blockA.usedRuntimeScene = true;
    blockA.inputPeak = 0.11f;
    blockA.outputPeak = 0.22f;
    blockA.wetDifferencePeak = 0.33f;

    std::thread publisherA([&blockA]()
    {
        rwwa::runtime::PublishEffectBlockDiagnostics(blockA);
    });
    WaitUntil(g_diagnosticsMidTupleHookEntered);

    rwwa::runtime::EffectBlockDiagnostics blockB{};
    blockB.framesProcessed = 17u;
    blockB.runtimeSceneRevision = 0xbbbbbbbb66666666ull;
    blockB.usedRuntimeScene = false;
    blockB.inputPeak = 0.44f;
    blockB.outputPeak = 0.55f;
    blockB.wetDifferencePeak = 0.66f;
    rwwa::runtime::PublishEffectBlockDiagnostics(blockB);

    g_releaseDiagnosticsMidTupleHook.store(true, std::memory_order_release);
    publisherA.join();
    rwwa::runtime::SetDiagnosticsMidTupleTestHook(nullptr);

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 2u);
    EXPECT(diagnostics.framesProcessed == blockA.framesProcessed + blockB.framesProcessed);
    EXPECT(diagnostics.runtimeSceneBlockCount == 1u);
    EXPECT(diagnostics.authoredFallbackBlockCount == 1u);
    EXPECT(diagnostics.maxInputPeak == blockB.inputPeak);
    EXPECT(diagnostics.maxOutputPeak == blockB.outputPeak);
    EXPECT(diagnostics.maxWetDifferencePeak == blockB.wetDifferencePeak);
    EXPECT(diagnostics.lastRuntimeSceneRevision == blockA.runtimeSceneRevision);
    EXPECT(diagnostics.lastInputPeak == blockA.inputPeak);
    EXPECT(diagnostics.lastOutputPeak == blockA.outputPeak);
    EXPECT(diagnostics.lastWetDifferencePeak == blockA.wetDifferencePeak);
    EXPECT(diagnostics.lastBlockUsedRuntimeScene == 1u);
}

void TestDiagnosticsLastTupleMultiWriterEncoding()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    constexpr std::uint32_t kPublisherCount = 4u;
    constexpr std::uint32_t kBlocksPerPublisher = 10000u;
    std::atomic<bool> start{false};
    std::thread publishers[kPublisherCount];
    for (std::uint32_t publisherIndex = 0u;
         publisherIndex < kPublisherCount;
         ++publisherIndex)
    {
        publishers[publisherIndex] = std::thread([publisherIndex, &start]()
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            rwwa::runtime::EffectBlockDiagnostics block{};
            block.framesProcessed = 1u;
            block.usedRuntimeScene = true;
            for (std::uint32_t index = 0u; index < kBlocksPerPublisher; ++index)
            {
                const std::uint32_t writerCode = publisherIndex + 1u;
                const float blockCode = static_cast<float>(writerCode * 100000u + index);
                block.runtimeSceneRevision =
                    (static_cast<std::uint64_t>(writerCode) << 32u) | index;
                block.inputPeak = blockCode;
                block.outputPeak = blockCode + 1.0f;
                block.wetDifferencePeak = blockCode + 2.0f;
                rwwa::runtime::PublishEffectBlockDiagnostics(block);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread& publisher : publishers)
    {
        publisher.join();
    }

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == kPublisherCount * kBlocksPerPublisher);
    EXPECT(diagnostics.runtimeSceneBlockCount == diagnostics.effectExecuteCount);
    EXPECT(diagnostics.authoredFallbackBlockCount == 0u);
    EXPECT(diagnostics.lastBlockUsedRuntimeScene == 1u);

    const std::uint32_t writerCode =
        static_cast<std::uint32_t>(diagnostics.lastRuntimeSceneRevision >> 32u);
    const std::uint32_t blockIndex =
        static_cast<std::uint32_t>(diagnostics.lastRuntimeSceneRevision);
    EXPECT(writerCode >= 1u && writerCode <= kPublisherCount);
    EXPECT(blockIndex < kBlocksPerPublisher);
    const float expectedBlockCode =
        static_cast<float>(writerCode * 100000u + blockIndex);
    EXPECT(diagnostics.lastInputPeak == expectedBlockCode);
    EXPECT(diagnostics.lastOutputPeak == expectedBlockCode + 1.0f);
    EXPECT(diagnostics.lastWetDifferencePeak == expectedBlockCode + 2.0f);
}

void TestDiagnosticsCounterWrapAndRevision()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    constexpr std::uint64_t kRevision = 0xfedcba9876543210ull;
    rwwa::runtime::EffectBlockDiagnostics first{};
    first.framesProcessed = (std::numeric_limits<std::uint32_t>::max)();
    first.runtimeSceneRevision = kRevision;
    first.usedRuntimeScene = true;
    first.nonFiniteSampleCount = (std::numeric_limits<std::uint32_t>::max)();
    rwwa::runtime::PublishEffectBlockDiagnostics(first);

    rwwa::runtime::EffectBlockDiagnostics second{};
    second.framesProcessed = 17u;
    second.runtimeSceneRevision = kRevision + 1u;
    second.usedRuntimeScene = true;
    second.nonFiniteSampleCount = 17u;
    rwwa::runtime::PublishEffectBlockDiagnostics(second);

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 2u);
    EXPECT(diagnostics.framesProcessed ==
        static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) + 17u);
    EXPECT(diagnostics.runtimeSceneBlockCount == 2u);
    EXPECT(diagnostics.lastRuntimeSceneRevision == kRevision + 1u);
    EXPECT(diagnostics.nonFiniteSampleCount ==
        (std::numeric_limits<std::uint32_t>::max)());
}

void TestDiagnosticsConcurrency()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    constexpr std::uint32_t kThreadCount = 4u;
    constexpr std::uint32_t kBlocksPerThread = 10000u;
    std::thread threads[kThreadCount];
    for (std::uint32_t threadIndex = 0u; threadIndex < kThreadCount; ++threadIndex)
    {
        threads[threadIndex] = std::thread([threadIndex]()
        {
            for (std::uint32_t block = 0u; block < kBlocksPerThread; ++block)
            {
                rwwa::runtime::EffectBlockDiagnostics diagnostics{};
                diagnostics.framesProcessed = 1u;
                diagnostics.usedRuntimeScene = (threadIndex & 1u) != 0u;
                diagnostics.runtimeSceneRevision = block;
                diagnostics.inputPeak = 0.25f + static_cast<float>(threadIndex) * 0.1f;
                diagnostics.outputPeak = 0.5f;
                diagnostics.wetDifferencePeak = 0.125f;
                diagnostics.nonFiniteSampleCount = 1u;
                rwwa::runtime::PublishEffectBlockDiagnostics(diagnostics);
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == kThreadCount * kBlocksPerThread);
    EXPECT(diagnostics.framesProcessed == kThreadCount * kBlocksPerThread);
    EXPECT(diagnostics.runtimeSceneBlockCount == 2u * kBlocksPerThread);
    EXPECT(diagnostics.authoredFallbackBlockCount == 2u * kBlocksPerThread);
    EXPECT(diagnostics.maxInputPeak == 0.55f);
    EXPECT(diagnostics.maxOutputPeak == 0.5f);
    EXPECT(diagnostics.maxWetDifferencePeak == 0.125f);
    EXPECT(diagnostics.nonFiniteSampleCount == kThreadCount * kBlocksPerThread);
}

void TestDiagnosticsResetPublishGetStress()
{
    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    constexpr std::uint32_t kPublisherCount = 3u;
    constexpr std::uint32_t kBlocksPerPublisher = 12000u;
    constexpr std::uint32_t kResetAttempts = 12000u;
    constexpr std::uint32_t kGetAttempts = 40000u;
    constexpr std::uint32_t kFramesPerBlock = 3u;

    std::atomic<bool> start{false};
    std::atomic<bool> mutationsDone{false};
    std::atomic<std::uint32_t> invariantFailures{0u};
    std::atomic<std::uint32_t> unexpectedStatuses{0u};
    std::atomic<std::uint32_t> successfulSnapshots{0u};
    std::thread publishers[kPublisherCount];
    for (std::uint32_t publisherIndex = 0u;
         publisherIndex < kPublisherCount;
         ++publisherIndex)
    {
        publishers[publisherIndex] = std::thread([publisherIndex, &start]()
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            rwwa::runtime::EffectBlockDiagnostics block{};
            block.framesProcessed = kFramesPerBlock;
            block.usedRuntimeScene = (publisherIndex & 1u) != 0u;
            block.wetBypass = true;
            block.geometryDisabled = true;
            block.inputPeak = 0.25f;
            block.outputPeak = 0.5f;
            block.wetDifferencePeak = 0.25f;
            block.nonFiniteSampleCount = 1u;
            for (std::uint32_t index = 0u; index < kBlocksPerPublisher; ++index)
            {
                block.runtimeSceneRevision = index;
                rwwa::runtime::PublishEffectBlockDiagnostics(block);
            }
        });
    }

    std::thread resetter([&start, &unexpectedStatuses]()
    {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        for (std::uint32_t index = 0u; index < kResetAttempts; ++index)
        {
            const RWWA_RuntimeStatus status = RWWA_RuntimeDiagnostics_ResetV1();
            if (status != RWWA_RUNTIME_STATUS_OK && status != RWWA_RUNTIME_STATUS_BUSY)
            {
                unexpectedStatuses.fetch_add(1u, std::memory_order_relaxed);
            }
        }
    });

    std::thread getter([
        &start,
        &mutationsDone,
        &invariantFailures,
        &unexpectedStatuses,
        &successfulSnapshots]()
    {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::uint32_t attemptCount = 0u;
        bool observedSuccessfulSnapshot = false;
        do
        {
            ++attemptCount;
            RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
            const RWWA_RuntimeStatus status = RWWA_RuntimeDiagnostics_GetV1(&diagnostics);
            if (status == RWWA_RUNTIME_STATUS_BUSY)
            {
                continue;
            }
            if (status != RWWA_RUNTIME_STATUS_OK)
            {
                unexpectedStatuses.fetch_add(1u, std::memory_order_relaxed);
                continue;
            }
            observedSuccessfulSnapshot = true;
            successfulSnapshots.fetch_add(1u, std::memory_order_relaxed);
            const std::uint64_t classifiedBlocks =
                diagnostics.runtimeSceneBlockCount + diagnostics.authoredFallbackBlockCount;
            if (diagnostics.effectExecuteCount != classifiedBlocks ||
                diagnostics.framesProcessed !=
                    diagnostics.effectExecuteCount * kFramesPerBlock ||
                diagnostics.wetBypassBlockCount != diagnostics.effectExecuteCount ||
                diagnostics.geometryDisabledBlockCount != diagnostics.effectExecuteCount ||
                diagnostics.nonFiniteSampleCount != diagnostics.effectExecuteCount)
            {
                invariantFailures.fetch_add(1u, std::memory_order_relaxed);
            }
        } while (attemptCount < kGetAttempts ||
            !mutationsDone.load(std::memory_order_acquire) ||
            !observedSuccessfulSnapshot);
    });

    start.store(true, std::memory_order_release);
    for (std::thread& publisher : publishers)
    {
        publisher.join();
    }
    resetter.join();
    mutationsDone.store(true, std::memory_order_release);
    getter.join();

    EXPECT(unexpectedStatuses.load(std::memory_order_relaxed) == 0u);
    EXPECT(invariantFailures.load(std::memory_order_relaxed) == 0u);
    EXPECT(successfulSnapshots.load(std::memory_order_relaxed) != 0u);

    EXPECT(RWWA_RuntimeDiagnostics_ResetV1() == RWWA_RUNTIME_STATUS_OK);
    RWWA_RuntimeDiagnosticsV1 diagnostics = MakeDiagnosticsGetBuffer();
    EXPECT(RWWA_RuntimeDiagnostics_GetV1(&diagnostics) == RWWA_RUNTIME_STATUS_OK);
    EXPECT(diagnostics.effectExecuteCount == 0u);
    EXPECT(diagnostics.framesProcessed == 0u);
}
} // namespace

int main()
{
    TestFirstClaimTransitionNeverFallsBack();
    TestRuntimeOverrideAndFallbackSelection();
    TestAbiValidationAndLimits();
    TestSetReadClear();
    TestSanitization();
    TestConcurrentConsistency();
    TestClaimedConcurrencyNeverFallsBack();
    TestDiagnosticsAbiAndBehavior();
    TestEffectSampleDiagnosticsDetectsNonFiniteValues();
    TestDiagnosticsOverlapReturnsBusyWithoutPartialState();
    TestDiagnosticsCompletedPublishInvalidatesInFlightGet();
    TestDiagnosticsLastTupleTryCommitIsCoherent();
    TestDiagnosticsLastTupleMultiWriterEncoding();
    TestDiagnosticsCounterWrapAndRevision();
    TestDiagnosticsConcurrency();
    TestDiagnosticsResetPublishGetStress();
    if (g_failureCount != 0)
    {
        std::cerr << g_failureCount << " expectation(s) failed\n";
        return 1;
    }
    return 0;
}
