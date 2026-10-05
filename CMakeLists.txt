cmake_minimum_required(VERSION 3.22)

project(BandTrigger VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on Mac (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Copy finished plug-ins into your user plug-in folders after building.
# Off on Windows, where the VST3 folder needs admin rights.
if(WIN32)
    set(BT_COPY_AFTER_BUILD FALSE)
else()
    set(BT_COPY_AFTER_BUILD TRUE)
endif()

set(BT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BT_FORMATS AU)
endif()

# Two plugins from the same code:
#   BandTrigger             - audio effect: insert it on the drum track.
#                             For Reaper, Bitwig, Cubase, Studio One, FL Studio...
#   BandTrigger Instrument  - instrument with a sidechain input: put it on a MIDI
#                             track and sidechain the drum track into it.
#                             For Ableton Live, which takes plug-in MIDI out
#                             from MIDI tracks.
function(bandtrigger_add_plugin target product code is_synth categories au_type)
    juce_add_plugin(${target}
        COMPANY_NAME                "BandTrigger"
        BUNDLE_ID                   com.bandtrigger.${code}
        PLUGIN_MANUFACTURER_CODE    Bntg          # 4 chars, at least one uppercase
        PLUGIN_CODE                 ${code}       # 4 chars, unique per plugin
        PRODUCT_NAME                "${product}"
        FORMATS                     ${BT_FORMATS}
        IS_SYNTH                    ${is_synth}
        NEEDS_MIDI_INPUT            ${is_synth}   # Live won't open an instrument without a MIDI input
        NEEDS_MIDI_OUTPUT           TRUE          # sends the trigger notes
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE          # for the name field
        COPY_PLUGIN_AFTER_BUILD     ${BT_COPY_AFTER_BUILD}
        VST3_CATEGORIES             ${categories}
        AU_MAIN_TYPE                ${au_type})

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

bandtrigger_add_plugin(BandTrigger     "BandTrigger"            Btr1 FALSE "Fx Analyzer"  kAudioUnitType_Effect)
bandtrigger_add_plugin(BandTriggerInst "BandTrigger Instrument" Btr2 TRUE  "Instrument"   kAudioUnitType_MusicDevice)

# Detector unit test (plain C++, no JUCE)
add_executable(EngineTest Tests/EngineTest.cpp)
target_include_directories(EngineTest PRIVATE Source)
