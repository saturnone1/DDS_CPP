cmake_minimum_required(VERSION 3.20)
project(DDSClientConsumerSmoke LANGUAGES CXX)

find_package(DDSClient CONFIG REQUIRED)
find_path(DDSCLIENT_TEST_DEFINITIONS qos_profiles.xml
    PATHS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES share/dds_cpp/definitions
    NO_DEFAULT_PATH REQUIRED)
add_executable(ddsclient_consumer main.cpp)
target_compile_definitions(ddsclient_consumer PRIVATE
    DDSCLIENT_TEST_DEFINITIONS="${DDSCLIENT_TEST_DEFINITIONS}")
target_compile_features(ddsclient_consumer PRIVATE cxx_std_20)
target_link_libraries(ddsclient_consumer PRIVATE DDSClient::DDSSim DDSClient::Telemetry)

if(TARGET DDSClient::Diagnostics)
    target_link_libraries(ddsclient_consumer PRIVATE DDSClient::Diagnostics)
    target_compile_definitions(ddsclient_consumer PRIVATE DDSCLIENT_TEST_AUTO_MODULE=1)
endif()
if(TARGET DDSClient::NetworkA AND TARGET DDSClient::NetworkB)
    target_sources(ddsclient_consumer PRIVATE network_isolation.cpp)
    target_link_libraries(ddsclient_consumer PRIVATE DDSClient::NetworkA DDSClient::NetworkB)
    target_compile_definitions(ddsclient_consumer PRIVATE DDSCLIENT_TEST_NETWORK_ISOLATION=1)
endif()
