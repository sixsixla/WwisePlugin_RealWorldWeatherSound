# CMake generated Testfile for 
# Source directory: D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost
# Build directory: D:/Tool/WwisePlugin_RealWorldWeatherSound/Build/NativeHost
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[rwwa_native_host_scene_payload_tests]=] "D:/Tool/WwisePlugin_RealWorldWeatherSound/Build/NativeHost/Debug/rwwa_native_host_scene_payload_tests.exe")
  set_tests_properties([=[rwwa_native_host_scene_payload_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;85;add_test;D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[rwwa_native_host_scene_payload_tests]=] "D:/Tool/WwisePlugin_RealWorldWeatherSound/Build/NativeHost/Release/rwwa_native_host_scene_payload_tests.exe")
  set_tests_properties([=[rwwa_native_host_scene_payload_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;85;add_test;D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[rwwa_native_host_scene_payload_tests]=] "D:/Tool/WwisePlugin_RealWorldWeatherSound/Build/NativeHost/MinSizeRel/rwwa_native_host_scene_payload_tests.exe")
  set_tests_properties([=[rwwa_native_host_scene_payload_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;85;add_test;D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[rwwa_native_host_scene_payload_tests]=] "D:/Tool/WwisePlugin_RealWorldWeatherSound/Build/NativeHost/RelWithDebInfo/rwwa_native_host_scene_payload_tests.exe")
  set_tests_properties([=[rwwa_native_host_scene_payload_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;85;add_test;D:/Tool/WwisePlugin_RealWorldWeatherSound/Tools/NativeHost/CMakeLists.txt;0;")
else()
  add_test([=[rwwa_native_host_scene_payload_tests]=] NOT_AVAILABLE)
endif()
