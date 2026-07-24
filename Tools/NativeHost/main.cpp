#include <AK/SoundEngine/Common/AkMemoryMgr.h>
#include <AK/SoundEngine/Common/AkModule.h>
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/SoundEngine/Common/AkStreamMgrModule.h>
#include <AK/SoundEngine/Platforms/Windows/AkWinSoundEngine.h>

#include "AkDefaultIOHookDeferred.h"
#include "RealWorldWeatherAcousticsRuntimeAPI.h"
#include "ScenePayloadComparison.h"

#include <Windows.h>

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr AkGameObjectID kListenerId = 10000;
constexpr AkGameObjectID kEmitterId = 10001;
constexpr AkUInt32 kCompanyId = 64;
constexpr AkUInt32 kSourcePluginId = 31001;
constexpr AkUInt32 kEffectPluginId = 31002;
constexpr wchar_t kPluginDllName[] = L"RealWorldWeatherAcoustics";
constexpr wchar_t kInitBankName[] = L"Init.bnk";
constexpr float kNonSilentPeakThreshold = 1.0e-6f;
constexpr float kChangedDifferenceThreshold = 1.0e-5f;
constexpr float kTransparentDifferenceTolerance = 1.0e-7f;

static_assert(sizeof(RWWA_RuntimeFeatureV1) == 40, "Unexpected Runtime Feature V1 ABI layout");
static_assert(sizeof(RWWA_RuntimeSceneV1) == 392, "Unexpected Runtime Scene V1 ABI layout");
static_assert(sizeof(RWWA_RuntimeDiagnosticsV1) == 96, "Unexpected Runtime Diagnostics V1 ABI layout");
static_assert(
    offsetof(RWWA_RuntimeDiagnosticsV1, nonFiniteSampleCount) == 92,
    "Unexpected Runtime Diagnostics V1 non-finite counter offset");

struct Options
{
    std::wstring bankDir;
    std::wstring bank;
    std::wstring event;
    std::wstring pluginDir;
    std::uint32_t durationMs = 0;
    std::wstring report;
    std::wstring sceneJson;
    std::string differenceExpectation;
};

struct AkStep
{
    bool attempted = false;
    bool success = false;
    AKRESULT result = AK_NotImplemented;
};

struct RuntimeStep
{
    bool attempted = false;
    bool success = false;
    RWWA_RuntimeStatus status = RWWA_RUNTIME_STATUS_INVALID_ARGUMENT;
};

struct HostReport
{
    Options options;
    int exitCode = 1;
    std::string stage = "arguments";
    std::string message = "Native Host did not run";

    AkStep memoryManager;
    bool streamManagerAttempted = false;
    bool streamManagerCreated = false;
    AkStep ioDevice;
    AkStep soundEngine;
    AkStep bankPath;
    AkStep language;

    AkStep pluginDll;
    bool sourcePluginRegistered = false;
    bool effectPluginRegistered = false;

    bool scenePrepared = false;
    std::string sceneSource = "builtin";
    std::uint64_t sceneRevision = 0;
    std::uint32_t sceneFeatureCount = 0;
    std::uint32_t sceneStructSize = static_cast<std::uint32_t>(sizeof(RWWA_RuntimeSceneV1));
    bool sceneDllModuleFound = false;
    bool sceneSetExportFound = false;
    bool sceneGetExportFound = false;
    bool sceneClearExportFound = false;
    RuntimeStep sceneSet;
    RuntimeStep sceneGet;
    bool sceneRoundTripVerified = false;
    bool sceneRoundTripPayloadMatched = false;
    std::vector<std::string> sceneRoundTripMismatchFields;
    RuntimeStep sceneClear;

    std::uint32_t diagnosticsStructSize = static_cast<std::uint32_t>(sizeof(RWWA_RuntimeDiagnosticsV1));
    bool diagnosticsResetExportFound = false;
    bool diagnosticsGetExportFound = false;
    RuntimeStep diagnosticsReset;
    RuntimeStep diagnosticsGet;
    bool diagnosticsCaptured = false;
    bool diagnosticsHeaderVerified = false;
    RWWA_RuntimeDiagnosticsV1 diagnostics{};
    bool diagnosticsAssertionsEvaluated = false;
    bool effectExecuteCountPositive = false;
    bool framesProcessedPositive = false;
    bool runtimeSceneBlockCountPositive = false;
    bool authoredFallbackBlockCountZero = false;
    bool runtimeSceneRevisionMatchesPublished = false;
    bool nonFiniteSampleCountZero = false;
    bool inputPeakNonSilent = false;
    bool outputPeakNonSilent = false;
    bool wetDifferenceMatchesExpectation = false;
    bool reasonAssertionsRequired = false;
    std::string wetBypassCountExpectation = "not-specified";
    std::string geometryDisabledCountExpectation = "not-specified";
    bool wetBypassCountMatchesExpectation = false;
    bool geometryDisabledCountMatchesExpectation = false;
    bool reasonAssertionsPassed = false;
    bool diagnosticsAssertionsPassed = false;

    AkStep listenerRegistration;
    AkStep emitterRegistration;
    AkStep listenerPosition;
    AkStep emitterPosition;
    AkStep defaultListener;

    bool initBankRequired = false;
    bool initBankPresent = false;
    AkStep initBankLoad;
    AkBankID initBankId = AK_INVALID_BANK_ID;
    AkStep bankLoad;
    AkBankID bankId = AK_INVALID_BANK_ID;
    AkStep initBankUnload;
    AkStep bankUnload;

    bool postEventAttempted = false;
    AkPlayingID playingId = AK_INVALID_PLAYING_ID;
    std::uint32_t renderCalls = 0;
    std::uint32_t renderFailures = 0;
    AKRESULT lastRenderResult = AK_NotImplemented;
    bool stopRequested = false;

    bool emitterUnregistered = false;
    bool listenerUnregistered = false;
    bool soundEngineTerminated = false;
    bool ioDeviceTerminated = false;
    bool streamManagerDestroyed = false;
    bool memoryManagerTerminated = false;
};

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int byteCount = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0)
    {
        return "<invalid UTF-16>";
    }

    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        utf8.data(),
        byteCount,
        nullptr,
        nullptr);
    return utf8;
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream escaped;
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '\"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (character < 0x20)
            {
                static constexpr char kHex[] = "0123456789abcdef";
                escaped << "\\u00" << kHex[(character >> 4) & 0x0f] << kHex[character & 0x0f];
            }
            else
            {
                escaped << character;
            }
            break;
        }
    }
    return escaped.str();
}

void AppendString(std::ostringstream& json, const std::string& value)
{
    json << '\"' << JsonEscape(value) << '\"';
}

void AppendWideString(std::ostringstream& json, const std::wstring& value)
{
    AppendString(json, WideToUtf8(value));
}

void AppendBool(std::ostringstream& json, const bool value)
{
    json << (value ? "true" : "false");
}

void AppendFloat(std::ostringstream& json, const float value)
{
    if (!std::isfinite(value))
    {
        json << "null";
        return;
    }

    const std::streamsize previousPrecision = json.precision();
    json.precision(std::numeric_limits<float>::max_digits10);
    json << value;
    json.precision(previousPrecision);
}

