#define RWWA_RUNTIME_API_INTERNAL
#include "RealWorldWeatherAcousticsRuntimeAPI.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kReadAttemptCount = 8u;

struct AtomicFeature
{
    std::atomic<std::uint32_t> idLow{0u};
    std::atomic<std::uint32_t> idHigh{0u};
    std::atomic<std::uint32_t> x{0u};
    std::atomic<std::uint32_t> y{0u};
    std::atomic<std::uint32_t> z{0u};
    std::atomic<std::uint32_t> radius{0u};
    std::atomic<std::uint32_t> profile{0u};
    std::atomic<std::uint32_t> mask{0u};
    std::atomic<std::uint32_t> priority{0u};
};

struct AtomicScene
{
    std::atomic<std::uint32_t> sequence{0u};
    std::atomic<std::uint32_t> claimed{0u};
    std::atomic<std::uint64_t> revision{0u};
    std::atomic<std::uint32_t> valid{0u};
    std::atomic<std::uint32_t> geometryEnabled{0u};
    std::atomic<std::uint32_t> listenerX{0u};
    std::atomic<std::uint32_t> listenerY{0u};
    std::atomic<std::uint32_t> listenerZ{0u};
    std::atomic<std::uint32_t> listenerYawRadians{0u};
    std::atomic<std::uint32_t> rainIntensity{0u};
    std::atomic<std::uint32_t> windSpeedMetersPerSecond{0u};
    std::atomic<std::uint32_t> windDirectionRadians{0u};
    std::atomic<std::uint32_t> windGustiness{0u};
    std::atomic<std::uint32_t> weatherSeed{0u};
    std::atomic<std::uint32_t> weatherMasterGainLinear{0u};
    std::atomic<std::uint32_t> featureCount{0u};
    AtomicFeature features[RWWA_RUNTIME_SCENE_MAX_FEATURES]{};
    std::atomic_flag writer = ATOMIC_FLAG_INIT;
};

struct AtomicDiagnostics
{
    std::atomic<std::uint32_t> resetInProgress{0u};
    std::atomic<std::uint32_t> activePublishers{0u};
    std::atomic<std::uint64_t> completedGeneration{0u};
    std::atomic<std::uint64_t> effectExecuteCount{0u};
    std::atomic<std::uint64_t> framesProcessed{0u};
    std::atomic<std::uint64_t> runtimeSceneBlockCount{0u};
    std::atomic<std::uint64_t> authoredFallbackBlockCount{0u};
    std::atomic<std::uint64_t> wetBypassBlockCount{0u};
    std::atomic<std::uint64_t> geometryDisabledBlockCount{0u};
    std::atomic_flag lastTupleWriter = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> lastRuntimeSceneRevision{0u};
    std::atomic<std::uint32_t> lastInputPeak{0u};
    std::atomic<std::uint32_t> maxInputPeak{0u};
    std::atomic<std::uint32_t> lastOutputPeak{0u};
    std::atomic<std::uint32_t> maxOutputPeak{0u};
    std::atomic<std::uint32_t> lastWetDifferencePeak{0u};
    std::atomic<std::uint32_t> maxWetDifferencePeak{0u};
    std::atomic<std::uint32_t> lastBlockUsedRuntimeScene{0u};
    std::atomic<std::uint64_t> nonFiniteSampleCount{0u};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
    "The real-time scene snapshot requires lock-free 32-bit atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
    "The Windows x64 runtime ABI requires lock-free 64-bit atomics");
static_assert(sizeof(float) == sizeof(std::uint32_t),
    "The real-time scene snapshot requires 32-bit IEEE-style floats");
static_assert(sizeof(RWWA_RuntimeDiagnosticsV1) == 96u,
    "Diagnostics V1 size is part of the public C ABI");
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, effectExecuteCount) == 8u,
    "Diagnostics V1 counter offset is part of the public C ABI");
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, lastRuntimeSceneRevision) == 56u,
    "Diagnostics V1 revision offset is part of the public C ABI");
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, lastBlockUsedRuntimeScene) == 88u,
    "Diagnostics V1 flag offset is part of the public C ABI");
static_assert(offsetof(RWWA_RuntimeDiagnosticsV1, nonFiniteSampleCount) == 92u,
    "Diagnostics V1 non-finite counter offset is part of the public C ABI");

