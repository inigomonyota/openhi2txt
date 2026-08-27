cmake_minimum_required(VERSION 3.20)

project(openhi2txt
    VERSION 0.4.0
    LANGUAGES CXX
)

option(OPENHI2TXT_BUILD_CLI "Build the openhi2txt command-line executable" ON)
option(BUILD_TESTING "Build the openhi2txt test executables" ON)

if(BUILD_TESTING)
    enable_testing()
endif()

set(OPENHI2TXT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(OPENHI2TXT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(OPENHI2TXT_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(OPENHI2TXT_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${OPENHI2TXT_GENERATED_INCLUDE_DIR}/openhi2txt/version.h"
    @ONLY
)

add_library(openhi2txt STATIC
    src/openhi2txt.cpp
    src/app/DefResolver.cpp
    src/app/InputProcessor.cpp
    src/core/Formatter.cpp
    src/core/Processor.cpp
    src/core/ResultRenderer.cpp
    src/core/StructureSelector.cpp
    src/io/ArchiveManager.cpp
    src/io/ChdReader.cpp
    src/io/HiscoreDat.cpp
    src/io/Utils.cpp
    src/xml/XmlParser.cpp
)

add_library(openhi2txt::openhi2txt ALIAS openhi2txt)

target_compile_features(openhi2txt PUBLIC cxx_std_17)

set_target_properties(openhi2txt PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lib"
)

if(MSVC)
    set_target_properties(openhi2txt PROPERTIES
        VS_GLOBAL_VcpkgEnabled false
        VS_GLOBAL_VcpkgApplocalDeps false
    )
endif()

target_include_directories(openhi2txt
    PUBLIC
        "$<BUILD_INTERFACE:${OPENHI2TXT_INCLUDE_DIR}>"
        "$<BUILD_INTERFACE:${OPENHI2TXT_GENERATED_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${OPENHI2TXT_SRC_DIR}"
        "${OPENHI2TXT_THIRDPARTY_DIR}/rapidxml-1.13"
)

if(MSVC)
    target_compile_options(openhi2txt PRIVATE /utf-8)
endif()

set(OPENHI2TXT_BUNDLED_ZLIB_DIR "${OPENHI2TXT_THIRDPARTY_DIR}/zlib-static")
set(OPENHI2TXT_BUNDLED_ZLIB_INCLUDE "${OPENHI2TXT_BUNDLED_ZLIB_DIR}/include")
set(OPENHI2TXT_BUNDLED_ZLIB_LIB "${OPENHI2TXT_BUNDLED_ZLIB_DIR}/lib/msvc-x64-md/zs.lib")
set(OPENHI2TXT_BUNDLED_MINIZIP_LIB "${OPENHI2TXT_BUNDLED_ZLIB_DIR}/lib/msvc-x64-md/minizip.lib")

if(WIN32 AND MSVC)
    if(NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}/zlib.h"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}/minizip/unzip.h"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_LIB}"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_MINIZIP_LIB}")
        message(FATAL_ERROR
            "Windows/MSVC builds expect static zlib and minizip under "
            "thirdparty/zlib-static. Required layout: include/zlib.h, "
            "include/minizip/unzip.h, lib/msvc-x64-md/zs.lib, and "
            "lib/msvc-x64-md/minizip.lib."
        )
    endif()

    set(ZLIB_INCLUDE_DIR "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}" CACHE PATH
        "Path to the zlib headers used by openhi2txt" FORCE)
    set(ZLIB_LIBRARY "${OPENHI2TXT_BUNDLED_ZLIB_LIB}" CACHE FILEPATH
        "Path to the static zlib library used by openhi2txt" FORCE)
    find_package(ZLIB REQUIRED)

    add_library(openhi2txt_minizip STATIC IMPORTED)
    set_target_properties(openhi2txt_minizip PROPERTIES
        IMPORTED_LOCATION "${OPENHI2TXT_BUNDLED_MINIZIP_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}"
    )

    target_link_libraries(openhi2txt PRIVATE ZLIB::ZLIB openhi2txt_minizip)
    target_compile_definitions(openhi2txt PRIVATE ZLIB_STATIC)
else()
    find_package(ZLIB REQUIRED)
    find_path(MINIZIP_INCLUDE_DIR
        NAMES minizip/unzip.h
        PATH_SUFFIXES include
    )
    find_library(MINIZIP_LIBRARY
        NAMES minizip
    )

    if(NOT MINIZIP_INCLUDE_DIR OR NOT MINIZIP_LIBRARY)
        message(FATAL_ERROR
            "minizip was not found. Install the minizip development package "
            "or provide MINIZIP_INCLUDE_DIR and MINIZIP_LIBRARY."
        )
    endif()

    target_include_directories(openhi2txt PRIVATE "${MINIZIP_INCLUDE_DIR}")
    target_link_libraries(openhi2txt PRIVATE ZLIB::ZLIB "${MINIZIP_LIBRARY}")
endif()

# libchdr is pinned as a submodule and built statically. It reuses the zlib
# target selected above, while CD/GD-ROM support is excluded because
# openhi2txt reads writable hard-disk images and their delta overlays.
function(openhi2txt_add_libchdr)
    set(BUILD_SHARED_LIBS OFF)
    set(INSTALL_STATIC_LIBS OFF CACHE BOOL "Install libchdr static libraries" FORCE)
    set(WITH_SYSTEM_ZLIB ON CACHE BOOL "Use openhi2txt's selected zlib for libchdr" FORCE)
    set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "Use libchdr's bundled Zstandard decoder" FORCE)
    set(CHDR_ENABLE_CD OFF CACHE BOOL "Enable libchdr CD/GD-ROM support" FORCE)
    set(CHDR_WANT_RAW_DATA_SECTOR OFF CACHE BOOL "Reconstruct raw CD sectors" FORCE)
    set(CHDR_WANT_SUBCODE OFF CACHE BOOL "Output CD subchannel data" FORCE)
    set(BUILD_FUZZER OFF CACHE BOOL "Build libchdr's fuzz target" FORCE)
    add_subdirectory(
        "${OPENHI2TXT_THIRDPARTY_DIR}/libchdr"
        "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/libchdr"
        EXCLUDE_FROM_ALL
    )