void AppendAkStep(std::ostringstream& json, const AkStep& step)
{
    json << "{\"attempted\":";
    AppendBool(json, step.attempted);
    json << ",\"success\":";
    AppendBool(json, step.success);
    json << ",\"result\":";
    if (step.attempted)
    {
        json << static_cast<int>(step.result);
    }
    else
    {
        json << "null";
    }
    json << '}';
}

void AppendRuntimeStep(std::ostringstream& json, const RuntimeStep& step)
{
    json << "{\"attempted\":";
    AppendBool(json, step.attempted);
    json << ",\"success\":";
    AppendBool(json, step.success);
    json << ",\"status\":";
    if (step.attempted)
    {
        json << step.status;
    }
    else
    {
        json << "null";
    }
    json << '}';
}

std::string SerializeReport(const HostReport& report)
{
    std::ostringstream json;
    json << "{\"schemaVersion\":1,\"arguments\":{";
    json << "\"bankDir\":";
    AppendWideString(json, report.options.bankDir);
    json << ",\"bank\":";
    AppendWideString(json, report.options.bank);
    json << ",\"event\":";
    AppendWideString(json, report.options.event);
    json << ",\"pluginDir\":";
    AppendWideString(json, report.options.pluginDir);
    json << ",\"durationMs\":" << report.options.durationMs;
    json << ",\"report\":";
    AppendWideString(json, report.options.report);
    json << ",\"sceneJson\":";
    if (report.options.sceneJson.empty())
    {
        json << "null";
    }
    else
    {
        AppendWideString(json, report.options.sceneJson);
    }
    json << ",\"expectDifference\":";
    AppendString(json, report.options.differenceExpectation);
    json << "},\"initialization\":{";
    json << "\"memoryManager\":";
    AppendAkStep(json, report.memoryManager);
    json << ",\"streamManager\":{\"attempted\":";
    AppendBool(json, report.streamManagerAttempted);
    json << ",\"success\":";
    AppendBool(json, report.streamManagerCreated);
    json << "},\"ioDevice\":";
    AppendAkStep(json, report.ioDevice);
    json << ",\"soundEngine\":";
    AppendAkStep(json, report.soundEngine);
    json << ",\"bankPath\":";
    AppendAkStep(json, report.bankPath);
    json << ",\"language\":";
    AppendAkStep(json, report.language);
    json << "},\"pluginRegistration\":{\"dll\":";
    AppendAkStep(json, report.pluginDll);
    json << ",\"source\":{\"companyId\":" << kCompanyId << ",\"pluginId\":" << kSourcePluginId
         << ",\"registered\":";
    AppendBool(json, report.sourcePluginRegistered);
    json << "},\"effect\":{\"companyId\":" << kCompanyId << ",\"pluginId\":" << kEffectPluginId
         << ",\"registered\":";
    AppendBool(json, report.effectPluginRegistered);
    json << "}},\"sceneSubmission\":{\"prepared\":";
    AppendBool(json, report.scenePrepared);
    json << ",\"source\":";
    AppendString(json, report.sceneSource);
    json << ",\"revision\":" << report.sceneRevision << ",\"featureCount\":" << report.sceneFeatureCount
         << ",\"structSize\":" << report.sceneStructSize << ",\"dllModuleFound\":";
    AppendBool(json, report.sceneDllModuleFound);
    json << ",\"setExportFound\":";
    AppendBool(json, report.sceneSetExportFound);
    json << ",\"getExportFound\":";
    AppendBool(json, report.sceneGetExportFound);
    json << ",\"clearExportFound\":";
    AppendBool(json, report.sceneClearExportFound);
    json << ",\"set\":";
    AppendRuntimeStep(json, report.sceneSet);
    json << ",\"get\":";
    AppendRuntimeStep(json, report.sceneGet);
    json << ",\"roundTripVerified\":";
    AppendBool(json, report.sceneRoundTripVerified);
    json << ",\"roundTripPayloadMatched\":";
    AppendBool(json, report.sceneRoundTripPayloadMatched);
    json << ",\"roundTripMismatchFields\":[";
    for (std::size_t index = 0; index < report.sceneRoundTripMismatchFields.size(); ++index)
    {
        if (index != 0u)
        {
            json << ',';
        }
        AppendString(json, report.sceneRoundTripMismatchFields[index]);
    }
    json << ']';
    json << ",\"clear\":";
    AppendRuntimeStep(json, report.sceneClear);
    json << "},\"diagnostics\":{\"structSizeExpected\":" << report.diagnosticsStructSize
         << ",\"resetExportFound\":";
    AppendBool(json, report.diagnosticsResetExportFound);
    json << ",\"getExportFound\":";
    AppendBool(json, report.diagnosticsGetExportFound);
    json << ",\"reset\":";
    AppendRuntimeStep(json, report.diagnosticsReset);
    json << ",\"get\":";
    AppendRuntimeStep(json, report.diagnosticsGet);
    json << ",\"captured\":";
    AppendBool(json, report.diagnosticsCaptured);
    json << ",\"headerVerified\":";
    AppendBool(json, report.diagnosticsHeaderVerified);
    json << ",\"abiVersion\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.abiVersion;
    }
    else
    {
        json << "null";
    }
    json << ",\"structSize\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.structSize;
    }
    else
    {
        json << "null";
    }
    json << ",\"counters\":{\"effectExecuteCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.effectExecuteCount;
    }
    else
    {
        json << "null";
    }
    json << ",\"framesProcessed\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.framesProcessed;
    }
    else
    {
        json << "null";
    }
    json << ",\"runtimeSceneBlockCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.runtimeSceneBlockCount;
    }
    else
    {
        json << "null";
    }
    json << ",\"authoredFallbackBlockCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.authoredFallbackBlockCount;
    }
    else
    {
        json << "null";
    }
    json << ",\"wetBypassBlockCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.wetBypassBlockCount;
    }
    else
    {
        json << "null";
    }
    json << ",\"geometryDisabledBlockCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.geometryDisabledBlockCount;
    }
    else
    {
        json << "null";
    }
    json << ",\"nonFiniteSampleCount\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.nonFiniteSampleCount;
    }
    else
    {
        json << "null";
    }
    json << "},\"lastRuntimeSceneRevision\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.lastRuntimeSceneRevision;
    }
    else
    {
        json << "null";
    }
    json << ",\"peaks\":{\"lastInput\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.lastInputPeak);
    }
    else
    {
        json << "null";
    }
    json << ",\"maxInput\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.maxInputPeak);
    }
    else
    {
        json << "null";
    }
    json << ",\"lastOutput\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.lastOutputPeak);
    }
    else
    {
        json << "null";
    }
    json << ",\"maxOutput\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.maxOutputPeak);
    }
    else
    {
        json << "null";
    }
    json << ",\"lastWetDifference\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.lastWetDifferencePeak);
    }
    else
    {
        json << "null";
    }
    json << ",\"maxWetDifference\":";
    if (report.diagnosticsCaptured)
    {
        AppendFloat(json, report.diagnostics.maxWetDifferencePeak);
    }
    else
    {
        json << "null";
    }
    json << "},\"lastBlockUsedRuntimeScene\":";
    if (report.diagnosticsCaptured)
    {
        json << report.diagnostics.lastBlockUsedRuntimeScene;
    }
    else
    {
        json << "null";
    }
    json << ",\"assertions\":{\"evaluated\":";
    AppendBool(json, report.diagnosticsAssertionsEvaluated);
    json << ",\"effectExecuteCountPositive\":";
    AppendBool(json, report.effectExecuteCountPositive);
    json << ",\"framesProcessedPositive\":";
    AppendBool(json, report.framesProcessedPositive);
    json << ",\"runtimeSceneBlockCountPositive\":";
    AppendBool(json, report.runtimeSceneBlockCountPositive);
    json << ",\"authoredFallbackBlockCountZero\":";
    AppendBool(json, report.authoredFallbackBlockCountZero);
    json << ",\"runtimeSceneRevisionMatchesPublished\":";
    AppendBool(json, report.runtimeSceneRevisionMatchesPublished);
    json << ",\"nonFiniteSampleCountZero\":";
    AppendBool(json, report.nonFiniteSampleCountZero);
    json << ",\"inputPeakNonSilent\":{\"maxGreaterThan\":";
    AppendFloat(json, kNonSilentPeakThreshold);
    json << ",\"passed\":";
    AppendBool(json, report.inputPeakNonSilent);
    json << "},\"outputPeakNonSilent\":{\"maxGreaterThan\":";
    AppendFloat(json, kNonSilentPeakThreshold);
    json << ",\"passed\":";
    AppendBool(json, report.outputPeakNonSilent);
    json << "},\"wetDifference\":{\"expectation\":";
    AppendString(json, report.options.differenceExpectation);
    json << ",\"changedMaxGreaterThan\":";
    AppendFloat(json, kChangedDifferenceThreshold);
    json << ",\"transparentAbsoluteMaxAtMost\":";
    AppendFloat(json, kTransparentDifferenceTolerance);
    json << ",\"passed\":";
    AppendBool(json, report.wetDifferenceMatchesExpectation);
    json << "},\"reasonCounters\":{\"required\":";
    AppendBool(json, report.reasonAssertionsRequired);
    json << ",\"wetBypassBlockCount\":{\"expectedRelation\":";
    AppendString(json, report.wetBypassCountExpectation);
    json << ",\"passed\":";
    if (report.reasonAssertionsRequired)
    {
        AppendBool(json, report.wetBypassCountMatchesExpectation);
    }
    else
    {
        json << "null";
    }
    json << "},\"geometryDisabledBlockCount\":{\"expectedRelation\":";
    AppendString(json, report.geometryDisabledCountExpectation);
    json << ",\"passed\":";
    if (report.reasonAssertionsRequired)
    {
        AppendBool(json, report.geometryDisabledCountMatchesExpectation);
    }
    else
    {
        json << "null";
    }
    json << "},\"allPassed\":";
    if (report.reasonAssertionsRequired)
    {
        AppendBool(json, report.reasonAssertionsPassed);
    }
    else
    {
        json << "null";
    }
    json << "},\"allPassed\":";
    AppendBool(json, report.diagnosticsAssertionsPassed);
    json << "}},\"gameObjects\":{\"listenerRegistration\":";
    AppendAkStep(json, report.listenerRegistration);
    json << ",\"emitterRegistration\":";
    AppendAkStep(json, report.emitterRegistration);
    json << ",\"listenerPosition\":";
    AppendAkStep(json, report.listenerPosition);
    json << ",\"emitterPosition\":";
    AppendAkStep(json, report.emitterPosition);
    json << ",\"defaultListener\":";
    AppendAkStep(json, report.defaultListener);
    json << "},\"banks\":{\"init\":{\"required\":";
    AppendBool(json, report.initBankRequired);
    json << ",\"present\":";
    AppendBool(json, report.initBankPresent);
    json << ",\"load\":";
    AppendAkStep(json, report.initBankLoad);
    json << ",\"bankId\":" << report.initBankId << ",\"unload\":";
    AppendAkStep(json, report.initBankUnload);
    json << "},\"requested\":{\"load\":";
    AppendAkStep(json, report.bankLoad);
    json << ",\"bankId\":" << report.bankId << ",\"unload\":";
    AppendAkStep(json, report.bankUnload);
    json << "}},\"playback\":{\"postEventAttempted\":";
    AppendBool(json, report.postEventAttempted);
    json << ",\"playingId\":" << report.playingId << ",\"renderCalls\":" << report.renderCalls
         << ",\"renderFailures\":" << report.renderFailures << ",\"lastRenderResult\":";
    if (report.renderCalls > 0)
    {
        json << static_cast<int>(report.lastRenderResult);
    }
    else
    {
        json << "null";
    }
    json << ",\"stopRequested\":";
    AppendBool(json, report.stopRequested);
    json << "},\"termination\":{\"emitterUnregistered\":";
    AppendBool(json, report.emitterUnregistered);
    json << ",\"listenerUnregistered\":";
    AppendBool(json, report.listenerUnregistered);
    json << ",\"soundEngineTerminated\":";
    AppendBool(json, report.soundEngineTerminated);
    json << ",\"ioDeviceTerminated\":";
    AppendBool(json, report.ioDeviceTerminated);
    json << ",\"streamManagerDestroyed\":";
    AppendBool(json, report.streamManagerDestroyed);
    json << ",\"memoryManagerTerminated\":";
    AppendBool(json, report.memoryManagerTerminated);
    json << "},\"exitStatus\":{\"success\":";
    AppendBool(json, report.exitCode == 0);
    json << ",\"code\":" << report.exitCode << ",\"stage\":";
    AppendString(json, report.stage);
    json << ",\"message\":";
    AppendString(json, report.message);
    json << "}}";
    return json.str();
}

