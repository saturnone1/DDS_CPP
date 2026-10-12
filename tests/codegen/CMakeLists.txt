cmake_minimum_required(VERSION 3.20)
project(DDSCodegenRegression LANGUAGES CXX)
find_package(DDSClient CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(codegen_regression main.cpp)
target_compile_features(codegen_regression PRIVATE cxx_std_20)
target_link_libraries(codegen_regression PRIVATE DDSClient::dds_cpp)
foreach(name IN ITEMS Nested GlobalA GlobalB Keyword)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/codegen/schema_modules.py"
        --xml "${ROOT}/DDS_Nuget/tests/fixtures/${name}.xml" --output "${output}"
        --language C++11 --rti-home "${DDSClient_RTI_HOME}" COMMAND_ERROR_IS_FATAL ANY)
    target_sources(codegen_regression PRIVATE "${output}/Generated/${name}.cxx" "${output}/Generated/${name}Plugin.cxx"
        "${output}/${name}_messages.cpp")
    target_include_directories(codegen_regression PRIVATE "${output}" "${output}/Generated")
endforeach()
enable_testing()
add_test(NAME codegen_regression COMMAND codegen_regression)