AtomicScene g_scene;
AtomicDiagnostics g_diagnostics;
#if defined(RWWA_RUNTIME_TESTING)
rwwa::runtime::RuntimeTestHook g_sceneWriteTestHook = nullptr;
rwwa::runtime::RuntimeTestHook g_diagnosticsPublishTestHook = nullptr;
rwwa::runtime::RuntimeTestHook g_diagnosticsGetSnapshotTestHook = nullptr;
rwwa::runtime::RuntimeTestHook g_diagnosticsMidTupleTestHook = nullptr;
#endif

std::uint32_t FloatBits(float value) noexcept
{
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float BitsFloat(std::uint32_t bits) noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void AddCounter(std::atomic<std::uint64_t>& counter, std::uint64_t amount) noexcept
{
    counter.fetch_add(amount, std::memory_order_relaxed);
}

std::uint64_t LoadCounter(const std::atomic<std::uint64_t>& counter) noexcept
{
    return counter.load(std::memory_order_acquire);
}

void ResetCounter(std::atomic<std::uint64_t>& counter) noexcept
{
    counter.store(0u, std::memory_order_release);
}

float SanitizePeak(float value) noexcept
{
    return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

void UpdateMaximum(std::atomic<std::uint32_t>& maximum, float candidate) noexcept
{
    candidate = SanitizePeak(candidate);
    std::uint32_t current = maximum.load(std::memory_order_relaxed);
    for (std::uint32_t attempt = 0u; attempt < kReadAttemptCount; ++attempt)
    {
        if (BitsFloat(current) >= candidate)
        {
            return;
        }
        if (maximum.compare_exchange_weak(
                current,
                FloatBits(candidate),
                std::memory_order_relaxed,
                std::memory_order_relaxed))
        {
            return;
        }
    }
}

float ClampFinite(
    float value,
    float minimum,
    float maximum,
    float fallback,
    bool& clamped) noexcept
{
    if (!std::isfinite(value))
    {
        clamped = true;
        return fallback;
    }
    const float result = std::clamp(value, minimum, maximum);
    clamped |= result != value;
    return result;
}

float WrapRadians(float value, bool& clamped) noexcept
{
    if (!std::isfinite(value))
    {
        clamped = true;
        return 0.0f;
    }
    const float original = value;
    value = std::fmod(value + kPi, 2.0f * kPi);
    if (value < 0.0f)
    {
        value += 2.0f * kPi;
    }
    value -= kPi;
    clamped |= value != original;
    return value;
}

std::uint32_t ClampUnsigned(
    std::uint32_t value,
    std::uint32_t maximum,
    std::uint32_t fallback,
    bool& clamped) noexcept
{
    if (value <= maximum)
    {
        return value;
    }
    clamped = true;
    return fallback;
}

std::int32_t ClampPriority(std::int32_t value, bool& clamped) noexcept
{
    const std::int32_t result = std::clamp(value, 0, 1000);
    clamped |= result != value;
    return result;
}

void StoreFloat(std::atomic<std::uint32_t>& target, float value) noexcept
{
    target.store(FloatBits(value), std::memory_order_relaxed);
}

float LoadFloat(const std::atomic<std::uint32_t>& source) noexcept
{
    return BitsFloat(source.load(std::memory_order_relaxed));
}

void StoreScene(const RWWA_RuntimeSceneV1& scene) noexcept
{
    g_scene.revision.store(scene.revision, std::memory_order_relaxed);
    g_scene.valid.store(scene.valid, std::memory_order_relaxed);
    g_scene.geometryEnabled.store(scene.geometryEnabled, std::memory_order_relaxed);
    StoreFloat(g_scene.listenerX, scene.listenerX);
    StoreFloat(g_scene.listenerY, scene.listenerY);
    StoreFloat(g_scene.listenerZ, scene.listenerZ);
    StoreFloat(g_scene.listenerYawRadians, scene.listenerYawRadians);
    StoreFloat(g_scene.rainIntensity, scene.rainIntensity);
    StoreFloat(g_scene.windSpeedMetersPerSecond, scene.windSpeedMetersPerSecond);
    StoreFloat(g_scene.windDirectionRadians, scene.windDirectionRadians);
    StoreFloat(g_scene.windGustiness, scene.windGustiness);
    g_scene.weatherSeed.store(scene.weatherSeed, std::memory_order_relaxed);
    StoreFloat(g_scene.weatherMasterGainLinear, scene.weatherMasterGainLinear);
    g_scene.featureCount.store(scene.featureCount, std::memory_order_relaxed);

    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        const RWWA_RuntimeFeatureV1& source = scene.features[index];
        AtomicFeature& target = g_scene.features[index];
        target.idLow.store(static_cast<std::uint32_t>(source.id), std::memory_order_relaxed);
        target.idHigh.store(static_cast<std::uint32_t>(source.id >> 32u), std::memory_order_relaxed);
        StoreFloat(target.x, source.x);
        StoreFloat(target.y, source.y);
        StoreFloat(target.z, source.z);
        StoreFloat(target.radius, source.radius);
        target.profile.store(source.profile, std::memory_order_relaxed);
        target.mask.store(source.mask, std::memory_order_relaxed);
        target.priority.store(
            static_cast<std::uint32_t>(source.priority), std::memory_order_relaxed);
    }
}

void LoadScene(RWWA_RuntimeSceneV1& scene) noexcept
{
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    scene.structSize = static_cast<std::uint32_t>(sizeof(RWWA_RuntimeSceneV1));
    scene.revision = g_scene.revision.load(std::memory_order_relaxed);
    scene.valid = g_scene.valid.load(std::memory_order_relaxed);
    scene.geometryEnabled = g_scene.geometryEnabled.load(std::memory_order_relaxed);
    scene.listenerX = LoadFloat(g_scene.listenerX);
    scene.listenerY = LoadFloat(g_scene.listenerY);
    scene.listenerZ = LoadFloat(g_scene.listenerZ);
    scene.listenerYawRadians = LoadFloat(g_scene.listenerYawRadians);
    scene.rainIntensity = LoadFloat(g_scene.rainIntensity);
    scene.windSpeedMetersPerSecond = LoadFloat(g_scene.windSpeedMetersPerSecond);
    scene.windDirectionRadians = LoadFloat(g_scene.windDirectionRadians);
    scene.windGustiness = LoadFloat(g_scene.windGustiness);
    scene.weatherSeed = g_scene.weatherSeed.load(std::memory_order_relaxed);
    scene.weatherMasterGainLinear = LoadFloat(g_scene.weatherMasterGainLinear);
    scene.featureCount = g_scene.featureCount.load(std::memory_order_relaxed);
    scene.reserved0 = 0u;

    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        const AtomicFeature& source = g_scene.features[index];
        RWWA_RuntimeFeatureV1& target = scene.features[index];
        const std::uint64_t idLow = source.idLow.load(std::memory_order_relaxed);
        const std::uint64_t idHigh = source.idHigh.load(std::memory_order_relaxed);
        target.id = idLow | (idHigh << 32u);
        target.x = LoadFloat(source.x);
        target.y = LoadFloat(source.y);
        target.z = LoadFloat(source.z);
        target.radius = LoadFloat(source.radius);
        target.profile = source.profile.load(std::memory_order_relaxed);
        target.mask = source.mask.load(std::memory_order_relaxed);
        target.priority = static_cast<std::int32_t>(
            source.priority.load(std::memory_order_relaxed));
        target.reserved0 = 0u;
    }
}