void SetAkStep(AkStep& step, const AKRESULT result)
{
    step.attempted = true;
    step.result = result;
    step.success = result == AK_Success;
}

bool ParseDuration(const std::wstring& text, std::uint32_t& durationMs)
{
    try
    {
        std::size_t parsedCharacters = 0;
        const unsigned long value = std::stoul(text, &parsedCharacters, 10);
        if (parsedCharacters != text.size() || value == 0 || value > 3'600'000UL)
        {
            return false;
        }
        durationMs = static_cast<std::uint32_t>(value);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ParseArguments(const int argc, wchar_t* argv[], Options& options, std::string& error)
{
    if (argc == 2 && std::wstring(argv[1]) == L"--help")
    {
        error = "Usage: rwwa_native_host --bank-dir DIR --bank FILE --event NAME --plugin-dir DIR --duration-ms N --report FILE --expect-difference changed|wet-bypass|geometry-disabled|transparent [--scene-json FILE]";
        return false;
    }

    std::vector<std::wstring> seen;
    for (int index = 1; index < argc; index += 2)
    {
        if (index + 1 >= argc)
        {
            error = "Every option requires a value";
            return false;
        }

        const std::wstring name = argv[index];
        const std::wstring value = argv[index + 1];
        if (value.empty())
        {
            error = "Option values must not be empty";
            return false;
        }
        for (const std::wstring& existing : seen)
        {
            if (existing == name)
            {
                error = "Duplicate option: " + WideToUtf8(name);
                return false;
            }
        }
        seen.push_back(name);

        if (name == L"--bank-dir")
        {
            options.bankDir = value;
        }
        else if (name == L"--bank")
        {
            options.bank = value;
        }
        else if (name == L"--event")
        {
            options.event = value;
        }
        else if (name == L"--plugin-dir")
        {
            options.pluginDir = value;
        }
        else if (name == L"--duration-ms")
        {
            if (!ParseDuration(value, options.durationMs))
            {
                error = "--duration-ms must be an integer from 1 through 3600000";
                return false;
            }
        }
        else if (name == L"--report")
        {
            options.report = value;
        }
        else if (name == L"--scene-json")
        {
            options.sceneJson = value;
        }
        else if (name == L"--expect-difference")
        {
            if (value == L"changed" || value == L"wet-bypass" || value == L"geometry-disabled" ||
                value == L"transparent")
            {
                options.differenceExpectation = WideToUtf8(value);
            }
            else
            {
                error =
                    "--expect-difference must be 'changed', 'wet-bypass', 'geometry-disabled', or 'transparent'";
                return false;
            }
        }
        else
        {
            error = "Unknown option: " + WideToUtf8(name);
            return false;
        }
    }

    if (options.bankDir.empty() || options.bank.empty() || options.event.empty() || options.pluginDir.empty() ||
        options.durationMs == 0 || options.report.empty() || options.differenceExpectation.empty())
    {
        error = "Required options: --bank-dir, --bank, --event, --plugin-dir, --duration-ms, --report, --expect-difference";
        return false;
    }
    return true;
}

bool IsSameFileName(const std::wstring& left, const wchar_t* right)
{
    return _wcsicmp(std::filesystem::path(left).filename().c_str(), right) == 0;
}

bool Preflight(HostReport& report)
{
    std::error_code filesystemError;
    const std::filesystem::path bankDir(report.options.bankDir);
    if (!std::filesystem::is_directory(bankDir, filesystemError))
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Bank directory does not exist: " + WideToUtf8(report.options.bankDir);
        return false;
    }

    const std::filesystem::path bankPath = bankDir / report.options.bank;
    if (!std::filesystem::is_regular_file(bankPath, filesystemError))
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Requested SoundBank does not exist: " + WideToUtf8(bankPath.wstring());
        return false;
    }

    report.initBankRequired = !IsSameFileName(report.options.bank, kInitBankName);
    const std::filesystem::path initBankPath = bankDir / kInitBankName;
    report.initBankPresent = std::filesystem::is_regular_file(initBankPath, filesystemError);
    if (report.initBankRequired && !report.initBankPresent)
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Required Init.bnk does not exist beside the requested SoundBank: " +
                         WideToUtf8(initBankPath.wstring());
        return false;
    }

    const std::filesystem::path pluginDir(report.options.pluginDir);
    if (!std::filesystem::is_directory(pluginDir, filesystemError))
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Plugin directory does not exist: " + WideToUtf8(report.options.pluginDir);
        return false;
    }

    const std::filesystem::path pluginPath = pluginDir / (std::wstring(kPluginDllName) + L".dll");
    if (!std::filesystem::is_regular_file(pluginPath, filesystemError))
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Runtime plug-in DLL does not exist: " + WideToUtf8(pluginPath.wstring());
        return false;
    }

    if (!report.options.sceneJson.empty() &&
        !std::filesystem::is_regular_file(std::filesystem::path(report.options.sceneJson), filesystemError))
    {
        report.exitCode = 3;
        report.stage = "preflight";
        report.message = "Scene JSON does not exist: " + WideToUtf8(report.options.sceneJson);
        return false;
    }
    return true;
}

