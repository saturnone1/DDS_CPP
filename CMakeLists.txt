cmake_minimum_required(VERSION 3.20)

project(DdsAmbassadorDDSCPP VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    list(APPEND CMAKE_VS_GLOBALS
        "ForceImportBeforeCppTargets=${CMAKE_CURRENT_SOURCE_DIR}/../tools/build/utf8.props")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/install" CACHE PATH "Install output directory" FORCE)
endif()

option(DDSCPP_BUILD_MODULES "Generate independent modules for every message XML." ON)
option(DDSCPP_BUILD_STATIC "Build the DDSCPP static library." ON)
option(DDSCPP_BUILD_SHARED "Build the DDSCPP shared library." ON)
option(DDSCPP_BUILD_TESTS "Build explicit sample-dependent tests." OFF)
option(DDSCPP_BUILD_CLI "Build the explicit DDSSim CLI example." OFF)
option(DDSCPP_ENABLE_RTI "RTI is required for the public DDS library." ON)
if(NOT DDSCPP_ENABLE_RTI)
    message(FATAL_ERROR "DDSCPP는 실제 RTI 전용입니다. InMemory는 내부 테스트 구현이며 빌드 모드가 아닙니다.")
endif()
option(DDSCPP_INSTALL_RTI_RUNTIME "Install RTI runtime libraries beside DDSCPP (check your RTI redistribution terms first)." OFF)
set(DDSCPP_RTI_HOME "" CACHE PATH "RTI Connext DDS installation directory (NDDSHOME).")
set(DDSCPP_RTI_PLATFORM "" CACHE STRING "Optional RTI target directory name or absolute path.")
set(DDSCPP_RTI_PRODUCT_VERSION "" CACHE STRING "RTI product version recorded in installed package metadata.")
set(DDSCPP_DEFINITIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../definitions" CACHE PATH "DDS XML definitions directory.")
set(DDSCPP_PREGENERATED_DIR "" CACHE PATH "Optional pre-generated DDSSim C++ directory for cross-host builds.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows RTI 라이브러리는 MSVC ABI가 필요합니다. Visual Studio 2022 이상의 x64 C++ 도구를 선택하세요.")
endif()
if(MSVC)
    # 소스 및 공개 헤더의 한국어 문자열을 동일하게 해석합니다.
    add_compile_options(/utf-8)
endif()

file(GLOB_RECURSE DDSCPP_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp/*.hpp")

set(DDSCPP_SOURCES
    core/src/api/DdsClient.cpp
    core/src/configuration/DdsConfiguration.cpp
    core/src/configuration/DdsConfigurationReader.cpp
    core/src/configuration/DdsClientOptionResolver.cpp
    core/src/testing/InMemoryDdsTransport.cpp
    core/src/transport/RtiDdsTransport.cpp)

set(DDSCPP_PRIVATE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp/api"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp/configuration"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp/transport"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp/testing")
add_library(dds_cpp_objects OBJECT ${DDSCPP_PUBLIC_HEADERS} ${DDSCPP_SOURCES})
target_include_directories(dds_cpp_objects
    PRIVATE ${DDSCPP_PRIVATE_INCLUDE_DIRS}
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>"
        "$<INSTALL_INTERFACE:include>")
target_compile_features(dds_cpp_objects PUBLIC cxx_std_20)
target_compile_options(dds_cpp_objects INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
set_target_properties(dds_cpp_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(dds_cpp_objects PRIVATE /W4 /permissive-)
    target_compile_definitions(dds_cpp_objects PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(dds_cpp_objects PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(DDSCPP_ENABLE_RTI)
    if(NOT DDSCPP_RTI_HOME AND DEFINED ENV{NDDSHOME})
        set(DDSCPP_RTI_HOME "$ENV{NDDSHOME}")
    endif()
    if(NOT DDSCPP_RTI_HOME AND WIN32)
        file(GLOB rti_candidates LIST_DIRECTORIES true "C:/Program Files/rti_connext_dds-*")
        if(rti_candidates)
            list(SORT rti_candidates COMPARE NATURAL ORDER DESCENDING)
            list(GET rti_candidates 0 DDSCPP_RTI_HOME)
        endif()
    endif()
    if(NOT DDSCPP_RTI_HOME)
        message(FATAL_ERROR "RTI Connext DDS를 찾을 수 없습니다. DDSCPP_RTI_HOME 또는 NDDSHOME을 지정하세요.")
    endif()

    set(RTI_PLATFORM_DIR "")
    if(DDSCPP_RTI_PLATFORM)
        if(IS_ABSOLUTE "${DDSCPP_RTI_PLATFORM}")
            set(RTI_PLATFORM_DIR "${DDSCPP_RTI_PLATFORM}")
        else()
            set(RTI_PLATFORM_DIR "${DDSCPP_RTI_HOME}/lib/${DDSCPP_RTI_PLATFORM}")
        endif()
        if(NOT IS_DIRECTORY "${RTI_PLATFORM_DIR}")
            message(FATAL_ERROR "지정한 RTI 대상 플랫폼 폴더가 없습니다: ${RTI_PLATFORM_DIR}")
        endif()
    elseif(WIN32)
        file(GLOB rti_platform_candidates LIST_DIRECTORIES true "${DDSCPP_RTI_HOME}/lib/x64Win64VS*")
        if(rti_platform_candidates)
            list(SORT rti_platform_candidates COMPARE NATURAL ORDER DESCENDING)
            list(GET rti_platform_candidates 0 RTI_PLATFORM_DIR)
        endif()
    elseif(UNIX)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            file(GLOB rti_platform_candidates LIST_DIRECTORIES true
                "${DDSCPP_RTI_HOME}/lib/arm64Linux*"
                "${DDSCPP_RTI_HOME}/lib/aarch64Linux*")
        else()
            file(GLOB rti_platform_candidates LIST_DIRECTORIES true
                "${DDSCPP_RTI_HOME}/lib/x64Linux*")
        endif()
        if(rti_platform_candidates)
            list(SORT rti_platform_candidates COMPARE NATURAL ORDER DESCENDING)
            list(GET rti_platform_candidates 0 RTI_PLATFORM_DIR)
        endif()
    endif()
    if(NOT RTI_PLATFORM_DIR)
        message(FATAL_ERROR "${DDSCPP_RTI_HOME}/lib 아래에서 RTI 대상 라이브러리를 찾지 못했습니다.")
    endif()
    get_filename_component(DDSCLIENT_RTI_PLATFORM_NAME "${RTI_PLATFORM_DIR}" NAME)
    if(NOT DDSCPP_RTI_PRODUCT_VERSION AND EXISTS "${DDSCPP_RTI_HOME}/rti_versions.xml")
        file(READ "${DDSCPP_RTI_HOME}/rti_versions.xml" rti_versions_xml)
        string(REGEX MATCH "<base_version>[ \\t\\r\\n]*([^<]+)" rti_version_match "${rti_versions_xml}")
        if(CMAKE_MATCH_1)
            string(STRIP "${CMAKE_MATCH_1}" DDSCPP_RTI_PRODUCT_VERSION)
        endif()
    endif()

    target_compile_definitions(dds_cpp_objects PUBLIC DDSCPP_ENABLE_RTI=1)
    if(WIN32)
        target_compile_definitions(dds_cpp_objects PUBLIC NDDS_DLL_VARIABLE=1 WIN32_LEAN_AND_MEAN=1)
    elseif(UNIX)
        target_compile_definitions(dds_cpp_objects PUBLIC RTI_LINUX=1 RTI_UNIX=1 RTI_64BIT=1)
    endif()
    target_include_directories(dds_cpp_objects SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${DDSCPP_RTI_HOME}/include>"
        "$<BUILD_INTERFACE:${DDSCPP_RTI_HOME}/include/ndds>"
        "$<BUILD_INTERFACE:${DDSCPP_RTI_HOME}/include/ndds/hpp>")
    if(WIN32)
        target_link_libraries(dds_cpp_objects PUBLIC
            optimized "${RTI_PLATFORM_DIR}/nddscpp2.lib"
            optimized "${RTI_PLATFORM_DIR}/nddsc.lib"
            optimized "${RTI_PLATFORM_DIR}/nddscore.lib"
            debug "${RTI_PLATFORM_DIR}/nddscpp2d.lib"
            debug "${RTI_PLATFORM_DIR}/nddscd.lib"
            debug "${RTI_PLATFORM_DIR}/nddscored.lib")
        set(RTI_APPUTILS_LIBRARY optimized "${RTI_PLATFORM_DIR}/rtiapputilsc.lib"
            debug "${RTI_PLATFORM_DIR}/rtiapputilscd.lib")
    else()
        find_library(RTI_NDDSCPP2_LIBRARY NAMES nddscpp2 PATHS "${RTI_PLATFORM_DIR}" NO_DEFAULT_PATH REQUIRED)
        find_library(RTI_NDDSC_LIBRARY NAMES nddsc PATHS "${RTI_PLATFORM_DIR}" NO_DEFAULT_PATH REQUIRED)
        find_library(RTI_NDDSCORE_LIBRARY NAMES nddscore PATHS "${RTI_PLATFORM_DIR}" NO_DEFAULT_PATH REQUIRED)
        if(DDSCPP_BUILD_CLI)
            find_library(RTI_APPUTILS_LIBRARY NAMES rtiapputilsc PATHS "${RTI_PLATFORM_DIR}" NO_DEFAULT_PATH REQUIRED)
        endif()
        target_link_libraries(dds_cpp_objects PUBLIC
            "${RTI_NDDSCPP2_LIBRARY}"
            "${RTI_NDDSC_LIBRARY}"
            "${RTI_NDDSCORE_LIBRARY}"
            dl m pthread rt)
    endif()
    set(DDSCPP_RTI_RUNTIME_DIR "${RTI_PLATFORM_DIR}" CACHE INTERNAL "RTI runtime directory")
endif()

if(DDSCPP_BUILD_MODULES)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AutoMessageModules.cmake")
else()
    set(DDSCLIENT_AUTO_MODULES "")
endif()

if(DDSCPP_PREGENERATED_DIR)
    set(DDSCPP_GENERATED_DIR "${DDSCPP_PREGENERATED_DIR}/DDSSim")
else()
    set(DDSCPP_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/DDSSim")
endif()
set(DDSCPP_CLI_REGISTRY_DIR "${CMAKE_CURRENT_BINARY_DIR}/cli-generated")

if(DDSCPP_ENABLE_RTI AND DDSCPP_BUILD_CLI)
    if(NOT TARGET dds_module_DDSSim)
        message(FATAL_ERROR "CLI는 DDSSim 모듈이 필요합니다. Core 단독 빌드는 DDSCPP_BUILD_CLI=OFF를 지정하세요.")
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../cli/codegen/generate-registry.py"
        --dds-sim "${DDSCPP_DEFINITIONS_DIR}/DDSSim.xml" --output-dir "${DDSCPP_CLI_REGISTRY_DIR}"
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(ddsclient "${CMAKE_CURRENT_SOURCE_DIR}/../cli/cpp/ddsclient.cpp")
    target_link_libraries(ddsclient PRIVATE dds_cpp_objects dds_module_DDSSim ${RTI_APPUTILS_LIBRARY})
    target_include_directories(ddsclient PRIVATE core/include ${DDSCPP_PRIVATE_INCLUDE_DIRS} "${DDSCPP_GENERATED_DIR}" "${DDSCPP_CLI_REGISTRY_DIR}")
    if(WIN32)
        target_compile_options(ddsclient PRIVATE /utf-8)
        add_custom_command(TARGET ddsclient POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/rtiapputilscd.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/rtiapputilsc.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/rtixml2d.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/rtixml2.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscpp2d.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscpp2.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscd.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddsc.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscored.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscore.dll>"
                "$<TARGET_FILE_DIR:ddsclient>"
            COMMAND_EXPAND_LISTS)
    elseif(UNIX)
        set_target_properties(ddsclient PROPERTIES
            BUILD_RPATH "${RTI_PLATFORM_DIR}"
            INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
endif()

if(DDSCPP_BUILD_STATIC)
    add_library(dds_cpp_static STATIC $<TARGET_OBJECTS:dds_cpp_objects>)
    target_link_libraries(dds_cpp_static PUBLIC dds_cpp_objects)
    target_include_directories(dds_cpp_static PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>"
        "$<INSTALL_INTERFACE:include>")
    if(WIN32)
        set(DDSCPP_STATIC_OUTPUT_NAME dds_cpp_static)
    else()
        set(DDSCPP_STATIC_OUTPUT_NAME dds_cpp)
    endif()
    set_target_properties(dds_cpp_static PROPERTIES
        OUTPUT_NAME "${DDSCPP_STATIC_OUTPUT_NAME}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

if(DDSCPP_BUILD_SHARED)
    # 인라인 공개 API 심볼은 실제로 컴파일하는 객체 타깃에서 내보낸다.
    target_compile_definitions(dds_cpp_objects PRIVATE DDSCPP_BUILDING_DLL=1)
    add_library(dds_cpp_shared SHARED $<TARGET_OBJECTS:dds_cpp_objects>)
    target_link_libraries(dds_cpp_shared PUBLIC dds_cpp_objects)
    target_include_directories(dds_cpp_shared PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>"
        "$<INSTALL_INTERFACE:include>")
    target_compile_definitions(dds_cpp_shared PRIVATE DDSCPP_BUILDING_DLL=1)
    set_target_properties(dds_cpp_shared PROPERTIES
        OUTPUT_NAME dds_cpp
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(DDSCPP_ENABLE_RTI AND UNIX)
        set_target_properties(dds_cpp_shared PROPERTIES
            BUILD_RPATH "${RTI_PLATFORM_DIR}"
            INSTALL_RPATH "$ORIGIN")
    endif()
    if(DDSCPP_ENABLE_RTI AND WIN32)
        add_custom_command(TARGET dds_cpp_shared POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscpp2d.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscpp2.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscd.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddsc.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscored.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscore.dll>"
                "$<TARGET_FILE_DIR:dds_cpp_shared>"
            COMMAND_EXPAND_LISTS)
    endif()
endif()

if(DDSCPP_BUILD_TESTS)
    foreach(fixture IN ITEMS DDSSim Telemetry)
        if(NOT TARGET dds_module_${fixture})
            message(FATAL_ERROR "선택한 예제 테스트의 모듈이 없습니다: ${fixture}")
        endif()
    endforeach()
    enable_testing()
    add_executable(dds_cpp_smoke_tests tests/smoke_tests.cpp tests/rti_tests.cpp)
    target_link_libraries(dds_cpp_smoke_tests PRIVATE dds_cpp_objects dds_module_DDSSim dds_module_Telemetry)
    target_include_directories(dds_cpp_smoke_tests PRIVATE core/include ${DDSCPP_PRIVATE_INCLUDE_DIRS})
    add_test(NAME dds_cpp_smoke_tests COMMAND dds_cpp_smoke_tests)
    set_tests_properties(dds_cpp_smoke_tests PROPERTIES
        TIMEOUT 90
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME dds_registry_generator COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/test_generate_registry.py")
        set_tests_properties(dds_registry_generator PROPERTIES TIMEOUT 30)
    endif()
    set(DDS_CONTRACT_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/validate-dds.py")
    if(Python3_Interpreter_FOUND AND EXISTS "${DDS_CONTRACT_VALIDATOR}")
        set(DDS_CONTRACT_VALIDATOR_ARGS
            --cpp-root "${CMAKE_CURRENT_SOURCE_DIR}")
        if(DDSCPP_ENABLE_RTI)
            list(APPEND DDS_CONTRACT_VALIDATOR_ARGS
                --cpp-registry "${DDSCPP_CLI_REGISTRY_DIR}/dds_cpp_rti_type_registry.inc"
                --require-cpp-generated)
        endif()
        add_test(NAME dds_contract_parity
            COMMAND "${Python3_EXECUTABLE}" "${DDS_CONTRACT_VALIDATOR}" ${DDS_CONTRACT_VALIDATOR_ARGS})
        set_tests_properties(dds_contract_parity PROPERTIES TIMEOUT 30)
    endif()
    if(TARGET ddsclient)
        add_test(NAME ddsclient_cli_list COMMAND ddsclient list
            --qos "${DDSCPP_DEFINITIONS_DIR}/qos_profiles.xml")
        add_test(NAME ddsclient_cli_publish COMMAND ddsclient publish TimeTickInformation
            --domain 77 --wait-timeout-ms 100 --wait-for-subscribers 2147483647
            --qos "${DDSCPP_DEFINITIONS_DIR}/qos_profiles.xml")
        set_tests_properties(ddsclient_cli_publish PROPERTIES WILL_FAIL TRUE)
        set_tests_properties(ddsclient_cli_list ddsclient_cli_publish PROPERTIES TIMEOUT 30)
    endif()
    if(WIN32 AND TARGET ddsclient)
        find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        find_program(DOTNET_EXECUTABLE NAMES dotnet)
        set(DDS_CSHARP_CLI_PROJECT
            "${CMAKE_CURRENT_SOURCE_DIR}/../cli/csharp/DdsAmbassador.DDSClient.Cli.csproj")
        if(POWERSHELL_EXECUTABLE AND DOTNET_EXECUTABLE AND EXISTS "${DDS_CSHARP_CLI_PROJECT}")
            add_test(NAME dds_csharp_cpp_interop
                COMMAND "${POWERSHELL_EXECUTABLE}"
                    -NoProfile
                    -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/../cli/tests/test-interop.ps1"
                    -CppCli "$<TARGET_FILE:ddsclient>"
                    -Dotnet "${DOTNET_EXECUTABLE}"
                    -CSharpProject "${DDS_CSHARP_CLI_PROJECT}"
                    -Definitions "${DDSCPP_DEFINITIONS_DIR}")
            set_tests_properties(dds_csharp_cpp_interop PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        endif()
    endif()
    if(DDSCPP_ENABLE_RTI AND WIN32)
        add_custom_command(TARGET dds_cpp_smoke_tests POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscpp2d.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscpp2.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscd.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddsc.dll>"
                "$<$<CONFIG:Debug>:${RTI_PLATFORM_DIR}/nddscored.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${RTI_PLATFORM_DIR}/nddscore.dll>"
                "$<TARGET_FILE_DIR:dds_cpp_smoke_tests>"
            COMMAND_EXPAND_LISTS)
    endif()
endif()

if(TARGET ddsclient)
    install(TARGETS ddsclient RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(TARGET dds_cpp_static)
    install(TARGETS dds_cpp_static
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
if(TARGET dds_cpp_shared)
    install(TARGETS dds_cpp_shared
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/core/include/dds_cpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" PATTERN "testing" EXCLUDE)
install(FILES "${DDSCPP_DEFINITIONS_DIR}/qos_profiles.xml"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dds_cpp/definitions")
if(EXISTS "${DDSCPP_DEFINITIONS_DIR}/networks")
    install(DIRECTORY "${DDSCPP_DEFINITIONS_DIR}/networks/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/dds_cpp/definitions/networks" FILES_MATCHING PATTERN "*.xml")
endif()

if(DDSCPP_ENABLE_RTI)
    if(DDSCPP_INSTALL_RTI_RUNTIME AND WIN32)
        install(FILES
            "${RTI_PLATFORM_DIR}/nddscpp2.dll"
            "${RTI_PLATFORM_DIR}/nddsc.dll"
            "${RTI_PLATFORM_DIR}/nddscore.dll"
            "${RTI_PLATFORM_DIR}/rtiapputilsc.dll"
            "${RTI_PLATFORM_DIR}/rtixml2.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
        install(FILES
            "${RTI_PLATFORM_DIR}/nddscpp2d.dll"
            "${RTI_PLATFORM_DIR}/nddscd.dll"
            "${RTI_PLATFORM_DIR}/nddscored.dll"
            "${RTI_PLATFORM_DIR}/rtiapputilscd.dll"
            "${RTI_PLATFORM_DIR}/rtixml2d.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            CONFIGURATIONS Debug)
    elseif(DDSCPP_INSTALL_RTI_RUNTIME AND UNIX)
        file(GLOB DDSCPP_RTI_RUNTIME_LIBS
            "${RTI_PLATFORM_DIR}/libnddscpp2.so*"
            "${RTI_PLATFORM_DIR}/libnddsc.so*"
            "${RTI_PLATFORM_DIR}/libnddscore.so*"
            "${RTI_PLATFORM_DIR}/librtiapputilsc.so*")
        file(GLOB DDSCPP_RTI_XML_RUNTIME_LIBS "${RTI_PLATFORM_DIR}/librtixml2.so*")
        list(APPEND DDSCPP_RTI_RUNTIME_LIBS ${DDSCPP_RTI_XML_RUNTIME_LIBS})
        install(FILES ${DDSCPP_RTI_RUNTIME_LIBS}
            DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
endif()

set(DDSCLIENT_HAS_STATIC ${DDSCPP_BUILD_STATIC})
set(DDSCLIENT_HAS_SHARED ${DDSCPP_BUILD_SHARED})
if(NOT DEFINED DDSCLIENT_RTI_PLATFORM_NAME)
    set(DDSCLIENT_RTI_PLATFORM_NAME "")
endif()
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DDSClientConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DDSClientConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DDSClient")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DDSClientConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DDSClientConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DDSClientConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/DDSClient")