RWWA_RuntimeStatus ValidateHeader(const RWWA_RuntimeSceneV1* scene) noexcept
{
    if (scene == nullptr)
    {
        return RWWA_RUNTIME_STATUS_NULL_ARGUMENT;
    }
    if (scene->abiVersion != RWWA_RUNTIME_SCENE_ABI_VERSION)
    {
        return RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI;
    }
    if (scene->structSize < sizeof(RWWA_RuntimeSceneV1))
    {
        return RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL;
    }
    return RWWA_RUNTIME_STATUS_OK;
}

RWWA_RuntimeStatus ValidateHeader(const RWWA_RuntimeDiagnosticsV1* diagnostics) noexcept
{
    if (diagnostics == nullptr)
    {
        return RWWA_RUNTIME_STATUS_NULL_ARGUMENT;
    }
    if (diagnostics->abiVersion != RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION)
    {
        return RWWA_RUNTIME_STATUS_INCOMPATIBLE_ABI;
    }
    if (diagnostics->structSize < sizeof(RWWA_RuntimeDiagnosticsV1))
    {
        return RWWA_RUNTIME_STATUS_STRUCT_TOO_SMALL;
    }
    return RWWA_RUNTIME_STATUS_OK;
}

RWWA_RuntimeSceneV1 SanitizeScene(
    const RWWA_RuntimeSceneV1& source,
    bool& clamped) noexcept
{
    RWWA_RuntimeSceneV1 result = source;
    result.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    result.structSize = sizeof(RWWA_RuntimeSceneV1);
    result.valid = 1u;
    result.geometryEnabled = source.geometryEnabled == 0u ? 0u : 1u;
    clamped |= source.geometryEnabled > 1u;
    result.listenerX = ClampFinite(source.listenerX, -1.0e6f, 1.0e6f, 0.0f, clamped);
    result.listenerY = ClampFinite(source.listenerY, -1.0e6f, 1.0e6f, 0.0f, clamped);
    result.listenerZ = ClampFinite(source.listenerZ, -1.0e6f, 1.0e6f, 0.0f, clamped);
    result.listenerYawRadians = WrapRadians(source.listenerYawRadians, clamped);
    result.rainIntensity = ClampFinite(source.rainIntensity, 0.0f, 1.0f, 0.0f, clamped);
    result.windSpeedMetersPerSecond = ClampFinite(
        source.windSpeedMetersPerSecond, 0.0f, 60.0f, 0.0f, clamped);
    result.windDirectionRadians = WrapRadians(source.windDirectionRadians, clamped);
    result.windGustiness = ClampFinite(source.windGustiness, 0.0f, 1.0f, 0.0f, clamped);
    result.weatherMasterGainLinear = ClampFinite(
        source.weatherMasterGainLinear, 0.0f, 4.0f, 0.0f, clamped);
    result.reserved0 = 0u;

    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        RWWA_RuntimeFeatureV1& feature = result.features[index];
        if (index >= result.featureCount)
        {
            feature = {};
            continue;
        }
        feature.x = ClampFinite(feature.x, -1.0e6f, 1.0e6f, 0.0f, clamped);
        feature.y = ClampFinite(feature.y, -1.0e6f, 1.0e6f, 0.0f, clamped);
        feature.z = ClampFinite(feature.z, -1.0e6f, 1.0e6f, 0.0f, clamped);
        feature.radius = ClampFinite(feature.radius, 0.01f, 100000.0f, 1.0f, clamped);
        feature.profile = ClampUnsigned(feature.profile, 3u, 0u, clamped);
        const std::uint32_t sanitizedMask = feature.mask & 3u;
        clamped |= sanitizedMask != feature.mask;
        feature.mask = sanitizedMask;
        feature.priority = ClampPriority(feature.priority, clamped);
        feature.reserved0 = 0u;
    }
    return result;
}