using SceneSetFunction = RWWA_RuntimeStatus(RWWA_RUNTIME_CALL*)(const RWWA_RuntimeSceneV1* scene);
using SceneGetFunction = RWWA_RuntimeStatus(RWWA_RUNTIME_CALL*)(RWWA_RuntimeSceneV1* scene);
using SceneClearFunction = RWWA_RuntimeStatus(RWWA_RUNTIME_CALL*)();
using DiagnosticsResetFunction = RWWA_RuntimeStatus(RWWA_RUNTIME_CALL*)();
using DiagnosticsGetFunction = RWWA_RuntimeStatus(RWWA_RUNTIME_CALL*)(RWWA_RuntimeDiagnosticsV1* diagnostics);

void SetRuntimeStep(RuntimeStep& step, const RWWA_RuntimeStatus status)
{
    step.attempted = true;
    step.status = status;
    step.success = status == RWWA_RUNTIME_STATUS_OK;
}

void EvaluateDiagnosticsAssertions(HostReport& report)
{
    const RWWA_RuntimeDiagnosticsV1& diagnostics = report.diagnostics;
    report.diagnosticsAssertionsEvaluated = true;
    report.effectExecuteCountPositive = diagnostics.effectExecuteCount > 0u;
    report.framesProcessedPositive = diagnostics.framesProcessed > 0u;
    report.runtimeSceneBlockCountPositive = diagnostics.runtimeSceneBlockCount > 0u;
    report.authoredFallbackBlockCountZero = diagnostics.authoredFallbackBlockCount == 0u;
    report.runtimeSceneRevisionMatchesPublished =
        diagnostics.lastRuntimeSceneRevision == report.sceneRevision;
    report.nonFiniteSampleCountZero = diagnostics.nonFiniteSampleCount == 0u;
    report.inputPeakNonSilent =
        std::isfinite(diagnostics.maxInputPeak) && diagnostics.maxInputPeak > kNonSilentPeakThreshold;
    report.outputPeakNonSilent =
        std::isfinite(diagnostics.maxOutputPeak) && diagnostics.maxOutputPeak > kNonSilentPeakThreshold;

    const bool wetDifferenceFinite = std::isfinite(diagnostics.maxWetDifferencePeak);
    if (report.options.differenceExpectation == "changed")
    {
        report.wetDifferenceMatchesExpectation =
            wetDifferenceFinite && diagnostics.maxWetDifferencePeak > kChangedDifferenceThreshold;

        report.reasonAssertionsRequired = true;
        report.wetBypassCountExpectation = "==0";
        report.geometryDisabledCountExpectation = "==0";
        report.wetBypassCountMatchesExpectation = diagnostics.wetBypassBlockCount == 0u;
        report.geometryDisabledCountMatchesExpectation = diagnostics.geometryDisabledBlockCount == 0u;
    }
    else if (report.options.differenceExpectation == "wet-bypass")
    {
        report.wetDifferenceMatchesExpectation =
            wetDifferenceFinite &&
            std::fabs(diagnostics.maxWetDifferencePeak) <= kTransparentDifferenceTolerance;

        report.reasonAssertionsRequired = true;
        report.wetBypassCountExpectation = "==effectExecuteCount";
        report.geometryDisabledCountExpectation = "==0";
        report.wetBypassCountMatchesExpectation =
            diagnostics.wetBypassBlockCount == diagnostics.effectExecuteCount;
        report.geometryDisabledCountMatchesExpectation = diagnostics.geometryDisabledBlockCount == 0u;
    }
    else if (report.options.differenceExpectation == "geometry-disabled")
    {
        report.wetDifferenceMatchesExpectation =
            wetDifferenceFinite &&
            std::fabs(diagnostics.maxWetDifferencePeak) <= kTransparentDifferenceTolerance;

        report.reasonAssertionsRequired = true;
        report.wetBypassCountExpectation = "==0";
        report.geometryDisabledCountExpectation = "==effectExecuteCount";
        report.wetBypassCountMatchesExpectation = diagnostics.wetBypassBlockCount == 0u;
        report.geometryDisabledCountMatchesExpectation =
            diagnostics.geometryDisabledBlockCount == diagnostics.effectExecuteCount;
    }
    else
    {
        report.wetDifferenceMatchesExpectation =
            wetDifferenceFinite &&
            std::fabs(diagnostics.maxWetDifferencePeak) <= kTransparentDifferenceTolerance;
    }

    report.reasonAssertionsPassed = report.reasonAssertionsRequired &&
                                    report.wetBypassCountMatchesExpectation &&
                                    report.geometryDisabledCountMatchesExpectation;
    const bool reasonAssertionsGate = !report.reasonAssertionsRequired || report.reasonAssertionsPassed;

    report.diagnosticsAssertionsPassed =
        report.effectExecuteCountPositive && report.framesProcessedPositive &&
        report.runtimeSceneBlockCountPositive && report.authoredFallbackBlockCountZero &&
        report.runtimeSceneRevisionMatchesPublished && report.nonFiniteSampleCountZero &&
        report.inputPeakNonSilent && report.outputPeakNonSilent && report.wetDifferenceMatchesExpectation &&
        reasonAssertionsGate;
}

