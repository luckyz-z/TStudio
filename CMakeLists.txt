cmake_minimum_required(VERSION 3.25)
project(TStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(lua GIT_REPOSITORY https://github.com/lua/lua.git GIT_TAG v5.4.7 GIT_SHALLOW ON SOURCE_SUBDIR none)
FetchContent_MakeAvailable(lua)

FetchContent_Declare(lame
    URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
        https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz/download
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
    DOWNLOAD_NAME lame-3.100.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(lame)
file(GLOB LAME_SOURCES ${lame_SOURCE_DIR}/libmp3lame/*.c ${lame_SOURCE_DIR}/libmp3lame/vector/*.c)
if(MSVC)
    configure_file(${lame_SOURCE_DIR}/configMS.h ${CMAKE_CURRENT_BINARY_DIR}/lame-config/config.h COPYONLY)
else()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/lame-config/config.h [=[
#pragma once
#include <stdint.h>
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define PACKAGE "lame"
#define LAME_LIBRARY_BUILD
typedef long double ieee854_float80_t;
typedef double ieee754_float64_t;
typedef float ieee754_float32_t;
]=])
endif()
add_library(mp3lame STATIC ${LAME_SOURCES})
target_include_directories(mp3lame PUBLIC ${lame_SOURCE_DIR}/include PRIVATE ${lame_SOURCE_DIR}/libmp3lame ${CMAKE_CURRENT_BINARY_DIR}/lame-config)
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H TAKEHIRO_IEEE754_HACK _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(mp3lame PRIVATE /w)
else()
    target_compile_options(mp3lame PRIVATE -w)
endif()
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/lua.c ${lua_SOURCE_DIR}/luac.c ${lua_SOURCE_DIR}/onelua.c ${lua_SOURCE_DIR}/ltests.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
if(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()

juce_add_gui_app(TStudio
    PRODUCT_NAME "TStudio"
    COMPANY_NAME "TStudio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/logo.png")

file(GLOB TS_DEMOS CONFIGURE_DEPENDS Demos/*.tsp)
file(GLOB TS_SCRIPTS CONFIGURE_DEPENDS Scripts/*.lua)
file(GLOB TS_LUA_PLUGINS CONFIGURE_DEPENDS Plugins/*.lua)
file(GLOB TS_EXTENSIONS CONFIGURE_DEPENDS Extensions/*.lua)
juce_add_binary_data(TStudioData SOURCES Resources/logo.png ${TS_DEMOS} ${TS_SCRIPTS} ${TS_LUA_PLUGINS} ${TS_EXTENSIONS})

file(GLOB_RECURSE TS_SOURCES CONFIGURE_DEPENDS Source/*.cpp Source/*.h)
target_sources(TStudio PRIVATE ${TS_SOURCES})
target_include_directories(TStudio PRIVATE Source SDK)

target_compile_definitions(TStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:TStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:TStudio,JUCE_VERSION>")

target_link_libraries(TStudio
    PRIVATE TStudioData lua mp3lame juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(TSTUDIO_BUILD_LUA_LOADER "Build the TStudio Lua Loader VST3 plugins" ON)
if(TSTUDIO_BUILD_LUA_LOADER)
    file(GLOB TS_LOADER_SCRIPTS CONFIGURE_DEPENDS Plugins/*.lua)
    juce_add_binary_data(TStudioLoaderData HEADER_NAME TStudioLoaderData.h NAMESPACE TStudioLoaderData SOURCES ${TS_LOADER_SCRIPTS})
    foreach(kind IN ITEMS FX Synth)
        if(kind STREQUAL "Synth")
            set(ts_is_synth TRUE)
            set(ts_code Tlsy)
            set(ts_instrument 1)
        else()
            set(ts_is_synth FALSE)
            set(ts_code Tlfx)
            set(ts_instrument 0)
        endif()
        juce_add_plugin(TStudioLua${kind}
            PRODUCT_NAME "TStudio Lua ${kind}"
            COMPANY_NAME "TStudio"
            PLUGIN_MANUFACTURER_CODE Tmto
            PLUGIN_CODE ${ts_code}
            FORMATS VST3
            IS_SYNTH ${ts_is_synth}
            NEEDS_MIDI_INPUT TRUE
            COPY_PLUGIN_AFTER_BUILD FALSE)
        target_sources(TStudioLua${kind} PRIVATE Loader/LuaLoader.cpp Source/Shared/LuaPluginScript.cpp Source/Shared/LuaPluginUI.cpp Source/Shared/LuaVar.cpp Source/UI/LookAndFeel.cpp)
        target_include_directories(TStudioLua${kind} PRIVATE Source)
        target_compile_definitions(TStudioLua${kind} PRIVATE TS_LOADER_INSTRUMENT=${ts_instrument} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
        target_link_libraries(TStudioLua${kind}
            PRIVATE TStudioLoaderData lua juce::juce_audio_utils
            PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
    endforeach()
endif()

option(TSTUDIO_BUILD_NATIVE_EXAMPLE "Build the example native (C API) plugin library" ON)
if(TSTUDIO_BUILD_NATIVE_EXAMPLE)
    add_library(TomatoNative SHARED SDK/examples/tomato_native.c)
    set_target_properties(TomatoNative PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
    if(NOT WIN32)
        target_link_libraries(TomatoNative PRIVATE m)
    endif()
    add_custom_command(TARGET TomatoNative POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TStudio>/Plugins"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:TomatoNative>" "$<TARGET_FILE_DIR:TStudio>/Plugins/")
    add_dependencies(TStudio TomatoNative)
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME integration COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py" --exe "$<TARGET_FILE:TStudio>")
    set_tests_properties(integration PROPERTIES TIMEOUT 1800)
endif()