bool TryAcquireWriter() noexcept
{
    return !g_scene.writer.test_and_set(std::memory_order_acquire);
}

void ReleaseWriter() noexcept
{
    g_scene.writer.clear(std::memory_order_release);
}

void ResetDiagnostics() noexcept
{
    ResetCounter(g_diagnostics.effectExecuteCount);
    ResetCounter(g_diagnostics.framesProcessed);
    ResetCounter(g_diagnostics.runtimeSceneBlockCount);
    ResetCounter(g_diagnostics.authoredFallbackBlockCount);
    ResetCounter(g_diagnostics.wetBypassBlockCount);
    ResetCounter(g_diagnostics.geometryDisabledBlockCount);
    g_diagnostics.lastRuntimeSceneRevision.store(0u, std::memory_order_relaxed);
    g_diagnostics.lastInputPeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.maxInputPeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.lastOutputPeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.maxOutputPeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.lastWetDifferencePeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.maxWetDifferencePeak.store(0u, std::memory_order_relaxed);
    g_diagnostics.lastBlockUsedRuntimeScene.store(0u, std::memory_order_release);
    g_diagnostics.nonFiniteSampleCount.store(0u, std::memory_order_release);
    g_diagnostics.lastTupleWriter.clear(std::memory_order_relaxed);
}