bool FindJsonToken(
    const std::string& text,
    const std::string& key,
    std::string& token,
    std::string& error)
{
    const std::string keyText = "\"" + key + "\"";
    const std::size_t keyPosition = text.find(keyText);
    if (keyPosition == std::string::npos)
    {
        error = "Scene JSON is missing field '" + key + "'";
        return false;
    }

    const std::size_t colonPosition = text.find(':', keyPosition + keyText.size());
    if (colonPosition == std::string::npos)
    {
        error = "Scene JSON field '" + key + "' has no value";
        return false;
    }

    std::size_t valueStart = colonPosition + 1;
    while (valueStart < text.size() && std::isspace(static_cast<unsigned char>(text[valueStart])) != 0)
    {
        ++valueStart;
    }
    std::size_t valueEnd = valueStart;
    while (valueEnd < text.size())
    {
        const char character = text[valueEnd];
        if (std::isspace(static_cast<unsigned char>(character)) != 0 || character == ',' || character == '}' ||
            character == ']')
        {
            break;
        }
        ++valueEnd;
    }
    if (valueStart == valueEnd)
    {
        error = "Scene JSON field '" + key + "' has an empty value";
        return false;
    }
    token = text.substr(valueStart, valueEnd - valueStart);
    return true;
}

bool ParseUnsigned64(const std::string& text, const std::string& key, std::uint64_t& value, std::string& error)
{
    std::string token;
    if (!FindJsonToken(text, key, token, error) || token.front() == '-')
    {
        if (error.empty())
        {
            error = "Scene JSON field '" + key + "' must be an unsigned integer";
        }
        return false;
    }
    try
    {
        std::size_t parsed = 0;
        const unsigned long long parsedValue = std::stoull(token, &parsed, 10);
        if (parsed != token.size())
        {
            throw std::invalid_argument("trailing characters");
        }
        value = static_cast<std::uint64_t>(parsedValue);
        return true;
    }
    catch (...)
    {
        error = "Scene JSON field '" + key + "' must be an unsigned integer";
        return false;
    }
}

bool ParseUnsigned32(const std::string& text, const std::string& key, std::uint32_t& value, std::string& error)
{
    std::uint64_t parsedValue = 0;
    if (!ParseUnsigned64(text, key, parsedValue, error) || parsedValue > std::numeric_limits<std::uint32_t>::max())
    {
        if (error.empty())
        {
            error = "Scene JSON field '" + key + "' is outside uint32 range";
        }
        return false;
    }
    value = static_cast<std::uint32_t>(parsedValue);
    return true;
}

bool ParseSigned32(const std::string& text, const std::string& key, std::int32_t& value, std::string& error)
{
    std::string token;
    if (!FindJsonToken(text, key, token, error))
    {
        return false;
    }
    try
    {
        std::size_t parsed = 0;
        const long long parsedValue = std::stoll(token, &parsed, 10);
        if (parsed != token.size() || parsedValue < std::numeric_limits<std::int32_t>::min() ||
            parsedValue > std::numeric_limits<std::int32_t>::max())
        {
            throw std::out_of_range("int32");
        }
        value = static_cast<std::int32_t>(parsedValue);
        return true;
    }
    catch (...)
    {
        error = "Scene JSON field '" + key + "' must be an int32";
        return false;
    }
}

bool ParseFloat(const std::string& text, const std::string& key, float& value, std::string& error)
{
    std::string token;
    if (!FindJsonToken(text, key, token, error))
    {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsedValue = std::strtof(token.c_str(), &end);
    if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(parsedValue))
    {
        error = "Scene JSON field '" + key + "' must be a finite number";
        return false;
    }
    value = parsedValue;
    return true;
}

bool ParseBool32(const std::string& text, const std::string& key, std::uint32_t& value, std::string& error)
{
    std::string token;
    if (!FindJsonToken(text, key, token, error))
    {
        return false;
    }
    if (token == "true" || token == "1")
    {
        value = 1;
        return true;
    }
    if (token == "false" || token == "0")
    {
        value = 0;
        return true;
    }
    error = "Scene JSON field '" + key + "' must be true, false, 0, or 1";
    return false;
}

bool ParseFeature(const std::string& object, RWWA_RuntimeFeatureV1& feature, std::string& error)
{
    return ParseUnsigned64(object, "id", feature.id, error) && ParseFloat(object, "x", feature.x, error) &&
           ParseFloat(object, "y", feature.y, error) && ParseFloat(object, "z", feature.z, error) &&
           ParseFloat(object, "radius", feature.radius, error) &&
           ParseUnsigned32(object, "profile", feature.profile, error) &&
           ParseUnsigned32(object, "mask", feature.mask, error) &&
           ParseSigned32(object, "priority", feature.priority, error);
}

bool ParseFeatures(const std::string& text, RWWA_RuntimeSceneV1& scene, std::string& error)
{
    const std::string keyText = "\"features\"";
    const std::size_t keyPosition = text.find(keyText);
    const std::size_t colonPosition =
        keyPosition == std::string::npos ? std::string::npos : text.find(':', keyPosition + keyText.size());
    const std::size_t arrayStart =
        colonPosition == std::string::npos ? std::string::npos : text.find('[', colonPosition + 1);
    if (arrayStart == std::string::npos)
    {
        error = "Scene JSON is missing the 'features' array";
        return false;
    }

    std::size_t cursor = arrayStart + 1;
    std::uint32_t count = 0;
    while (cursor < text.size())
    {
        while (cursor < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 || text[cursor] == ','))
        {
            ++cursor;
        }
        if (cursor >= text.size())
        {
            break;
        }
        if (text[cursor] == ']')
        {
            scene.featureCount = count;
            return true;
        }
        if (text[cursor] != '{')
        {
            error = "Scene JSON 'features' must contain objects";
            return false;
        }
        if (count >= RWWA_RUNTIME_SCENE_MAX_FEATURES)
        {
            error = "Scene JSON contains more than 8 features";
            return false;
        }

        const std::size_t objectEnd = text.find('}', cursor + 1);
        if (objectEnd == std::string::npos)
        {
            error = "Scene JSON contains an unterminated feature object";
            return false;
        }
        const std::string object = text.substr(cursor, objectEnd - cursor + 1);
        if (!ParseFeature(object, scene.features[count], error))
        {
            error = "Feature " + std::to_string(count) + ": " + error;
            return false;
        }
        ++count;
        cursor = objectEnd + 1;
    }

    error = "Scene JSON contains an unterminated 'features' array";
    return false;
}

