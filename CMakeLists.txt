cmake_minimum_required(VERSION 3.22)
project(YOLO_TUNE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# JUCE is downloaded automatically the first time you configure.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(YOLO
    PRODUCT_NAME                "YOLO Tune"
    COMPANY_NAME                "KINOK9 x RAZEE"
    BUNDLE_ID                   com.kinok9razee.yolotune
    PLUGIN_MANUFACTURER_CODE    Kn9R
    PLUGIN_CODE                 Yolo
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "YOLO Tune's standalone app tunes your voice from the microphone."
    NEEDS_WEBVIEW2              TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

juce_generate_juce_header(YOLO)

target_sources(YOLO PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# The HTML interface is compiled into the plugin.
juce_add_binary_data(YoloUI SOURCES ui/index.html)

target_compile_definitions(YOLO PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(YOLO
    PRIVATE
        YoloUI
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