RWWA_RuntimeDiagnosticsV1 LoadDiagnostics() noexcept
{
    RWWA_RuntimeDiagnosticsV1 diagnostics{};
    diagnostics.abiVersion = RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION;
    diagnostics.structSize = static_cast<std::uint32_t>(sizeof(diagnostics));
    diagnostics.effectExecuteCount = LoadCounter(g_diagnostics.effectExecuteCount);
    diagnostics.framesProcessed = LoadCounter(g_diagnostics.framesProcessed);
    diagnostics.runtimeSceneBlockCount = LoadCounter(g_diagnostics.runtimeSceneBlockCount);
    diagnostics.authoredFallbackBlockCount = LoadCounter(
        g_diagnostics.authoredFallbackBlockCount);
    diagnostics.wetBypassBlockCount = LoadCounter(g_diagnostics.wetBypassBlockCount);
    diagnostics.geometryDisabledBlockCount = LoadCounter(
        g_diagnostics.geometryDisabledBlockCount);
    diagnostics.lastRuntimeSceneRevision =
        g_diagnostics.lastRuntimeSceneRevision.load(std::memory_order_acquire);
    diagnostics.lastInputPeak = BitsFloat(
        g_diagnostics.lastInputPeak.load(std::memory_order_acquire));
    diagnostics.maxInputPeak = BitsFloat(
        g_diagnostics.maxInputPeak.load(std::memory_order_acquire));
    diagnostics.lastOutputPeak = BitsFloat(
        g_diagnostics.lastOutputPeak.load(std::memory_order_acquire));
    diagnostics.maxOutputPeak = BitsFloat(
        g_diagnostics.maxOutputPeak.load(std::memory_order_acquire));
    diagnostics.lastWetDifferencePeak = BitsFloat(
        g_diagnostics.lastWetDifferencePeak.load(std::memory_order_acquire));
    diagnostics.maxWetDifferencePeak = BitsFloat(
        g_diagnostics.maxWetDifferencePeak.load(std::memory_order_acquire));
    diagnostics.lastBlockUsedRuntimeScene =
        g_diagnostics.lastBlockUsedRuntimeScene.load(std::memory_order_acquire);
    diagnostics.nonFiniteSampleCount = static_cast<std::uint32_t>((std::min)(
        g_diagnostics.nonFiniteSampleCount.load(std::memory_order_acquire),
        static_cast<std::uint64_t>(UINT32_MAX)));
    return diagnostics;
}
} // namespace

extern "C" RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_SetV1(const RWWA_RuntimeSceneV1* scene)
{
    const RWWA_RuntimeStatus headerStatus = ValidateHeader(scene);
    if (headerStatus != RWWA_RUNTIME_STATUS_OK)
    {
        return headerStatus;
    }
    if (scene->valid == 0u)
    {
        return RWWA_RUNTIME_STATUS_INVALID_ARGUMENT;
    }
    if (scene->featureCount > RWWA_RUNTIME_SCENE_MAX_FEATURES)
    {
        return RWWA_RUNTIME_STATUS_OUT_OF_RANGE;
    }

    bool clamped = scene->valid > 1u;
    const RWWA_RuntimeSceneV1 sanitized = SanitizeScene(*scene, clamped);
    if (!TryAcquireWriter())
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    g_scene.sequence.fetch_add(1u, std::memory_order_acq_rel);
#if defined(RWWA_RUNTIME_TESTING)
    if (g_sceneWriteTestHook != nullptr)
    {
        g_sceneWriteTestHook();
    }
#endif
    g_scene.claimed.store(1u, std::memory_order_relaxed);
    StoreScene(sanitized);
    g_scene.sequence.fetch_add(1u, std::memory_order_release);
    ReleaseWriter();
    return clamped ? RWWA_RUNTIME_STATUS_CLAMPED : RWWA_RUNTIME_STATUS_OK;
}

extern "C" RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_ClearV1(void)
{
    if (!TryAcquireWriter())
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    RWWA_RuntimeSceneV1 emptyScene{};
    emptyScene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    emptyScene.structSize = static_cast<std::uint32_t>(sizeof(emptyScene));
    emptyScene.valid = 0u;
    emptyScene.geometryEnabled = 0u;
    g_scene.sequence.fetch_add(1u, std::memory_order_acq_rel);
#if defined(RWWA_RUNTIME_TESTING)
    if (g_sceneWriteTestHook != nullptr)
    {
        g_sceneWriteTestHook();
    }
#endif
    g_scene.claimed.store(1u, std::memory_order_relaxed);
    StoreScene(emptyScene);
    g_scene.sequence.fetch_add(1u, std::memory_order_release);
    ReleaseWriter();
    return RWWA_RUNTIME_STATUS_OK;
}