RWWA_RuntimeSceneV1 BuiltinScene()
{
    RWWA_RuntimeSceneV1 scene{};
    scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
    scene.structSize = static_cast<std::uint32_t>(sizeof(scene));
    scene.revision = 1;
    scene.valid = 1;
    scene.geometryEnabled = 1;
    scene.rainIntensity = 0.75f;
    scene.windSpeedMetersPerSecond = 20.0f;
    scene.windDirectionRadians = 0.0f;
    scene.windGustiness = 0.35f;
    scene.weatherSeed = 24681357;
    scene.weatherMasterGainLinear = 1.0f;
    scene.featureCount = RWWA_RUNTIME_SCENE_MAX_FEATURES;

    constexpr float kPi = 3.14159265358979323846f;
    for (std::uint32_t index = 0; index < RWWA_RUNTIME_SCENE_MAX_FEATURES; ++index)
    {
        const float angle = (2.0f * kPi * static_cast<float>(index)) /
                            static_cast<float>(RWWA_RUNTIME_SCENE_MAX_FEATURES);
        RWWA_RuntimeFeatureV1& feature = scene.features[index];
        feature.id = 1000u + index;
        feature.x = std::sin(angle) * (6.0f + static_cast<float>(index));
        feature.y = 0.0f;
        feature.z = std::cos(angle) * (6.0f + static_cast<float>(index));
        feature.radius = 2.5f + 0.25f * static_cast<float>(index);
        feature.profile = index % (RWWA_RUNTIME_PROFILE_MAX + 1u);
        feature.mask = index % 3u + 1u;
        feature.priority = 100 - static_cast<std::int32_t>(index) * 10;
    }
    return scene;
}

bool LoadScene(const Options& options, RWWA_RuntimeSceneV1& scene, HostReport& report, std::string& error)
{
    if (options.sceneJson.empty())
    {
        scene = BuiltinScene();
        report.sceneSource = "builtin-eight-feature-test-scene";
    }
    else
    {
        const std::filesystem::path path(options.sceneJson);
        report.sceneSource = WideToUtf8(path.wstring());
        std::error_code filesystemError;
        const std::uintmax_t fileSize = std::filesystem::file_size(path, filesystemError);
        if (filesystemError || fileSize > 1024u * 1024u)
        {
            error = filesystemError ? "Could not inspect Scene JSON: " + filesystemError.message()
                                    : "Scene JSON exceeds the 1 MiB limit";
            return false;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "Could not open Scene JSON: " + WideToUtf8(path.wstring());
            return false;
        }
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const std::size_t first = text.find_first_not_of(" \t\r\n");
        const std::size_t last = text.find_last_not_of(" \t\r\n");
        if (first == std::string::npos || text[first] != '{' || text[last] != '}')
        {
            error = "Scene JSON root must be an object";
            return false;
        }

        scene = {};
        scene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
        scene.structSize = static_cast<std::uint32_t>(sizeof(scene));
        if (!ParseUnsigned64(text, "revision", scene.revision, error) ||
            !ParseBool32(text, "valid", scene.valid, error) ||
            !ParseBool32(text, "geometryEnabled", scene.geometryEnabled, error) ||
            !ParseFloat(text, "listenerX", scene.listenerX, error) ||
            !ParseFloat(text, "listenerY", scene.listenerY, error) ||
            !ParseFloat(text, "listenerZ", scene.listenerZ, error) ||
            !ParseFloat(text, "listenerYawRadians", scene.listenerYawRadians, error) ||
            !ParseFloat(text, "rainIntensity", scene.rainIntensity, error) ||
            !ParseFloat(text, "windSpeedMetersPerSecond", scene.windSpeedMetersPerSecond, error) ||
            !ParseFloat(text, "windDirectionRadians", scene.windDirectionRadians, error) ||
            !ParseFloat(text, "windGustiness", scene.windGustiness, error) ||
            !ParseUnsigned32(text, "weatherSeed", scene.weatherSeed, error) ||
            !ParseFloat(text, "weatherMasterGainLinear", scene.weatherMasterGainLinear, error) ||
            !ParseFeatures(text, scene, error))
        {
            return false;
        }
    }

    report.scenePrepared = true;
    report.sceneRevision = scene.revision;
    report.sceneFeatureCount = scene.featureCount;
    return true;
}

AkSoundPosition OriginPosition()
{
    AkVector position{0.0f, 0.0f, 0.0f};
    AkVector front{0.0f, 0.0f, 1.0f};
    AkVector top{0.0f, 1.0f, 0.0f};
    AkSoundPosition soundPosition;
    soundPosition.Set(position, front, top);
    return soundPosition;
}

