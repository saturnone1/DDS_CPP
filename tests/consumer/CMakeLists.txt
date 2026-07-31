cmake_minimum_required(VERSION 3.20)
project(DDSClientConsumerSmoke LANGUAGES CXX)

find_package(DDSClient CONFIG REQUIRED)
add_executable(ddsclient_consumer main.cpp)
target_compile_features(ddsclient_consumer PRIVATE cxx_std_20)
target_link_libraries(ddsclient_consumer PRIVATE DDSClient::dds_cpp)
