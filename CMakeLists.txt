cmake_minimum_required(VERSION 3.18)

# You must set ASM, otherwise CMAKE_ASM_COMPILE_OBJECT will not work on MSVC
project(AcceleratorCSS C CXX ASM)

if(DEFINED ENV{HL2SDKCS2})
    set(SOURCESDK_DIR $ENV{HL2SDKCS2})
else()
    message(FATAL_ERROR "Environment variable HL2SDKCS2 is not set!")
endif()

if(DEFINED ENV{MMSOURCE_DEV})
    set(METAMOD_DIR $ENV{MMSOURCE_DEV})
else()
    message(FATAL_ERROR "Environment variable MMSOURCE_DEV is not set!")
endif()

if(DEFINED ENV{CSGO_PROTO})
    set(CSGO_PROTO_DIR $ENV{CSGO_PROTO})
else()
    message(FATAL_ERROR "Environment variable CSGO_PROTO is not set!")
endif()

include("makefiles/shared.cmake")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(FMT_WERROR OFF CACHE BOOL "" FORCE)
set(FUNCHOOK_BUILD_TESTS OFF CACHE BOOL "Disable building tests for funchook." FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "Disable building examples for Zydis." FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "Disable building tools for Zydis." FORCE)

add_subdirectory(vendor/spdlog)
add_subdirectory(vendor/dyncall)
add_subdirectory(vendor/zlib)
add_subdirectory(vendor/funchook)
set_property(TARGET funchook-static PROPERTY POSITION_INDEPENDENT_CODE ON)

include("makefiles/protobuf.cmake")

set(SOURCE_FILES
    src/extension.cpp
    src/extension.h
    ${SOURCESDK_DIR}/public/tier0/memoverride.cpp
    ${SOURCESDK_DIR}/tier1/keyvalues3.cpp
    ${SOURCESDK_DIR}/tier1/convar.cpp
    ${SOURCESDK_DIR}/entity2/entityidentity.cpp
    ${SOURCESDK_DIR}/entity2/entitysystem.cpp
    ${SOURCESDK_DIR}/entity2/entitykeyvalues.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_chookidman.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_chookmaninfo.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_cvfnptr.cpp
    ${METAMOD_DIR}/core/sourcehook/sourcehook_impl_cproto.cpp
    src/log.cpp
    src/log.h
    src/paths.h
    src/CMiniDumpComment.hpp
    vendor/nlohmann/json.hpp
)

file(GLOB BREAKPAD_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_cfi_to_module.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_cu_to_module.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_line_to_module.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf_range_list_handler.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/language.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/module.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/path_helper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/stabs_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/stabs_to_module.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/bytereader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/dwarf2diehandler.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/dwarf2reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/dwarf/elf_reader.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/linux/crc32.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/linux/dump_symbols.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/breakpad/src/common/linux/elf_symbols_to_module.cc
)

set_source_files_properties(${BREAKPAD_SRC} PROPERTIES LANGUAGE CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${BREAKPAD_SRC} ${NATIVES_SOURCES} ${CONVERSIONS_SOURCES} ${CONVERSIONS_HEADERS})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(LINUX)
    include("makefiles/linux.base.cmake")
    set_target_properties(${PROJECT_NAME} PROPERTIES
                          PREFIX ""
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/AcceleratorCSS/bin/linuxsteamrt64"
    )
endif()

# Libraries
target_link_libraries(${PROJECT_NAME} ${ACCELERATORCSS_LINK_LIBRARIES} Protobufs)

add_custom_command(
    TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
)
