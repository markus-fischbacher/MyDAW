cmake_minimum_required(VERSION 3.22)

project(Arranger VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
endif()

# JUCE + tracktion_engine are vendored as a git checkout in libs/
set(TE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/tracktion_engine)
add_subdirectory(${TE_DIR}/modules/juce ${CMAKE_BINARY_DIR}/juce)
add_subdirectory(${TE_DIR}/modules ${CMAKE_BINARY_DIR}/tracktion)

juce_add_gui_app(Arranger
    PRODUCT_NAME "Arranger"
    BUNDLE_ID "com.maxx.arranger"
    COMPANY_NAME "Arranger"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio recording"
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    PLUGINHOST_AU TRUE)

juce_generate_juce_header(Arranger)

target_sources(Arranger PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ArrangementView.cpp
    Source/PianoRoll.cpp
    Source/TransportBar.cpp
    Source/MixerView.cpp
    Source/PluginUI.cpp
    Source/BrowserPanel.cpp)

target_include_directories(Arranger PRIVATE Source)

target_compile_definitions(Arranger PRIVATE
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Arranger,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Arranger,JUCE_VERSION>"
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

if(APPLE)
    target_compile_options(Arranger PRIVATE -fno-aligned-allocation)
endif()

target_link_libraries(Arranger PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
