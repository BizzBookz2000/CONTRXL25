cmake_minimum_required(VERSION 3.22)

# macOS: build for the Mac you are on (Intel = x86_64, Apple silicon = arm64) and run on macOS 10.13+.
# For one app that runs on both kinds of Mac:  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(CONTRXL25 VERSION 0.76.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build the static MSVC runtime so the .exe runs on any Windows PC without
# installing the Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ------------------------------------------------------------------
# JUCE
#   1. If a folder called JUCE sits next to this file (or JUCE_DIR is set),
#      that copy is used.
#   2. Otherwise JUCE (8.0.12 by default, which knows Visual Studio 2026) is downloaded automatically from GitHub.
# ------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a local JUCE checkout")

set(CONTRXL_JUCE_VERSION "8.0.12" CACHE STRING "JUCE release to download when no local copy is found")

if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local JUCE at ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" JUCE_build)
else()
    # Downloaded as a zip from GitHub, so Git does not need to be installed.
    message(STATUS "Downloading JUCE ${CONTRXL_JUCE_VERSION} (first configure only, about 70 MB)...")
    include(FetchContent)
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/${CONTRXL_JUCE_VERSION}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(CONTRXL25
    PRODUCT_NAME        "CONTRXL 25"
    COMPANY_NAME        "Illahtainment"
    BUNDLE_ID           "com.illahtainment.contrxl25"
    VERSION             "0.76.1"
    ICON_BIG            "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    DOCUMENT_EXTENSIONS zip wav aif aiff flac mp3 ogg m4a
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "CONTRXL 25 records from your audio input when you press Sample.")

target_sources(CONTRXL25 PRIVATE
    Source/Main.cpp
    Source/model/Model.cpp
    Source/engine/Engine.cpp
    Source/engine/Snapshot.cpp
    Source/engine/Effects.cpp
    Source/engine/Plugins.cpp
    Source/dsp/Offline.cpp
    Source/dsp/Stems.cpp
    Source/io/SongIO.cpp
    Source/io/Library.cpp
    Source/app/PluginManager.cpp
    Source/app/PluginScanner.cpp
    Source/app/Controller.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/WaveformView.cpp
    Source/ui/KeyboardView.cpp
    Source/ui/SequencerView.cpp
    Source/ui/Panels.cpp
    Source/ui/ChopEditor.cpp
    Source/ui/WindowStyle.cpp
    Source/ui/MidiLearn.cpp
    Source/ui/Mixer.cpp
    Source/ui/PluginBrowser.cpp
    Source/ui/PluginScanPanel.cpp
    Source/ui/MainComponent.cpp)

if(APPLE)
    target_sources(CONTRXL25 PRIVATE Source/ui/WindowStyle_mac.mm)
    # Audio Unit hosting (instruments + effects) next to VST3
    target_link_libraries(CONTRXL25 PRIVATE "-framework AudioUnit" "-framework CoreAudioKit")
    set(CX_HOST_AU 1)
else()
    set(CX_HOST_AU 0)
endif()

target_include_directories(CONTRXL25 PRIVATE Source)

target_compile_definitions(CONTRXL25 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CONTRXL25,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CONTRXL25,JUCE_VERSION>"
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_ASIO=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=${CX_HOST_AU}
    JUCE_PLUGINHOST_LADSPA=0)

target_link_libraries(CONTRXL25
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# ------------------------------------------------------------------
# Plug-in scanner helper: opens ONE plug-in per run, so a plug-in that crashes or hangs while
# being scanned only takes the helper down, never CONTRXL 25. It is copied next to the app's
# executable (inside "CONTRXL 25.app/Contents/MacOS" on a Mac, next to the .exe on Windows).
juce_add_console_app(CONTRXL25_PluginScanner PRODUCT_NAME "CONTRXL25_PluginScanner")
target_sources(CONTRXL25_PluginScanner PRIVATE Source/scanner/ScannerMain.cpp)
target_compile_definitions(CONTRXL25_PluginScanner PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=${CX_HOST_AU} JUCE_PLUGINHOST_LADSPA=0)
target_link_libraries(CONTRXL25_PluginScanner PRIVATE juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)
if(APPLE)
    target_link_libraries(CONTRXL25_PluginScanner PRIVATE "-framework AudioUnit" "-framework CoreAudioKit")
endif()
add_dependencies(CONTRXL25 CONTRXL25_PluginScanner)
add_custom_command(TARGET CONTRXL25 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CONTRXL25_PluginScanner>" "$<TARGET_FILE_DIR:CONTRXL25>/"
    COMMENT "Copying the plug-in scanner helper next to CONTRXL 25")

# Optional headless engine test (no window, no audio device):
#   cmake -B build -DCONTRXL_BUILD_TESTS=ON
option(CONTRXL_BUILD_TESTS "Build the headless engine test" OFF)
if(CONTRXL_BUILD_TESTS)
    juce_add_console_app(EngineTest PRODUCT_NAME "EngineTest")
    target_sources(EngineTest PRIVATE
        Tests/EngineTest.cpp
        Source/model/Model.cpp
        Source/engine/Engine.cpp
        Source/engine/Snapshot.cpp
        Source/engine/Effects.cpp
        Source/engine/Plugins.cpp
        Source/app/PluginManager.cpp
        Source/app/PluginScanner.cpp
        Source/dsp/Offline.cpp
        Source/dsp/Stems.cpp
        Source/io/SongIO.cpp
        Source/io/Library.cpp)
    target_include_directories(EngineTest PRIVATE Source)
    target_compile_definitions(EngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0)
    target_link_libraries(EngineTest PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(ControllerTest PRODUCT_NAME "ControllerTest")
    target_sources(ControllerTest PRIVATE
        Tests/ControllerTest.cpp
        Source/model/Model.cpp
        Source/engine/Engine.cpp
        Source/engine/Snapshot.cpp
        Source/engine/Effects.cpp
        Source/engine/Plugins.cpp
        Source/app/PluginManager.cpp
        Source/app/PluginScanner.cpp
        Source/app/Controller.cpp
        Source/dsp/Offline.cpp
        Source/dsp/Stems.cpp
        Source/io/SongIO.cpp
        Source/io/Library.cpp)
    target_include_directories(ControllerTest PRIVATE Source)
    target_compile_definitions(ControllerTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0)
    target_link_libraries(ControllerTest PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)

    # safe plug-in scanning check:  ScanTest <folder with .vst3 plug-ins> [timeout s]
    juce_add_console_app(ScanTest PRODUCT_NAME "ScanTest")
    target_sources(ScanTest PRIVATE Tests/ScanTest.cpp Source/app/PluginManager.cpp Source/app/PluginScanner.cpp
        Source/engine/Plugins.cpp Source/model/Model.cpp)
    target_include_directories(ScanTest PRIVATE Source)
    target_compile_definitions(ScanTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LADSPA=0)
    target_link_libraries(ScanTest PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
    add_dependencies(ScanTest CONTRXL25_PluginScanner)
    add_custom_command(TARGET ScanTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CONTRXL25_PluginScanner>" "$<TARGET_FILE_DIR:ScanTest>/")
endif()