extern "C" RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeScene_GetV1(RWWA_RuntimeSceneV1* outScene)
{
    const RWWA_RuntimeStatus headerStatus = ValidateHeader(outScene);
    if (headerStatus != RWWA_RUNTIME_STATUS_OK)
    {
        return headerStatus;
    }

    for (std::uint32_t attempt = 0u; attempt < kReadAttemptCount; ++attempt)
    {
        const std::uint32_t sequenceBefore =
            g_scene.sequence.load(std::memory_order_acquire);
        if ((sequenceBefore & 1u) != 0u)
        {
            continue;
        }

        RWWA_RuntimeSceneV1 snapshot{};
        LoadScene(snapshot);
        const bool claimed = g_scene.claimed.load(std::memory_order_relaxed) != 0u;
        std::atomic_thread_fence(std::memory_order_acquire);
        const std::uint32_t sequenceAfter =
            g_scene.sequence.load(std::memory_order_relaxed);
        if (sequenceBefore != sequenceAfter || (sequenceAfter & 1u) != 0u)
        {
            continue;
        }

        *outScene = snapshot;
        if (snapshot.valid != 0u)
        {
            return RWWA_RUNTIME_STATUS_OK;
        }
        return claimed
            ? RWWA_RUNTIME_STATUS_NO_SCENE
            : RWWA_RUNTIME_STATUS_UNCLAIMED;
    }
    return RWWA_RUNTIME_STATUS_BUSY;
}

