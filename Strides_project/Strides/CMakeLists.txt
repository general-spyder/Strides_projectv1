cmake_minimum_required(VERSION 3.22)
project(Strides VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime -> the .exe runs on any Windows PC without extra installs
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Strides
  COMPANY_NAME "Strides Audio"
  PRODUCT_NAME "Strides"
  FORMATS Standalone VST3
  PLUGIN_MANUFACTURER_CODE Strd
  PLUGIN_CODE Strd
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Analyzer
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Strides PRIVATE
  Source/Analyzer.cpp
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

# Poppins fonts: drop Poppins-Regular.ttf / Poppins-SemiBold.ttf / Poppins-Bold.ttf into Resources/
file(GLOB FONT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Poppins-*.ttf")
if(FONT_FILES)
  juce_add_binary_data(StridesFonts SOURCES ${FONT_FILES})
  target_link_libraries(Strides PRIVATE StridesFonts)
  target_compile_definitions(Strides PRIVATE STRIDES_HAS_FONTS=1)
else()
  message(WARNING "No Poppins fonts found in Resources/ - UI will use an installed Poppins or a fallback font.")
  target_compile_definitions(Strides PRIVATE STRIDES_HAS_FONTS=0)
endif()

target_compile_definitions(Strides PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_USE_WINDOWS_MEDIA_FORMAT=0)

target_link_libraries(Strides PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)
