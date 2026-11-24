cmake_minimum_required(VERSION 3.22)

project(GLOWTAG VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

# Use a local JUCE checkout if one is given (-DGLOWTAG_JUCE_DIR=/path/to/JUCE),
# otherwise download the pinned release.
set(GLOWTAG_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(GLOWTAG_JUCE_DIR)
    add_subdirectory(${GLOWTAG_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(Glowtag
    PRODUCT_NAME "GLOWTAG"
    COMPANY_NAME "Jack Millar"
    BUNDLE_ID "com.jackmillar.glowtag"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "GLOWTAG uses audio input to record and sample.")

target_sources(Glowtag PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Theme.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/Project.cpp
    Source/Engine/Patch.cpp
    Source/Engine/Graph.cpp
    Source/Engine/DeviceNodes.cpp
    Source/Engine/DrumNodes.cpp
    Source/Engine/NewDeviceNodes.cpp
    Source/Engine/Samples.cpp
    Source/Engine/Sequencer.cpp
    Source/Engine/PluginHost.cpp
    Source/Engine/VisListener.cpp
    Source/UI/Visualiser.cpp
    Source/UI/VisualiserRender.cpp
    Source/UI/GlowLookAndFeel.cpp
    Source/UI/Icons.cpp
    Source/UI/TopBar.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/TrackList.cpp
    Source/UI/ArrangerView.cpp
    Source/UI/DevicePanel.cpp
    Source/UI/DeviceCards.cpp
    Source/UI/SequencerPanel.cpp
    Source/UI/StatusBar.cpp
    Source/UI/MixerView.cpp
    Source/UI/EditorView.cpp
    Source/UI/DeviceWallModel.cpp
    Source/UI/DeviceWallArt.cpp
    Source/UI/DeviceWall.cpp
    Source/UI/NeonKit.cpp
    Source/UI/DeviceViews.cpp
    Source/UI/DeviceViewsFx.cpp
    Source/UI/DeviceViewsNew.cpp
    Source/UI/ExpandedDevice.cpp)
if(APPLE)
    target_sources(Glowtag PRIVATE Source/UI/VisualiserMac.mm)   # keeps the picture's window under the app's
endif()

# Lettering for the device wall (SIL Open Font License, see Source/Fonts)
juce_add_binary_data(GlowtagFonts SOURCES
    Source/Fonts/SedgwickAveDisplay-Regular.ttf
    Source/Fonts/Monoton-Regular.ttf
    Source/Fonts/BowlbyOne-Regular.ttf)

target_include_directories(Glowtag PRIVATE Source)

target_compile_definitions(Glowtag PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Glowtag,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Glowtag,JUCE_VERSION>")

target_link_libraries(Glowtag
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_opengl
        GlowtagFonts
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline engine checks: cmake -DGLOWTAG_TESTS=ON, then run GlowtagTests
option(GLOWTAG_TESTS "Build the engine tests" OFF)
if(GLOWTAG_TESTS)
    juce_add_console_app(GlowtagTests PRODUCT_NAME "GlowtagTests")
    target_sources(GlowtagTests PRIVATE
        Tests/EngineTests.cpp
        Source/Engine/AudioEngine.cpp
        Source/Engine/Project.cpp
        Source/Engine/Patch.cpp
        Source/Engine/Graph.cpp
        Source/Engine/DeviceNodes.cpp
        Source/Engine/DrumNodes.cpp
        Source/Engine/NewDeviceNodes.cpp
        Source/Engine/Samples.cpp
        Source/Engine/Sequencer.cpp
        Source/Engine/PluginHost.cpp
        Source/Engine/VisListener.cpp
        Source/UI/DeviceWallModel.cpp)
    target_include_directories(GlowtagTests PRIVATE Source)
    target_compile_definitions(GlowtagTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
    target_link_libraries(GlowtagTests PRIVATE
        juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
