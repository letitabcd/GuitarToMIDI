cmake_minimum_required(VERSION 3.22)

project(GuitarToMIDI VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Offline DSP tests (pure C++, no JUCE) — built first, no plugin deps
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(dsp_tests tests/dsp_tests.cpp Source/PitchEngine.cpp)
    add_test(NAME dsp_tests COMMAND dsp_tests)
    add_executable(dsp_regression_tests tests/dsp_regression_tests.cpp Source/PitchEngine.cpp)
    add_test(NAME dsp_regression_tests COMMAND dsp_regression_tests)
endif()

# ---------------------------------------------------------------------------
# JUCE plugin targets: AU audio effect with MIDI out + VST3 + Standalone
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Run: git submodule update --init --recursive (see README for source ZIP downloads).")
endif()
add_subdirectory(deps/JUCE EXCLUDE_FROM_ALL)

juce_add_binary_data(GuitarToMidiAssets
    HEADER_NAME GuitarToMidiAssets.h
    NAMESPACE GuitarToMidiAssets
    SOURCES Assets/harmonic_strings.png
)

juce_add_plugin(GuitarToMIDI
    COMPANY_NAME "renyi"
    BUNDLE_ID "com.renyi.guitartomidi"
    PLUGIN_MANUFACTURER_CODE Rnyi
    PLUGIN_CODE Gtmd
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Guitar To MIDI"
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Guitar To MIDI needs audio input to convert your guitar DI signal into MIDI."
)

target_sources(GuitarToMIDI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PluginLookAndFeel.cpp
    Source/PitchEngine.cpp
    Source/VirtualMidiOutput.cpp
)

target_compile_definitions(GuitarToMIDI PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_sources(GuitarToMIDI_Standalone PRIVATE Source/Standalone.cpp)

if(BUILD_TESTING)
    add_executable(processor_tests tests/processor_tests.cpp)
    target_link_libraries(processor_tests PRIVATE GuitarToMIDI juce::juce_audio_utils)
    add_test(NAME processor_tests COMMAND processor_tests)
    if(APPLE)
        add_executable(au_integration_tests tests/au_integration_tests.cpp)
        target_link_libraries(au_integration_tests PRIVATE "-framework AudioToolbox" "-framework CoreMIDI" "-framework CoreFoundation")
        add_dependencies(au_integration_tests GuitarToMIDI_AU)
        add_test(NAME au_integration_tests COMMAND au_integration_tests "$<TARGET_BUNDLE_DIR:GuitarToMIDI_AU>")
    endif()
endif()

target_link_libraries(GuitarToMIDI
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        GuitarToMidiAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
