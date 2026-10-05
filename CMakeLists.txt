cmake_minimum_required (VERSION 3.22)

# Su macOS: binario universale (Apple Silicon + Intel), si può cambiare da riga di comando.
if (APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif ()
set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project (Orecchio VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

option (ORECCHIO_COPY_PLUGIN "Copia il plugin nella cartella di sistema dei plugin dopo la build" OFF)
option (ORECCHIO_BUILD_TESTS "Compila anche il test del motore DSP" ON)

# JUCE: usa una copia locale se indicata (-DJUCE_PATH=...), altrimenti la scarica.
set (JUCE_PATH "" CACHE PATH "Cartella di JUCE già scaricata (facoltativo)")
if (JUCE_PATH)
    add_subdirectory ("${JUCE_PATH}" JUCE EXCLUDE_FROM_ALL)
else ()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable (JUCE)
endif ()

juce_add_plugin (Orecchio
    COMPANY_NAME "Orecchio"
    BUNDLE_ID "com.orecchio.orecchio"
    PLUGIN_MANUFACTURER_CODE Orec
    PLUGIN_CODE Orc1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Orecchio"
    DESCRIPTION "Vedi le note di un sample e costruisci basso, accordi e melodia"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Tools
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${ORECCHIO_COPY_PLUGIN})

target_sources (Orecchio PRIVATE
    Source/DSP.cpp
    Source/PianoRoll.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp)

target_compile_definitions (Orecchio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries (Orecchio
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (ORECCHIO_BUILD_TESTS)
    add_executable (orecchio_dsp_test tests/dsp_test.cpp Source/DSP.cpp)
    enable_testing ()
    add_test (NAME dsp COMMAND orecchio_dsp_test)
endif ()

# Test d'integrazione del motore (sviluppo): processore + editor in un'app da console, senza DAW.
option (ORECCHIO_ENGINE_TEST "Compila il test d'integrazione del motore audio" OFF)
if (ORECCHIO_ENGINE_TEST)
    juce_add_console_app (orecchio_engine_test PRODUCT_NAME "orecchio_engine_test")
    target_sources (orecchio_engine_test PRIVATE
        tests/engine_test.cpp
        Source/DSP.cpp
        Source/PianoRoll.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp)
    target_compile_definitions (orecchio_engine_test PRIVATE
        JucePlugin_Name="Orecchio"
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries (orecchio_engine_test PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif ()
