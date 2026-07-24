#include "ScenePayloadComparison.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
RWWA_RuntimeSceneV1 MakeCanonicalScene()
{
    RWWA_RuntimeSceneV1 scene{};
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    scene.structSize = static_cast<std::uint32_t>(sizeof(scene));
    scene.revision = 42u;
    scene.valid = 1u;
    scene.geometryEnabled = 1u;
    scene.listenerX = 1.0f;
    scene.listenerY = 2.0f;
    scene.listenerZ = 3.0f;
    scene.listenerYawRadians = 0.5f;
    scene.rainIntensity = 0.75f;
    scene.windSpeedMetersPerSecond = 12.0f;
    scene.windDirectionRadians = -0.75f;
    scene.windGustiness = 0.25f;
    scene.weatherSeed = 123456u;
    scene.weatherMasterGainLinear = 1.25f;
    scene.featureCount = RWWA_RUNTIME_SCENE_MAX_FEATURES;
    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        RWWA_RuntimeFeatureV1& feature = scene.features[index];
        feature.id = 1000u + index;
        feature.x = static_cast<float>(index) + 1.0f;
        feature.y = static_cast<float>(index) + 2.0f;
        feature.z = static_cast<float>(index) + 3.0f;
        feature.radius = static_cast<float>(index) + 4.0f;
        feature.profile = index % (RWWA_RUNTIME_PROFILE_MAX + 1u);
        feature.mask = index % 3u;
        feature.priority = static_cast<std::int32_t>(index) - 4;
    }
    return scene;
}

std::vector<std::string> ExpectedEveryFieldMismatch()
{
    std::vector<std::string> fields{
        "abiVersion",
        "structSize",
        "revision",
        "valid",
        "geometryEnabled",
        "listenerX",
        "listenerY",
        "listenerZ",
        "listenerYawRadians",
        "rainIntensity",
        "windSpeedMetersPerSecond",
        "windDirectionRadians",
        "windGustiness",
        "weatherSeed",
        "weatherMasterGainLinear",
        "featureCount",
        "reserved0"};
    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        const std::string prefix = "features[" + std::to_string(index) + "].";
        fields.push_back(prefix + "id");
        fields.push_back(prefix + "x");
        fields.push_back(prefix + "y");
        fields.push_back(prefix + "z");
        fields.push_back(prefix + "radius");
        fields.push_back(prefix + "profile");
        fields.push_back(prefix + "mask");
        fields.push_back(prefix + "priority");
        fields.push_back(prefix + "reserved0");
    }
    return fields;
}
} // namespace

int main()
{
    const RWWA_RuntimeSceneV1 expected = MakeCanonicalScene();
    if (!rwwa::native_host::FindScenePayloadMismatches(expected, expected).empty())
    {
        std::cerr << "Identical Scene V1 payloads did not compare equal\n";
        return 1;
    }

    RWWA_RuntimeSceneV1 actual = expected;
    ++actual.abiVersion;
    actual.structSize += 4u;
    ++actual.revision;
    actual.valid = 0u;
    actual.geometryEnabled = 0u;
    actual.listenerX += 0.5f;
    actual.listenerY += 0.5f;
    actual.listenerZ += 0.5f;
    actual.listenerYawRadians += 0.5f;
    actual.rainIntensity += 0.5f;
    actual.windSpeedMetersPerSecond += 0.5f;
    actual.windDirectionRadians += 0.5f;
    actual.windGustiness += 0.5f;
    ++actual.weatherSeed;
    actual.weatherMasterGainLinear += 0.5f;
    --actual.featureCount;
    actual.reserved0 = 1u;
    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        RWWA_RuntimeFeatureV1& feature = actual.features[index];
        ++feature.id;
        feature.x += 0.5f;
        feature.y += 0.5f;
        feature.z += 0.5f;
        feature.radius += 0.5f;
        ++feature.profile;
        ++feature.mask;
        ++feature.priority;
        feature.reserved0 = 1u;
    }

    const std::vector<std::string> expectedMismatches = ExpectedEveryFieldMismatch();
    const std::vector<std::string> actualMismatches =
        rwwa::native_host::FindScenePayloadMismatches(expected, actual);
    if (actualMismatches != expectedMismatches)
    {
        std::cerr << "Scene V1 field coverage mismatch: expected " << expectedMismatches.size()
                  << " mismatches, received " << actualMismatches.size() << '\n';
        return 2;
    }
    return 0;
}
