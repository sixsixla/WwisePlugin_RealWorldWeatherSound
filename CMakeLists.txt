cmake_minimum_required(VERSION 3.20)

project(RealWorldWeatherAcousticsCore VERSION 0.1.0 LANGUAGES CXX)

option(RWWA_BUILD_TESTS "Build the dependency-free core tests" ON)
option(RWWA_BUILD_OFFLINE_RENDERER "Build the deterministic WAV renderer" ON)

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
    if(RWWA_BUILD_OFFLINE_RENDERER)
        add_test(
            NAME rwwa_offline_renderer_writes_wav
            COMMAND rwwa_offline_renderer "${CMAKE_CURRENT_BINARY_DIR}/Fixtures/rain_preview.wav")
    endif()
endif()
