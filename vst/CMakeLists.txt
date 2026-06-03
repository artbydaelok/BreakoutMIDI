cmake_minimum_required(VERSION 3.22)

project(BreakoutMIDI VERSION 0.1.0 LANGUAGES C CXX)

# ── JUCE ──────────────────────────────────────────────────────────────────────
# Point this at your JUCE 8 checkout. Override with -DJUCE_PATH=... if needed.
set(JUCE_PATH "C:/JUCE" CACHE PATH "Path to the JUCE framework checkout")
add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(BreakoutMIDI
    PLUGIN_MANUFACTURER_CODE Bdae
    PLUGIN_CODE Bkmi
    FORMATS VST3 Standalone
    PRODUCT_NAME "BreakoutMIDI"
    COMPANY_NAME "artbydaelok"
    BUNDLE_ID com.artbydaelok.breakoutmidi
    # An instrument (silent stereo out) rather than a pure MIDI effect: Ableton
    # rejects VST3s that have no audio output bus ("No valid output bus...").
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES "Instrument" "Tools"
    # Deploy is decoupled from the build (see deploy.ps1): auto-copy fails when
    # the DAW has the plugin file locked, which would otherwise red-X the build.
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Embed the existing single-file frontend (repo root index.html) into the binary.
juce_add_binary_data(BreakoutMIDIData
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../index.html)

target_sources(BreakoutMIDI PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(BreakoutMIDI PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    # NEEDS_WEBVIEW2 links the static loader; this tells JUCE to call it directly
    # instead of LoadLibrary("WebView2Loader.dll"). Without it the backend
    # silently falls back to the legacy IE browser (no resource provider).
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Flip to 1 to log served resources + every note into %TEMP%\breakoutmidi_midi.log
    BREAKOUT_MIDI_DEBUG_LOG=0)

target_link_libraries(BreakoutMIDI
    PRIVATE
        BreakoutMIDIData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
