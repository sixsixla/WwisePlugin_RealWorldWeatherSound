cmake_minimum_required(VERSION 3.20)

project(RealWorldWeatherAcousticsCore VERSION 0.2.0 LANGUAGES CXX)

option(RWWA_BUILD_TESTS "Build the dependency-free core tests" ON)
option(RWWA_BUILD_OFFLINE_RENDERER "Build the deterministic WAV renderer" ON)
option(RWWA_BUILD_WWISE_BANK_TESTS "Build the Wwise SourceParams bank-contract regression test" OFF)
set(RWWA_WWISE_SDK_ROOT "" CACHE PATH "Wwise installation root used by the SourceParams ABI regression test")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${configuration_upper} "${CMAKE_BINARY_DIR}/lib/${configuration}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${configuration_upper} "${CMAKE_BINARY_DIR}/lib/${configuration}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${CMAKE_BINARY_DIR}/bin/${configuration}")
endforeach()

add_library(rwwa_core STATIC
    Core/src/WeatherAcousticsCore.cpp)
add_library(rwwa::core ALIAS rwwa_core)

target_include_directories(rwwa_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/include")
target_compile_features(rwwa_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(rwwa_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(rwwa_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(RWWA_BUILD_OFFLINE_RENDERER)
    add_executable(rwwa_offline_renderer
        Tools/OfflineRenderer/main.cpp)
    target_link_libraries(rwwa_offline_renderer PRIVATE rwwa::core)
    target_compile_features(rwwa_offline_renderer PRIVATE cxx_std_17)
endif()

if(RWWA_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(rwwa_core_tests
        Tests/WeatherAcousticsCoreTests.cpp)
    target_link_libraries(rwwa_core_tests PRIVATE rwwa::core)
    target_compile_features(rwwa_core_tests PRIVATE cxx_std_17)

    add_test(NAME rwwa_core_tests COMMAND rwwa_core_tests)

    if(RWWA_BUILD_WWISE_BANK_TESTS)
        if(NOT MSVC)
            message(FATAL_ERROR "RWWA_BUILD_WWISE_BANK_TESTS currently supports the Windows/MSVC Wwise 2023 target only")
        endif()
        if(NOT RWWA_WWISE_SDK_ROOT)
            message(FATAL_ERROR "RWWA_WWISE_SDK_ROOT is required for the SourceParams ABI regression test on MSVC")
        endif()
        set(rwwa_wwise_sdk_include "${RWWA_WWISE_SDK_ROOT}/SDK/include")
        if(NOT EXISTS "${rwwa_wwise_sdk_include}/AK/SoundEngine/Common/IAkPlugin.h")
            message(FATAL_ERROR "RWWA_WWISE_SDK_ROOT does not contain the Wwise SDK headers: ${RWWA_WWISE_SDK_ROOT}")
        endif()

        add_executable(rwwa_source_params_bank_tests
            Tests/SourceParamsBankContractTests.cpp
            RealWorldWeatherAcoustics/SoundEnginePlugin/RealWorldWeatherAcousticsSourceParams.cpp)
        target_include_directories(rwwa_source_params_bank_tests
            PRIVATE
                "${rwwa_wwise_sdk_include}"
                "${CMAKE_CURRENT_SOURCE_DIR}/RealWorldWeatherAcoustics/SoundEnginePlugin")
        target_compile_features(rwwa_source_params_bank_tests PRIVATE cxx_std_17)
        target_compile_options(rwwa_source_params_bank_tests PRIVATE /W4 /permissive-)
        target_compile_definitions(rwwa_source_params_bank_tests PRIVATE WIN64 WIN32_LEAN_AND_MEAN)

        add_test(NAME rwwa_source_params_bank_tests COMMAND rwwa_source_params_bank_tests)
    endif()

    if(RWWA_BUILD_OFFLINE_RENDERER)
        add_test(
            NAME rwwa_offline_renderer_weather_ring
            COMMAND rwwa_offline_renderer --preset weather-ring --output "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/weather_ring.wav")
        add_test(
            NAME rwwa_offline_renderer_open_wind
            COMMAND rwwa_offline_renderer --preset open-wind --output "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/open_wind.wav")
        add_test(
            NAME rwwa_offline_renderer_rain_metal
            COMMAND rwwa_offline_renderer --preset rain-metal --output "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/rain_metal.wav")
    endif()
endif()
