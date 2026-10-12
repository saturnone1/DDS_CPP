find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DDS_SCHEMA_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/schema_modules.py")
file(GLOB DDS_SCHEMA_INPUTS CONFIGURE_DEPENDS "${DDSCPP_DEFINITIONS_DIR}/*.xml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DDS_SCHEMA_INPUTS} "${DDS_SCHEMA_GENERATOR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/isolate_types.py")
file(GLOB DDS_CODEGEN_BACKENDS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/backends/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DDS_CODEGEN_BACKENDS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/codegen/codegen_io.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${DDS_SCHEMA_GENERATOR}"
    --definitions "${DDSCPP_DEFINITIONS_DIR}" --list --manifest "${CMAKE_CURRENT_BINARY_DIR}/message-modules.txt"
    OUTPUT_VARIABLE DDSCLIENT_AUTO_MODULES OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
foreach(module IN LISTS DDSCLIENT_AUTO_MODULES)
    if(DDSCPP_PREGENERATED_DIR)
        set(module_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${module}")
    else()
        set(module_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/${module}")
    endif()
    set(generated_dir "${module_dir}")
    set(pregenerated_args)
    if(DDSCPP_PREGENERATED_DIR)
        set(generated_dir "${DDSCPP_PREGENERATED_DIR}/${module}")
        list(APPEND pregenerated_args --pregenerated "${generated_dir}")
    endif()
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${DDS_SCHEMA_GENERATOR}"
        --xml "${DDSCPP_DEFINITIONS_DIR}/${module}.xml" --output "${module_dir}"
        --language C++11 --rti-home "${DDSCPP_RTI_HOME}" ${pregenerated_args}
        COMMAND_ERROR_IS_FATAL ANY)
    add_library(dds_module_${module} STATIC "${module_dir}/${module}Messages.cpp"
        "${generated_dir}/${module}.cxx" "${generated_dir}/${module}Plugin.cxx")
    target_include_directories(dds_module_${module} PUBLIC "${module_dir}" "${generated_dir}"
        "$<TARGET_PROPERTY:dds_cpp_objects,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(dds_module_${module} PUBLIC "$<TARGET_PROPERTY:dds_cpp_objects,INTERFACE_COMPILE_DEFINITIONS>")
    target_link_libraries(dds_module_${module} PUBLIC "$<GENEX_EVAL:$<TARGET_PROPERTY:dds_cpp_objects,INTERFACE_LINK_LIBRARIES>>")
    target_compile_features(dds_module_${module} PUBLIC cxx_std_20)
    set_target_properties(dds_module_${module} PROPERTIES POSITION_INDEPENDENT_CODE ON ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    install(TARGETS dds_module_${module} ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${module_dir}/${module}Messages.hpp" "${module_dir}/${module}_messages.hpp" "${generated_dir}/${module}.hpp"
        "${generated_dir}/${module}Plugin.hpp" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/dds_modules/${module}")
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/message-modules.txt" DESTINATION "${CMAKE_INSTALL_DATADIR}/dds_cpp")
