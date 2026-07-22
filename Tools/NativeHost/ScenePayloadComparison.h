#ifndef RWWA_NATIVE_HOST_SCENE_PAYLOAD_COMPARISON_H
#define RWWA_NATIVE_HOST_SCENE_PAYLOAD_COMPARISON_H

#include "RealWorldWeatherAcousticsRuntimeAPI.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rwwa::native_host
{
inline std::vector<std::string> FindScenePayloadMismatches(
    const RWWA_RuntimeSceneV1& expected,
    const RWWA_RuntimeSceneV1& actual)
{
    std::vector<std::string> mismatches;
    const auto compare = [&mismatches](const auto& expectedValue, const auto& actualValue, const std::string& field)
    {
        if (expectedValue != actualValue)
        {
            mismatches.push_back(field);
        }
    };

    compare(expected.abiVersion, actual.abiVersion, "abiVersion");
    compare(expected.structSize, actual.structSize, "structSize");
    compare(expected.revision, actual.revision, "revision");
    compare(expected.valid, actual.valid, "valid");
    compare(expected.geometryEnabled, actual.geometryEnabled, "geometryEnabled");
    compare(expected.listenerX, actual.listenerX, "listenerX");
    compare(expected.listenerY, actual.listenerY, "listenerY");
    compare(expected.listenerZ, actual.listenerZ, "listenerZ");
    compare(expected.listenerYawRadians, actual.listenerYawRadians, "listenerYawRadians");
    compare(expected.rainIntensity, actual.rainIntensity, "rainIntensity");
    compare(
        expected.windSpeedMetersPerSecond,
        actual.windSpeedMetersPerSecond,
        "windSpeedMetersPerSecond");
    compare(expected.windDirectionRadians, actual.windDirectionRadians, "windDirectionRadians");
    compare(expected.windGustiness, actual.windGustiness, "windGustiness");
    compare(expected.weatherSeed, actual.weatherSeed, "weatherSeed");
    compare(
        expected.weatherMasterGainLinear,
        actual.weatherMasterGainLinear,
        "weatherMasterGainLinear");
    compare(expected.featureCount, actual.featureCount, "featureCount");
    compare(expected.reserved0, actual.reserved0, "reserved0");

    for (std::uint32_t index = 0u; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        const RWWA_RuntimeFeatureV1& expectedFeature = expected.features[index];
        const RWWA_RuntimeFeatureV1& actualFeature = actual.features[index];
        const std::string prefix = "features[" + std::to_string(index) + "].";
        compare(expectedFeature.id, actualFeature.id, prefix + "id");
        compare(expectedFeature.x, actualFeature.x, prefix + "x");
        compare(expectedFeature.y, actualFeature.y, prefix + "y");
        compare(expectedFeature.z, actualFeature.z, prefix + "z");
        compare(expectedFeature.radius, actualFeature.radius, prefix + "radius");
        compare(expectedFeature.profile, actualFeature.profile, prefix + "profile");
        compare(expectedFeature.mask, actualFeature.mask, prefix + "mask");
        compare(expectedFeature.priority, actualFeature.priority, prefix + "priority");
        compare(expectedFeature.reserved0, actualFeature.reserved0, prefix + "reserved0");
    }
    return mismatches;
}
} // namespace rwwa::native_host

#endif // RWWA_NATIVE_HOST_SCENE_PAYLOAD_COMPARISON_H
