cmake_minimum_required(VERSION 3.22)
project(Curio VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/MP)   # parallel compile per project
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no VC++ redist needed
endif()

# JUCE: uses ./JUCE if present, otherwise downloads it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Windows first: VST3 + Standalone. (Mac AU can be added later.)
set(CURIO_FORMATS VST3 Standalone)
option(CURIO_STANDALONE "Also build standalone apps" ON)
if(NOT CURIO_STANDALONE)
    set(CURIO_FORMATS VST3)
endif()

# name, 4-char code, is synth, wants MIDI, VST3 categories, source dir
function(curio_plugin NAME CODE SYNTH MIDI CATS DIR)
    juce_add_plugin(${NAME}
        COMPANY_NAME "Andrew"
        PRODUCT_NAME "${NAME}"
        BUNDLE_ID com.andrew.curio.${NAME}
        PLUGIN_MANUFACTURER_CODE Andw
        PLUGIN_CODE ${CODE}
        FORMATS ${CURIO_FORMATS}
        IS_SYNTH ${SYNTH}
        NEEDS_MIDI_INPUT ${MIDI}
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES ${CATS}
        COPY_PLUGIN_AFTER_BUILD FALSE)
    file(GLOB SRC "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${DIR}/*.cpp")
    target_sources(${NAME} PRIVATE ${SRC})
    target_compile_definitions(${NAME} PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${NAME}
        PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endfunction()

curio_plugin(Jitter       Cjit FALSE FALSE "Fx|Modulation"   Jitter)
curio_plugin(Repitch      Crep FALSE FALSE "Fx|Delay"        Repitch)
curio_plugin(Duck         Cduk FALSE TRUE  "Fx|Dynamics"     Duck)
curio_plugin(KeytrackRes  Ckey FALSE TRUE  "Fx|Filter"       KeytrackRes)
curio_plugin(CombPluck    Ccmb TRUE  TRUE  "Instrument|Synth" CombPluck)
curio_plugin(Manifest     Cman FALSE FALSE "Fx"              Manifest)
curio_plugin(Mudpie       Cmud FALSE TRUE  "Fx|Sampler"      Mudpie)
curio_plugin(SpectralOTT  Cspo FALSE FALSE "Fx|Dynamics"     SpectralOTT)

# Offline engine test (no JUCE): ctest / run curio_render_test
add_executable(curio_render_test test/render_all.cpp)