void RunHost(HostReport& report)
{
    bool memoryInitialized = false;
    bool streamManagerCreated = false;
    bool ioInitialized = false;
    bool soundEngineInitialized = false;
    bool listenerRegistered = false;
    bool emitterRegistered = false;
    bool initBankLoaded = false;
    bool bankLoaded = false;
    bool scenePublished = false;
    SceneClearFunction sceneClearFunction = nullptr;
    DiagnosticsResetFunction diagnosticsResetFunction = nullptr;
    DiagnosticsGetFunction diagnosticsGetFunction = nullptr;
    CAkDefaultIOHookDeferred ioHook;

    do
    {
        AkMemSettings memorySettings;
        AK::MemoryMgr::GetDefaultSettings(memorySettings);
        SetAkStep(report.memoryManager, AK::MemoryMgr::Init(&memorySettings));
        if (!report.memoryManager.success)
        {
            report.exitCode = 10;
            report.stage = "memory-manager-init";
            report.message = "AK::MemoryMgr::Init failed";
            break;
        }
        memoryInitialized = true;

        AkStreamMgrSettings streamSettings;
        AK::StreamMgr::GetDefaultSettings(streamSettings);
        report.streamManagerAttempted = true;
        streamManagerCreated = AK::StreamMgr::Create(streamSettings) != nullptr;
        report.streamManagerCreated = streamManagerCreated;
        if (!streamManagerCreated)
        {
            report.exitCode = 11;
            report.stage = "stream-manager-init";
            report.message = "AK::StreamMgr::Create failed";
            break;
        }

        AkDeviceSettings deviceSettings;
        AK::StreamMgr::GetDefaultDeviceSettings(deviceSettings);
        deviceSettings.bUseStreamCache = true;
        SetAkStep(report.ioDevice, ioHook.Init(deviceSettings));
        if (!report.ioDevice.success)
        {
            report.exitCode = 12;
            report.stage = "io-device-init";
            report.message = "CAkDefaultIOHookDeferred::Init failed";
            break;
        }
        ioInitialized = true;

        AkInitSettings initSettings;
        AkPlatformInitSettings platformSettings;
        AK::SoundEngine::GetDefaultInitSettings(initSettings);
        AK::SoundEngine::GetDefaultPlatformInitSettings(platformSettings);
        SetAkStep(report.soundEngine, AK::SoundEngine::Init(&initSettings, &platformSettings));
        if (!report.soundEngine.success)
        {
            report.exitCode = 13;
            report.stage = "sound-engine-init";
            report.message = "AK::SoundEngine::Init failed";
            break;
        }
        soundEngineInitialized = true;

        SetAkStep(report.bankPath, ioHook.SetBasePath(report.options.bankDir.c_str()));
        if (!report.bankPath.success)
        {
            report.exitCode = 14;
            report.stage = "bank-path";
            report.message = "CAkDefaultIOHookDeferred::SetBasePath failed";
            break;
        }

        SetAkStep(report.language, AK::StreamMgr::SetCurrentLanguage(AKTEXT("English(US)")));
        if (!report.language.success)
        {
            report.exitCode = 15;
            report.stage = "language";
            report.message = "AK::StreamMgr::SetCurrentLanguage failed";
            break;
        }

        SetAkStep(
            report.pluginDll,
            AK::SoundEngine::RegisterPluginDLL(kPluginDllName, report.options.pluginDir.c_str()));
        if (!report.pluginDll.success)
        {
            report.exitCode = 20;
            report.stage = "plugin-dll-registration";
            report.message = "AK::SoundEngine::RegisterPluginDLL failed";
            break;
        }

        report.sourcePluginRegistered =
            AK::SoundEngine::IsPluginRegistered(AkPluginTypeSource, kCompanyId, kSourcePluginId);
        report.effectPluginRegistered =
            AK::SoundEngine::IsPluginRegistered(AkPluginTypeEffect, kCompanyId, kEffectPluginId);
        if (!report.sourcePluginRegistered || !report.effectPluginRegistered)
        {
            report.exitCode = 21;
            report.stage = "plugin-verification";
            report.message = "The runtime DLL did not register both Source 31001 and Effect 31002";
            break;
        }

        RWWA_RuntimeSceneV1 scene{};
        std::string sceneError;
        if (!LoadScene(report.options, scene, report, sceneError))
        {
            report.exitCode = 22;
            report.stage = "scene-input";
            report.message = sceneError;
            break;
        }

        const HMODULE pluginModule = GetModuleHandleW(L"RealWorldWeatherAcoustics.dll");
        report.sceneDllModuleFound = pluginModule != nullptr;
        if (pluginModule == nullptr)
        {
            report.exitCode = 23;
            report.stage = "scene-api-probe";
            report.message = "The registered runtime plug-in DLL module could not be found";
            break;
        }

        const auto sceneSetFunction = reinterpret_cast<SceneSetFunction>(
            GetProcAddress(pluginModule, "RWWA_RuntimeScene_SetV1"));
        const auto sceneGetFunction = reinterpret_cast<SceneGetFunction>(
            GetProcAddress(pluginModule, "RWWA_RuntimeScene_GetV1"));
        sceneClearFunction = reinterpret_cast<SceneClearFunction>(
            GetProcAddress(pluginModule, "RWWA_RuntimeScene_ClearV1"));
        diagnosticsResetFunction = reinterpret_cast<DiagnosticsResetFunction>(
            GetProcAddress(pluginModule, "RWWA_RuntimeDiagnostics_ResetV1"));
        diagnosticsGetFunction = reinterpret_cast<DiagnosticsGetFunction>(
            GetProcAddress(pluginModule, "RWWA_RuntimeDiagnostics_GetV1"));
        report.sceneSetExportFound = sceneSetFunction != nullptr;
        report.sceneGetExportFound = sceneGetFunction != nullptr;
        report.sceneClearExportFound = sceneClearFunction != nullptr;
        report.diagnosticsResetExportFound = diagnosticsResetFunction != nullptr;
        report.diagnosticsGetExportFound = diagnosticsGetFunction != nullptr;
        if (sceneSetFunction == nullptr || sceneGetFunction == nullptr || sceneClearFunction == nullptr)
        {
            report.exitCode = 23;
            report.stage = "scene-api-probe";
            report.message = "The runtime plug-in DLL does not export SetV1/GetV1/ClearV1";
            break;
        }
        if (diagnosticsResetFunction == nullptr || diagnosticsGetFunction == nullptr)
        {
            report.exitCode = 27;
            report.stage = "diagnostics-api-probe";
            report.message = "The runtime plug-in DLL does not export Diagnostics ResetV1/GetV1";
            break;
        }

        SetRuntimeStep(report.sceneSet, sceneSetFunction(&scene));
        scenePublished = report.sceneSet.success;
        if (!scenePublished)
        {
            report.exitCode = 24;
            report.stage = "scene-submit";
            report.message = "RWWA_RuntimeScene_SetV1 rejected the scene";
            break;
        }

        RWWA_RuntimeSceneV1 roundTripScene{};
        roundTripScene.abiVersion = RWWA_RUNTIME_SCENE_ABI_VERSION;
        roundTripScene.structSize = static_cast<std::uint32_t>(sizeof(roundTripScene));
        SetRuntimeStep(report.sceneGet, sceneGetFunction(&roundTripScene));
        if (report.sceneGet.success)
        {
            report.sceneRoundTripMismatchFields =
                rwwa::native_host::FindScenePayloadMismatches(scene, roundTripScene);
            report.sceneRoundTripPayloadMatched = report.sceneRoundTripMismatchFields.empty();
        }
        report.sceneRoundTripVerified = report.sceneGet.success && report.sceneRoundTripPayloadMatched;
        if (!report.sceneRoundTripVerified)
        {
            report.exitCode = 25;
            report.stage = "scene-round-trip";
            report.message = report.sceneRoundTripMismatchFields.empty()
                                 ? "RWWA_RuntimeScene_GetV1 failed before full-payload comparison"
                                 : "RWWA Runtime Scene roundtrip payload mismatch at " +
                                       report.sceneRoundTripMismatchFields.front();
            break;
        }

        SetAkStep(report.listenerRegistration, AK::SoundEngine::RegisterGameObj(kListenerId, "Native Host Listener"));
        listenerRegistered = report.listenerRegistration.success;
        if (!listenerRegistered)
        {
            report.exitCode = 30;
            report.stage = "listener-registration";
            report.message = "AK::SoundEngine::RegisterGameObj failed for the listener";
            break;
        }

        SetAkStep(report.emitterRegistration, AK::SoundEngine::RegisterGameObj(kEmitterId, "Native Host Emitter"));
        emitterRegistered = report.emitterRegistration.success;
        if (!emitterRegistered)
        {
            report.exitCode = 30;
            report.stage = "emitter-registration";
            report.message = "AK::SoundEngine::RegisterGameObj failed for the emitter";
            break;
        }

        const AkSoundPosition origin = OriginPosition();
        SetAkStep(report.listenerPosition, AK::SoundEngine::SetPosition(kListenerId, origin));
        SetAkStep(report.emitterPosition, AK::SoundEngine::SetPosition(kEmitterId, origin));
        SetAkStep(report.defaultListener, AK::SoundEngine::SetDefaultListeners(&kListenerId, 1));
        if (!report.listenerPosition.success || !report.emitterPosition.success || !report.defaultListener.success)
        {
            report.exitCode = 31;
            report.stage = "game-object-positioning";
            report.message = "Listener/emitter positioning failed";
            break;
        }

        if (report.initBankRequired)
        {
            SetAkStep(report.initBankLoad, AK::SoundEngine::LoadBank(kInitBankName, report.initBankId));
            initBankLoaded = report.initBankLoad.success;
            if (!initBankLoaded)
            {
                report.exitCode = 40;
                report.stage = "init-bank-load";
                report.message = "AK::SoundEngine::LoadBank failed for Init.bnk";
                break;
            }
        }

        SetAkStep(report.bankLoad, AK::SoundEngine::LoadBank(report.options.bank.c_str(), report.bankId));
        bankLoaded = report.bankLoad.success;
        if (!bankLoaded)
        {
            report.exitCode = 41;
            report.stage = "bank-load";
            report.message = "AK::SoundEngine::LoadBank failed for the requested bank";
            break;
        }

        SetRuntimeStep(report.diagnosticsReset, diagnosticsResetFunction());
        if (!report.diagnosticsReset.success)
        {
            report.exitCode = 28;
            report.stage = "diagnostics-reset";
            report.message = "RWWA_RuntimeDiagnostics_ResetV1 failed before PostEvent";
            break;
        }

        report.postEventAttempted = true;
        report.playingId = AK::SoundEngine::PostEvent(report.options.event.c_str(), kEmitterId);
        if (report.playingId == AK_INVALID_PLAYING_ID)
        {
            report.exitCode = 50;
            report.stage = "post-event";
            report.message = "AK::SoundEngine::PostEvent returned AK_INVALID_PLAYING_ID";
            break;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(report.options.durationMs);
        do
        {
            report.lastRenderResult = AK::SoundEngine::RenderAudio();
            ++report.renderCalls;
            if (report.lastRenderResult != AK_Success)
            {
                ++report.renderFailures;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        } while (std::chrono::steady_clock::now() < deadline);

        if (report.renderFailures != 0)
        {
            report.exitCode = 51;
            report.stage = "render-audio";
            report.message = "AK::SoundEngine::RenderAudio failed";
            break;
        }

        report.exitCode = 0;
        report.stage = "complete";
        report.message = "Native SoundEngine Host completed successfully";
    } while (false);

    if (soundEngineInitialized && report.playingId != AK_INVALID_PLAYING_ID)
    {
        AK::SoundEngine::StopPlayingID(report.playingId, 0);
        report.stopRequested = true;
        const AKRESULT drainResult = AK::SoundEngine::RenderAudio();
        report.lastRenderResult = drainResult;
        ++report.renderCalls;
        if (drainResult != AK_Success)
        {
            ++report.renderFailures;
            if (report.exitCode == 0)
            {
                report.exitCode = 51;
                report.stage = "stop-render-audio";
                report.message = "Final AK::SoundEngine::RenderAudio failed";
            }
        }
    }

    if (report.diagnosticsReset.success && diagnosticsGetFunction != nullptr &&
        report.playingId != AK_INVALID_PLAYING_ID)
    {
        report.diagnostics = {};
        report.diagnostics.abiVersion = RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION;
        report.diagnostics.structSize = static_cast<std::uint32_t>(sizeof(report.diagnostics));
        SetRuntimeStep(report.diagnosticsGet, diagnosticsGetFunction(&report.diagnostics));
        report.diagnosticsCaptured = report.diagnosticsGet.success;
        report.diagnosticsHeaderVerified =
            report.diagnosticsCaptured &&
            report.diagnostics.abiVersion == RWWA_RUNTIME_DIAGNOSTICS_ABI_VERSION &&
            report.diagnostics.structSize >= sizeof(RWWA_RuntimeDiagnosticsV1);

        if (!report.diagnosticsHeaderVerified)
        {
            if (report.exitCode == 0)
            {
                report.exitCode = 29;
                report.stage = "diagnostics-get";
                report.message = report.diagnosticsGet.success
                                     ? "RWWA_RuntimeDiagnostics_GetV1 returned an incompatible result header"
                                     : "RWWA_RuntimeDiagnostics_GetV1 failed after playback";
            }
        }
        else
        {
            EvaluateDiagnosticsAssertions(report);
            if (!report.diagnosticsAssertionsPassed && report.exitCode == 0)
            {
                report.exitCode = 52;
                report.stage = "diagnostics-assertions";
                report.message = "One or more runtime diagnostics assertions failed";
            }
        }
    }

    if (soundEngineInitialized && bankLoaded)
    {
        SetAkStep(report.bankUnload, AK::SoundEngine::UnloadBank(report.bankId, nullptr));
        if (!report.bankUnload.success && report.exitCode == 0)
        {
            report.exitCode = 42;
            report.stage = "bank-unload";
            report.message = "AK::SoundEngine::UnloadBank failed for the requested bank";
        }
    }
    if (soundEngineInitialized && initBankLoaded)
    {
        SetAkStep(report.initBankUnload, AK::SoundEngine::UnloadBank(report.initBankId, nullptr));
        if (!report.initBankUnload.success && report.exitCode == 0)
        {
            report.exitCode = 43;
            report.stage = "init-bank-unload";
            report.message = "AK::SoundEngine::UnloadBank failed for Init.bnk";
        }
    }
    if (scenePublished && sceneClearFunction != nullptr)
    {
        SetRuntimeStep(report.sceneClear, sceneClearFunction());
        if (!report.sceneClear.success && report.exitCode == 0)
        {
            report.exitCode = 26;
            report.stage = "scene-clear";
            report.message = "RWWA_RuntimeScene_ClearV1 failed";
        }
    }
    if (soundEngineInitialized && emitterRegistered)
    {
        report.emitterUnregistered = AK::SoundEngine::UnregisterGameObj(kEmitterId) == AK_Success;
    }
    if (soundEngineInitialized && listenerRegistered)
    {
        report.listenerUnregistered = AK::SoundEngine::UnregisterGameObj(kListenerId) == AK_Success;
    }
    if (((emitterRegistered && !report.emitterUnregistered) ||
         (listenerRegistered && !report.listenerUnregistered)) &&
        report.exitCode == 0)
    {
        report.exitCode = 60;
        report.stage = "game-object-cleanup";
        report.message = "One or more registered game objects could not be unregistered";
    }
    if (soundEngineInitialized)
    {
        AK::SoundEngine::Term();
        report.soundEngineTerminated = true;
    }
    if (ioInitialized)
    {
        ioHook.Term();
        report.ioDeviceTerminated = true;
    }
    if (streamManagerCreated && AK::IAkStreamMgr::Get() != nullptr)
    {
        AK::IAkStreamMgr::Get()->Destroy();
        report.streamManagerDestroyed = true;
    }
    if (memoryInitialized && AK::MemoryMgr::IsInitialized())
    {
        AK::MemoryMgr::Term();
        report.memoryManagerTerminated = true;
    }
}

bool WriteReportFile(const std::wstring& reportPath, const std::string& json, std::string& error)
{
    std::error_code filesystemError;
    const std::filesystem::path path(reportPath);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, filesystemError);
        if (filesystemError)
        {
            error = "Could not create report directory: " + filesystemError.message();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not open report file: " + WideToUtf8(path.wstring());
        return false;
    }
    output << json << '\n';
    if (!output)
    {
        error = "Could not write report file: " + WideToUtf8(path.wstring());
        return false;
    }
    return true;
}
} // namespace

int wmain(const int argc, wchar_t* argv[])
{
    HostReport report;
    std::string argumentError;
    if (!ParseArguments(argc, argv, report.options, argumentError))
    {
        report.exitCode = 2;
        report.stage = "arguments";
        report.message = argumentError;
    }
    else if (Preflight(report))
    {
        RunHost(report);
    }

    std::string json = SerializeReport(report);
    if (!report.options.report.empty())
    {
        std::string reportError;
        if (!WriteReportFile(report.options.report, json, reportError))
        {
            report.exitCode = 60;
            report.stage = "report-write";
            report.message = reportError;
            json = SerializeReport(report);
        }
    }

    std::cout << json << std::endl;
    return report.exitCode;
}