extern "C" RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeDiagnostics_ResetV1(void)
{
    // The reset flag and publisher count form one handshake even though they
    // are stored in separate atomics. Sequential consistency closes the
    // store-buffering outcome where each side could otherwise observe the
    // other's old value and both mutate the diagnostics payload.
    std::uint32_t expected = 0u;
    if (!g_diagnostics.resetInProgress.compare_exchange_strong(
            expected,
            1u,
            std::memory_order_seq_cst,
            std::memory_order_seq_cst))
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    if (g_diagnostics.activePublishers.load(std::memory_order_seq_cst) != 0u)
    {
        g_diagnostics.resetInProgress.store(0u, std::memory_order_seq_cst);
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    ResetDiagnostics();
    g_diagnostics.completedGeneration.fetch_add(1u, std::memory_order_seq_cst);
    g_diagnostics.resetInProgress.store(0u, std::memory_order_seq_cst);
    return RWWA_RUNTIME_STATUS_OK;
}

extern "C" RWWA_RUNTIME_API RWWA_RuntimeStatus RWWA_RUNTIME_CALL
RWWA_RuntimeDiagnostics_GetV1(RWWA_RuntimeDiagnosticsV1* outDiagnostics)
{
    const RWWA_RuntimeStatus headerStatus = ValidateHeader(outDiagnostics);
    if (headerStatus != RWWA_RUNTIME_STATUS_OK)
    {
        return headerStatus;
    }
    if (g_diagnostics.resetInProgress.load(std::memory_order_seq_cst) != 0u ||
        g_diagnostics.activePublishers.load(std::memory_order_seq_cst) != 0u)
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    const std::uint64_t generationBefore =
        g_diagnostics.completedGeneration.load(std::memory_order_seq_cst);
    const RWWA_RuntimeDiagnosticsV1 snapshot = LoadDiagnostics();
#if defined(RWWA_RUNTIME_TESTING)
    if (g_diagnosticsGetSnapshotTestHook != nullptr)
    {
        g_diagnosticsGetSnapshotTestHook();
    }
#endif
    if (g_diagnostics.activePublishers.load(std::memory_order_seq_cst) != 0u ||
        g_diagnostics.resetInProgress.load(std::memory_order_seq_cst) != 0u)
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    // Keep generation as the final concurrency observation. A publisher that
    // completes after the payload read but before the checks above must still
    // invalidate the snapshot even when its active count is already back to 0.
    const std::uint64_t generationAfter =
        g_diagnostics.completedGeneration.load(std::memory_order_seq_cst);
    if (generationBefore != generationAfter)
    {
        return RWWA_RUNTIME_STATUS_BUSY;
    }
    *outDiagnostics = snapshot;
    return RWWA_RUNTIME_STATUS_OK;
}

namespace rwwa::runtime
{
namespace
{
SceneSnapshot MakeClaimedEmptySnapshot() noexcept
{
    SceneSnapshot snapshot{};
    snapshot.weather.geometryEnabled = false;
    snapshot.weather.masterGainLinear = 0.0f;
    return snapshot;
}

SceneSnapshot CompileRuntimeScene(const RWWA_RuntimeSceneV1& runtimeScene) noexcept
{
    SceneInput scene{};
    scene.featureCount = runtimeScene.featureCount;
    for (std::uint32_t index = 0u; index < runtimeScene.featureCount; ++index)
    {
        const RWWA_RuntimeFeatureV1& source = runtimeScene.features[index];
        SphereFeature& target = scene.features[index];
        target.id = source.id;
        target.position = {source.x, source.y, source.z};
        target.radius = source.radius;
        target.profileId = source.profile;
        target.responseMask = source.mask;
        target.priority = source.priority;
    }

    ListenerState listener{};
    listener.position = {
        runtimeScene.listenerX,
        runtimeScene.listenerY,
        runtimeScene.listenerZ};
    listener.yawRadians = runtimeScene.listenerYawRadians;

    WeatherState weather{};
    weather.rainIntensity = runtimeScene.rainIntensity;
    weather.windSpeedMetersPerSecond = runtimeScene.windSpeedMetersPerSecond;
    weather.windDirectionRadians = runtimeScene.windDirectionRadians;
    weather.windGustiness = runtimeScene.windGustiness;
    weather.seed = runtimeScene.weatherSeed;
    weather.geometryEnabled = runtimeScene.geometryEnabled != 0u;
    weather.masterGainLinear = runtimeScene.weatherMasterGainLinear;
    return CompileScene(scene, listener, weather);
}
} // namespace

#if defined(RWWA_RUNTIME_TESTING)
void SetSceneWriteTestHook(RuntimeTestHook hook) noexcept
{
    g_sceneWriteTestHook = hook;
}

void SetDiagnosticsPublishTestHook(RuntimeTestHook hook) noexcept
{
    g_diagnosticsPublishTestHook = hook;
}

void SetDiagnosticsGetSnapshotTestHook(RuntimeTestHook hook) noexcept
{
    g_diagnosticsGetSnapshotTestHook = hook;
}

void SetDiagnosticsMidTupleTestHook(RuntimeTestHook hook) noexcept
{
    g_diagnosticsMidTupleTestHook = hook;
}
#endif

CompiledSceneSelection CompileSceneWithRuntimeOverride(
    const SceneInput& fallbackScene,
    const ListenerState& fallbackListener,
    const WeatherState& fallbackWeather,
    SceneSnapshot& retainedRuntimeScene,
    std::uint64_t& retainedRuntimeRevision,
    bool& runtimeClaimed,
    bool& hasRetainedRuntimeScene) noexcept
{
    RWWA_RuntimeSceneV1 runtimeScene{};
    runtimeScene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    runtimeScene.structSize = static_cast<std::uint32_t>(sizeof(runtimeScene));
    const RWWA_RuntimeStatus status = RWWA_RuntimeScene_GetV1(&runtimeScene);

    CompiledSceneSelection selection{};
    if (status == RWWA_RUNTIME_STATUS_OK)
    {
        selection.snapshot = CompileRuntimeScene(runtimeScene);
        selection.runtimeRevision = runtimeScene.revision;
        selection.usesRuntimeScene = true;
        retainedRuntimeScene = selection.snapshot;
        retainedRuntimeRevision = selection.runtimeRevision;
        runtimeClaimed = true;
        hasRetainedRuntimeScene = true;
        return selection;
    }

    if (status == RWWA_RUNTIME_STATUS_UNCLAIMED && !runtimeClaimed)
    {
        selection.snapshot = CompileScene(
            fallbackScene,
            fallbackListener,
            fallbackWeather);
        return selection;
    }

    runtimeClaimed = runtimeClaimed || status == RWWA_RUNTIME_STATUS_BUSY ||
        g_scene.claimed.load(std::memory_order_acquire) != 0u;
    if (!runtimeClaimed)
    {
        selection.snapshot = CompileScene(
            fallbackScene,
            fallbackListener,
            fallbackWeather);
        return selection;
    }

    selection.usesRuntimeScene = true;
    if (status == RWWA_RUNTIME_STATUS_BUSY && hasRetainedRuntimeScene)
    {
        selection.snapshot = retainedRuntimeScene;
        selection.runtimeRevision = retainedRuntimeRevision;
        return selection;
    }

    hasRetainedRuntimeScene = false;
    retainedRuntimeRevision = 0u;
    retainedRuntimeScene = MakeClaimedEmptySnapshot();
    selection.snapshot = retainedRuntimeScene;
    return selection;
}

void AccumulateEffectSampleDiagnostics(
    float inputSample,
    float outputSample,
    EffectBlockDiagnostics& block) noexcept
{
    const float difference = outputSample - inputSample;
    if (!std::isfinite(inputSample) ||
        !std::isfinite(outputSample) ||
        !std::isfinite(difference))
    {
        if (block.nonFiniteSampleCount != UINT32_MAX)
        {
            ++block.nonFiniteSampleCount;
        }
    }
    block.inputPeak = (std::max)(block.inputPeak, SanitizePeak(std::abs(inputSample)));
    block.outputPeak = (std::max)(block.outputPeak, SanitizePeak(std::abs(outputSample)));
    block.wetDifferencePeak = (std::max)(
        block.wetDifferencePeak,
        SanitizePeak(std::abs(difference)));
}

void PublishEffectBlockDiagnostics(const EffectBlockDiagnostics& block) noexcept
{
    if (g_diagnostics.resetInProgress.load(std::memory_order_seq_cst) != 0u)
    {
        return;
    }
    g_diagnostics.activePublishers.fetch_add(1u, std::memory_order_seq_cst);
    if (g_diagnostics.resetInProgress.load(std::memory_order_seq_cst) != 0u)
    {
        g_diagnostics.activePublishers.fetch_sub(1u, std::memory_order_seq_cst);
        return;
    }
#if defined(RWWA_RUNTIME_TESTING)
    if (g_diagnosticsPublishTestHook != nullptr)
    {
        g_diagnosticsPublishTestHook();
    }
#endif

    AddCounter(g_diagnostics.effectExecuteCount, 1u);
    AddCounter(g_diagnostics.framesProcessed, block.framesProcessed);
    AddCounter(
        block.usedRuntimeScene
            ? g_diagnostics.runtimeSceneBlockCount
            : g_diagnostics.authoredFallbackBlockCount,
        1u);
    if (block.wetBypass)
    {
        AddCounter(g_diagnostics.wetBypassBlockCount, 1u);
    }
    if (block.geometryDisabled)
    {
        AddCounter(g_diagnostics.geometryDisabledBlockCount, 1u);
    }
    AddCounter(g_diagnostics.nonFiniteSampleCount, block.nonFiniteSampleCount);

    const float inputPeak = SanitizePeak(block.inputPeak);
    const float outputPeak = SanitizePeak(block.outputPeak);
    const float wetDifferencePeak = SanitizePeak(block.wetDifferencePeak);
    UpdateMaximum(g_diagnostics.maxInputPeak, inputPeak);
    UpdateMaximum(g_diagnostics.maxOutputPeak, outputPeak);
    UpdateMaximum(g_diagnostics.maxWetDifferencePeak, wetDifferencePeak);

    // Audio threads never wait for the last-block tuple. A contending block
    // still contributes to counters and maxima, but leaves the previous
    // complete tuple intact.
    if (!g_diagnostics.lastTupleWriter.test_and_set(std::memory_order_acquire))
    {
        g_diagnostics.lastRuntimeSceneRevision.store(
            block.usedRuntimeScene ? block.runtimeSceneRevision : 0u,
            std::memory_order_relaxed);
#if defined(RWWA_RUNTIME_TESTING)
        if (g_diagnosticsMidTupleTestHook != nullptr)
        {
            g_diagnosticsMidTupleTestHook();
        }
#endif
        g_diagnostics.lastInputPeak.store(FloatBits(inputPeak), std::memory_order_relaxed);
        g_diagnostics.lastOutputPeak.store(FloatBits(outputPeak), std::memory_order_relaxed);
        g_diagnostics.lastWetDifferencePeak.store(
            FloatBits(wetDifferencePeak), std::memory_order_relaxed);
        g_diagnostics.lastBlockUsedRuntimeScene.store(
            block.usedRuntimeScene ? 1u : 0u,
            std::memory_order_relaxed);
        g_diagnostics.lastTupleWriter.clear(std::memory_order_release);
    }
    g_diagnostics.completedGeneration.fetch_add(1u, std::memory_order_seq_cst);
    g_diagnostics.activePublishers.fetch_sub(1u, std::memory_order_seq_cst);
}
} // namespace rwwa::runtime