endfunction()
openhi2txt_add_libchdr()
target_link_libraries(openhi2txt PRIVATE chdr-static)

if(OPENHI2TXT_BUILD_CLI)
    set(OPENHI2TXT_CLI_RESOURCES)
    if(WIN32)
        enable_language(RC)
        set(OPENHI2TXT_RC_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
        configure_file(
            "${OPENHI2TXT_SRC_DIR}/app/openhi2txt.rc.in"
            "${OPENHI2TXT_RC_DIR}/openhi2txt.rc"
            @ONLY
        )
        list(APPEND OPENHI2TXT_CLI_RESOURCES "${OPENHI2TXT_RC_DIR}/openhi2txt.rc")
    endif()

    add_executable(openhi2txt_cli
        src/app/App.cpp
        src/app/Args.cpp
        src/app/Main.cpp
        src/app/OutputPrinter.cpp
        ${OPENHI2TXT_CLI_RESOURCES}
    )
    set_target_properties(openhi2txt_cli PROPERTIES
        OUTPUT_NAME openhi2txt
        ARCHIVE_OUTPUT_NAME openhi2txt_cli
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/bin"
    )
    if(MSVC)
        set_target_properties(openhi2txt_cli PROPERTIES
            VS_GLOBAL_VcpkgEnabled false
            VS_GLOBAL_VcpkgApplocalDeps false
        )
    endif()

    target_link_libraries(openhi2txt_cli PRIVATE openhi2txt)
    target_include_directories(openhi2txt_cli PRIVATE "${OPENHI2TXT_SRC_DIR}")

endif()

if(BUILD_TESTING)
    add_executable(openhi2txt_xml_parser_tests
        tests/XmlParserTests.cpp
    )
    target_link_libraries(openhi2txt_xml_parser_tests PRIVATE openhi2txt)
    target_include_directories(openhi2txt_xml_parser_tests PRIVATE "${OPENHI2TXT_SRC_DIR}")
    set_target_properties(openhi2txt_xml_parser_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/tests"
    )
    add_test(NAME openhi2txt_xml_parser_tests COMMAND openhi2txt_xml_parser_tests)

    add_executable(openhi2txt_open_extension_tests
        tests/OpenExtensionTests.cpp
        src/app/OutputPrinter.cpp
    )
    target_link_libraries(openhi2txt_open_extension_tests PRIVATE openhi2txt)
    target_include_directories(openhi2txt_open_extension_tests PRIVATE "${OPENHI2TXT_SRC_DIR}")
    set_target_properties(openhi2txt_open_extension_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/tests"
    )
    add_test(NAME openhi2txt_open_extension_tests COMMAND openhi2txt_open_extension_tests)
endif()
