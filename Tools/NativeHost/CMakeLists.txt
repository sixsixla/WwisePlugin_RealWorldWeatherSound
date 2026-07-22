cmake_minimum_required(VERSION 3.20)

project(RealWorldWeatherAcousticsNativeHost LANGUAGES CXX)

enable_testing()

if(NOT MSVC)
    message(FATAL_ERROR "The Native Host supports Windows/MSVC only")
endif()

set(RWWA_WWISE_ROOT "" CACHE PATH "Wwise 2023.1 installation root")
if(NOT RWWA_WWISE_ROOT)
    message(FATAL_ERROR "RWWA_WWISE_ROOT is required")
endif()

set(RWWA_WWISE_SDK "${RWWA_WWISE_ROOT}/SDK")
set(RWWA_WWISE_INCLUDE "${RWWA_WWISE_SDK}/include")
set(RWWA_WWISE_SAMPLE_COMMON "${RWWA_WWISE_SDK}/samples/SoundEngine/Common")
set(RWWA_WWISE_SAMPLE_WIN32 "${RWWA_WWISE_SDK}/samples/SoundEngine/Win32")
set(RWWA_WWISE_LIB_DIR "${RWWA_WWISE_SDK}/x64_vc170/Release/lib")
set(RWWA_RUNTIME_API_DIR "${CMAKE_CURRENT_LIST_DIR}/../../RealWorldWeatherAcoustics/SoundEnginePlugin")

foreach(required_path
        "${RWWA_WWISE_INCLUDE}/AK/SoundEngine/Common/AkSoundEngine.h"
        "${RWWA_WWISE_SAMPLE_WIN32}/AkDefaultIOHookDeferred.cpp"
        "${RWWA_WWISE_SAMPLE_COMMON}/AkMultipleFileLocation.cpp"
        "${RWWA_WWISE_SAMPLE_COMMON}/AkGeneratedSoundBanksResolver.cpp"
        "${RWWA_WWISE_LIB_DIR}/AkSoundEngine.lib"
        "${RWWA_RUNTIME_API_DIR}/RealWorldWeatherAcousticsRuntimeAPI.h")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Wwise SDK input is missing: ${required_path}")
    endif()
endforeach()

add_executable(rwwa_native_host
    main.cpp
    "${RWWA_WWISE_SAMPLE_WIN32}/AkDefaultIOHookDeferred.cpp"
    "${RWWA_WWISE_SAMPLE_COMMON}/AkMultipleFileLocation.cpp"
    "${RWWA_WWISE_SAMPLE_COMMON}/AkGeneratedSoundBanksResolver.cpp")

target_compile_features(rwwa_native_host PRIVATE cxx_std_17)
target_compile_definitions(rwwa_native_host PRIVATE
    AK_OPTIMIZED
    WIN64
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)
target_compile_options(rwwa_native_host PRIVATE /W4 /permissive- /utf-8)
target_include_directories(rwwa_native_host PRIVATE
    "${RWWA_WWISE_INCLUDE}"
    "${RWWA_WWISE_SAMPLE_WIN32}"
    "${RWWA_WWISE_SAMPLE_COMMON}"
    "${RWWA_RUNTIME_API_DIR}")
target_link_directories(rwwa_native_host PRIVATE "${RWWA_WWISE_LIB_DIR}")
target_link_libraries(rwwa_native_host PRIVATE
    AkSoundEngine
    AkMemoryMgr
    AkStreamMgr
    AkVorbisDecoder
    AkOpusDecoder
    ws2_32
    dinput8
    dsound
    msacm32
    dbghelp
    winmm
    d3d11)

set_target_properties(rwwa_native_host PROPERTIES
    OUTPUT_NAME "rwwa_native_host"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(rwwa_native_host_scene_payload_tests
    ScenePayloadComparisonTests.cpp)
target_compile_features(rwwa_native_host_scene_payload_tests PRIVATE cxx_std_17)
target_compile_definitions(rwwa_native_host_scene_payload_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(rwwa_native_host_scene_payload_tests PRIVATE /W4 /permissive- /utf-8)
target_include_directories(rwwa_native_host_scene_payload_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${RWWA_RUNTIME_API_DIR}")

add_test(
    NAME rwwa_native_host_scene_payload_tests
    COMMAND rwwa_native_host_scene_payload_tests)
