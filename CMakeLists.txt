cmake_minimum_required(VERSION 3.20)

project(openhi2txt
    VERSION 0.1.0
    LANGUAGES CXX
)

option(OPENHI2TXT_BUILD_CLI "Build the openhi2txt command-line executable" ON)

set(OPENHI2TXT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(OPENHI2TXT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(OPENHI2TXT_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

add_library(openhi2txt STATIC
    src/openhi2txt.cpp
    src/app/DefResolver.cpp
    src/app/InputProcessor.cpp
    src/core/Formatter.cpp
    src/core/Processor.cpp
    src/core/ResultRenderer.cpp
    src/core/StructureSelector.cpp
    src/io/ArchiveManager.cpp
    src/io/HiscoreDat.cpp
    src/io/Utils.cpp
    src/xml/XmlParser.cpp
)

add_library(openhi2txt::openhi2txt ALIAS openhi2txt)

target_compile_features(openhi2txt PUBLIC cxx_std_17)

if(MSVC)
    set_target_properties(openhi2txt PROPERTIES
        VS_GLOBAL_VcpkgEnabled false
        VS_GLOBAL_VcpkgApplocalDeps false
    )
endif()

target_include_directories(openhi2txt
    PUBLIC
        "$<BUILD_INTERFACE:${OPENHI2TXT_INCLUDE_DIR}>"
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
set(OPENHI2TXT_BUNDLED_ZLIB_LIB "${OPENHI2TXT_BUNDLED_ZLIB_DIR}/lib/zs.lib")
set(OPENHI2TXT_BUNDLED_MINIZIP_LIB "${OPENHI2TXT_BUNDLED_ZLIB_DIR}/lib/minizip.lib")

if(WIN32 AND MSVC)
    if(NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}/zlib.h"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}/minizip/unzip.h"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_ZLIB_LIB}"
            OR NOT EXISTS "${OPENHI2TXT_BUNDLED_MINIZIP_LIB}")
        message(FATAL_ERROR
            "Windows/MSVC builds expect static zlib and minizip under "
            "thirdparty/zlib-static. Required layout: include/zlib.h, "
            "include/minizip/unzip.h, lib/zs.lib, and lib/minizip.lib."
        )
    endif()

    add_library(openhi2txt_zlib STATIC IMPORTED)
    set_target_properties(openhi2txt_zlib PROPERTIES
        IMPORTED_LOCATION "${OPENHI2TXT_BUNDLED_ZLIB_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}"
    )

    add_library(openhi2txt_minizip STATIC IMPORTED)
    set_target_properties(openhi2txt_minizip PROPERTIES
        IMPORTED_LOCATION "${OPENHI2TXT_BUNDLED_MINIZIP_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENHI2TXT_BUNDLED_ZLIB_INCLUDE}"
    )

    target_link_libraries(openhi2txt PRIVATE openhi2txt_zlib openhi2txt_minizip)
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
    set_target_properties(openhi2txt_cli PROPERTIES OUTPUT_NAME openhi2txt)
    if(MSVC)
        set_target_properties(openhi2txt_cli PROPERTIES
            VS_GLOBAL_VcpkgEnabled false
            VS_GLOBAL_VcpkgApplocalDeps false
        )
    endif()

    target_link_libraries(openhi2txt_cli PRIVATE openhi2txt)
    target_include_directories(openhi2txt_cli PRIVATE "${OPENHI2TXT_SRC_DIR}")

    install(TARGETS openhi2txt_cli RUNTIME DESTINATION bin)
endif()

install(TARGETS openhi2txt
    EXPORT openhi2txtTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT openhi2txtTargets
    NAMESPACE openhi2txt::
    DESTINATION lib/cmake/openhi2txt
)
