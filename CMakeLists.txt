cmake_minimum_required(VERSION 3.20)

project(DdsAmbassadorDDSCPP VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(DDSCPP_BUILD_STATIC "Build the DDSCPP static library." ON)
option(DDSCPP_BUILD_SHARED "Build the DDSCPP shared library." ON)
option(DDSCPP_BUILD_TESTS "Build DDSCPP smoke tests." ON)
option(DDSCPP_BUILD_CLI "Build the NuGet-compatible ddsclient CLI when RTI is enabled." ON)
option(DDSCPP_ENABLE_RTI "Generate DDS types and enable the RTI Connext transport." ON)
set(DDSCPP_RTI_HOME "" CACHE PATH "RTI Connext DDS installation directory (NDDSHOME).")
set(DDSCPP_DEFINITIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../DDS_Nuget/definitions" CACHE PATH "DDS XML definitions directory.")
set(DDSCPP_PREGENERATED_DIR "" CACHE PATH "Optional pre-generated DDSSim C++ directory for cross-host builds.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DDSCPP_PUBLIC_HEADERS
    include/dds_cpp/dds_client.hpp
    include/dds_cpp/dds_client_options.hpp
    include/dds_cpp/dds_configuration.hpp
    include/dds_cpp/dds_configuration_loader.hpp
    include/dds_cpp/dds_cpp.hpp
    include/dds_cpp/dds_exception.hpp
    include/dds_cpp/generated_types.hpp
    include/dds_cpp/dds_log_level.hpp
    include/dds_cpp/dds_transport.hpp
    include/dds_cpp/in_memory_dds_transport.hpp
    include/dds_cpp/rti_dds_transport.hpp
    include/dds_cpp/topic_definition.hpp
    include/dds_cpp/topic_direction.hpp
    include/dds_cpp/visibility.hpp)

set(DDSCPP_SOURCES
    src/dds_client.cpp
    src/dds_client_options.cpp
    src/dds_configuration.cpp
    src/dds_configuration_loader.cpp
    src/dds_path_utils.cpp
    src/in_memory_dds_transport.cpp
    src/rti_dds_transport.cpp)

add_library(dds_cpp_objects OBJECT ${DDSCPP_PUBLIC_HEADERS} ${DDSCPP_SOURCES})
target_include_directories(dds_cpp_objects
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_compile_features(dds_cpp_objects PUBLIC cxx_std_20)
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
        message(FATAL_ERROR "RTI Connext DDS was not found. Set DDSCPP_RTI_HOME or NDDSHOME, or configure with DDSCPP_ENABLE_RTI=OFF.")
    endif()

    if(DDSCPP_PREGENERATED_DIR)
        get_filename_component(DDSCPP_GENERATED_DIR "${DDSCPP_PREGENERATED_DIR}" ABSOLUTE)
    else()
        find_program(RTIDDSGEN_EXECUTABLE NAMES rtiddsgen rtiddsgen.bat
            HINTS "${DDSCPP_RTI_HOME}/bin" NO_DEFAULT_PATH REQUIRED)
        set(RTI_SCHEMA_DIR "${DDSCPP_RTI_HOME}/resource/schema")
        set(DDSCPP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    endif()
    set(DDSCPP_GENERATED_FILES
        "${DDSCPP_GENERATED_DIR}/DDSSim.hpp"
        "${DDSCPP_GENERATED_DIR}/DDSSim.cxx"
        "${DDSCPP_GENERATED_DIR}/DDSSimPlugin.hpp"
        "${DDSCPP_GENERATED_DIR}/DDSSimPlugin.cxx"
        "${DDSCPP_GENERATED_DIR}/dds_cpp_rti_type_registry.inc"
        "${DDSCPP_GENERATED_DIR}/dds_cpp_json_names.inc")
    if(DDSCPP_PREGENERATED_DIR)
        foreach(generated_file IN LISTS DDSCPP_GENERATED_FILES)
            if(NOT EXISTS "${generated_file}")
                message(FATAL_ERROR "Required pre-generated RTI file is missing: ${generated_file}")
            endif()
        endforeach()
    else()
        add_custom_command(
            OUTPUT ${DDSCPP_GENERATED_FILES}
            COMMAND "${CMAKE_COMMAND}"
                -DRTIDDSGEN=${RTIDDSGEN_EXECUTABLE}
                -DRTI_SCHEMA_DIR=${RTI_SCHEMA_DIR}
                -DDDS_XML=${DDSCPP_DEFINITIONS_DIR}/DDSSim.xml
                -DTOPICS_XML=${DDSCPP_DEFINITIONS_DIR}/topics.xml
                -DOUTPUT_DIR=${DDSCPP_GENERATED_DIR}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateRtiTypes.cmake"
            DEPENDS
                "${DDSCPP_DEFINITIONS_DIR}/DDSSim.xml"
                "${DDSCPP_DEFINITIONS_DIR}/topics.xml"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateRtiTypes.cmake"
            VERBATIM
            COMMENT "Generating RTI Connext C++ types and topic registry")
    endif()
    target_sources(dds_cpp_objects PRIVATE ${DDSCPP_GENERATED_FILES})
    set_source_files_properties(${DDSCPP_GENERATED_FILES} PROPERTIES GENERATED TRUE)

    set(RTI_PLATFORM_DIR "")
    if(WIN32)
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
        message(FATAL_ERROR "Could not determine the RTI platform library directory under ${DDSCPP_RTI_HOME}/lib.")
    endif()

    target_compile_definitions(dds_cpp_objects PUBLIC DDSCPP_ENABLE_RTI=1)
    if(WIN32)
        target_compile_definitions(dds_cpp_objects PUBLIC NDDS_DLL_VARIABLE=1 WIN32_LEAN_AND_MEAN=1)
    elseif(UNIX)
        target_compile_definitions(dds_cpp_objects PUBLIC RTI_LINUX=1 RTI_UNIX=1 RTI_64BIT=1)
    endif()
    target_include_directories(dds_cpp_objects PUBLIC
        "$<BUILD_INTERFACE:${DDSCPP_GENERATED_DIR}>")
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
        find_library(RTI_APPUTILS_LIBRARY NAMES rtiapputilsc PATHS "${RTI_PLATFORM_DIR}" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(dds_cpp_objects PUBLIC
            "${RTI_NDDSCPP2_LIBRARY}"
            "${RTI_NDDSC_LIBRARY}"
            "${RTI_NDDSCORE_LIBRARY}"
            dl m pthread rt)
    endif()
    set(DDSCPP_RTI_RUNTIME_DIR "${RTI_PLATFORM_DIR}" CACHE INTERNAL "RTI runtime directory")
endif()

if(DDSCPP_ENABLE_RTI AND DDSCPP_BUILD_CLI)
    add_executable(ddsclient tools/ddsclient.cpp)
    target_link_libraries(ddsclient PRIVATE dds_cpp_objects ${RTI_APPUTILS_LIBRARY})
    target_include_directories(ddsclient PRIVATE include "${DDSCPP_GENERATED_DIR}")
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
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
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
    add_library(dds_cpp_shared SHARED $<TARGET_OBJECTS:dds_cpp_objects>)
    target_link_libraries(dds_cpp_shared PUBLIC dds_cpp_objects)
    target_include_directories(dds_cpp_shared PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
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
    enable_testing()
    add_executable(dds_cpp_smoke_tests tests/smoke_tests.cpp)
    target_link_libraries(dds_cpp_smoke_tests PRIVATE dds_cpp_objects)
    target_include_directories(dds_cpp_smoke_tests PRIVATE include)
    add_test(NAME dds_cpp_smoke_tests COMMAND dds_cpp_smoke_tests)
    set_tests_properties(dds_cpp_smoke_tests PROPERTIES TIMEOUT 30)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    set(DDS_CONTRACT_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/../DDS_Nuget/tools/validate-dds.py")
    if(Python3_Interpreter_FOUND AND EXISTS "${DDS_CONTRACT_VALIDATOR}")
        set(DDS_CONTRACT_VALIDATOR_ARGS
            --cpp-root "${CMAKE_CURRENT_SOURCE_DIR}")
        if(DDSCPP_ENABLE_RTI)
            list(APPEND DDS_CONTRACT_VALIDATOR_ARGS
                --cpp-registry "${DDSCPP_GENERATED_DIR}/dds_cpp_rti_type_registry.inc"
                --require-cpp-generated)
        endif()
        add_test(NAME dds_contract_parity
            COMMAND "${Python3_EXECUTABLE}" "${DDS_CONTRACT_VALIDATOR}" ${DDS_CONTRACT_VALIDATOR_ARGS})
        set_tests_properties(dds_contract_parity PROPERTIES TIMEOUT 30)
    endif()
    if(TARGET ddsclient)
        add_test(NAME ddsclient_cli_list COMMAND ddsclient list
            --config "${DDSCPP_DEFINITIONS_DIR}/dds_client.xml")
        add_test(NAME ddsclient_cli_publish COMMAND ddsclient publish TimeTickInformation
            --config "${DDSCPP_DEFINITIONS_DIR}/dds_client.xml")
        set_tests_properties(ddsclient_cli_list ddsclient_cli_publish PROPERTIES TIMEOUT 30)
    endif()
    if(WIN32 AND TARGET ddsclient)
        find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
        find_program(DOTNET_EXECUTABLE NAMES dotnet)
        set(DDS_CSHARP_CLI_PROJECT
            "${CMAKE_CURRENT_SOURCE_DIR}/../DDS_Nuget/src/DdsAmbassador.DDSClient.Cli/DdsAmbassador.DDSClient.Cli.csproj")
        if(POWERSHELL_EXECUTABLE AND DOTNET_EXECUTABLE AND EXISTS "${DDS_CSHARP_CLI_PROJECT}")
            add_test(NAME dds_csharp_cpp_interop
                COMMAND "${POWERSHELL_EXECUTABLE}"
                    -NoProfile
                    -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/test-interop.ps1"
                    -CppCli "$<TARGET_FILE:ddsclient>"
                    -Dotnet "${DOTNET_EXECUTABLE}"
                    -CSharpProject "${DDS_CSHARP_CLI_PROJECT}"
                    -Config "${DDSCPP_DEFINITIONS_DIR}/dds_client.rti-multicast.xml")
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
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/dds_cpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${DDSCPP_DEFINITIONS_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/dds_cpp/definitions"
    FILES_MATCHING PATTERN "*.xml")

if(DDSCPP_ENABLE_RTI)
    install(FILES
        "${DDSCPP_GENERATED_DIR}/DDSSim.hpp"
        "${DDSCPP_GENERATED_DIR}/DDSSimPlugin.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    if(WIN32)
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
    elseif(UNIX)
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
