cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0091 NEW)   # honour CMAKE_MSVC_RUNTIME_LIBRARY
project(PeepChain VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
# Windows: link the C++ runtime statically, so the VST3 loads without the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

option(PEEPCHAIN_INSTALL "Copy the plugin into the system plugin folder after building" OFF)

# Keep PLUGIN_MANUFACTURER_CODE and PLUGIN_CODE as they are: DAWs use them to find the plugin in saved projects.
juce_add_plugin(PeepChain
    COMPANY_NAME "PeepChain"
    BUNDLE_ID com.peepchain.peepchain
    PLUGIN_MANUFACTURER_CODE Ppch
    PLUGIN_CODE Pch1
    PRODUCT_NAME "PeepChain"
    FORMATS AU VST3                    # AU is built on macOS only; the VST3 is the release
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD ${PEEPCHAIN_INSTALL})

set(PLUGIN_SOURCES src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
# Where the editor looks for newer releases: https://github.com/PeepChain/PeepChain. Set it to "" to switch the
# update check off.
set(PEEPCHAIN_GITHUB_REPO "PeepChain/PeepChain" CACHE STRING "GitHub owner/repo checked for updates")

set(PLUGIN_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_REPORT_APP_USAGE=0
    PEEPCHAIN_VERSION="${PROJECT_VERSION}" PEEPCHAIN_GITHUB_REPO="${PEEPCHAIN_GITHUB_REPO}")

# The reverb send is its measured impulse response, built into the plugin.
# The editor's font is built in too (Inter, SIL Open Font License, see assets/fonts/Inter-OFL.txt), so it looks
# the same on every system: SemiBold for the lettering, Bold for the name.
juce_add_binary_data(PeepChainData SOURCES assets/sends/reverb_ir.wav
                     assets/fonts/Inter-SemiBold.ttf assets/fonts/Inter-Bold.ttf)
set_target_properties(PeepChainData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
list(APPEND PLUGIN_DEFS PEEPCHAIN_HAS_REVERB=1)

target_sources(PeepChain PRIVATE ${PLUGIN_SOURCES})
target_compile_definitions(PeepChain PUBLIC ${PLUGIN_DEFS})
target_link_libraries(PeepChain
    PRIVATE juce::juce_audio_utils juce::juce_dsp PeepChainData
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

include(local.cmake OPTIONAL)    # optional local-only additions
