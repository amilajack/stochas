# Build the original processor/editor against a build-local JUCE platform overlay.
cmake_minimum_required(VERSION 3.20)
project(StochasWeb VERSION 1.3.10 LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with the pinned Emscripten emcmake tool")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(STOCHAS_WCLAP_DSP "Build the original processor as standalone pure-WASI CLAP" OFF)
get_filename_component(STOCHAS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(overlay "${CMAKE_BINARY_DIR}/juce-browser")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/prepare-juce.py"
  "${STOCHAS_ROOT}/lib/JUCE" "${overlay}" COMMAND_ERROR_IS_FATAL ANY)
set(binary "${CMAKE_BINARY_DIR}/binary")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/binary-data.py"
  "${STOCHAS_ROOT}" "${binary}" COMMAND_ERROR_IS_FATAL ANY)
add_library(stochas-juce STATIC)
foreach(module juce_core juce_events juce_data_structures juce_graphics juce_gui_basics
               juce_gui_extra juce_audio_basics juce_audio_processors)
  target_sources(stochas-juce PRIVATE "${overlay}/modules/${module}/${module}.cpp")
endforeach()
target_sources(stochas-juce PRIVATE
  "${overlay}/modules/juce_graphics/juce_graphics_Harfbuzz.cpp"
  "${overlay}/modules/juce_graphics/juce_graphics_Sheenbidi.c")
target_include_directories(stochas-juce PUBLIC "${overlay}/modules" PRIVATE juce)
if(STOCHAS_WCLAP_DSP)
  target_compile_options(stochas-juce PUBLIC -msimd128 -ffp-contract=off -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 PRIVATE -sUSE_FREETYPE=1)
  target_link_options(stochas-juce INTERFACE -msimd128 -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0 -sUSE_FREETYPE=1)
  target_compile_definitions(stochas-juce PUBLIC STOCHAS_WCLAP_DSP=1)
else()
  target_compile_options(stochas-juce PUBLIC -msimd128 -pthread -fexceptions PRIVATE -sUSE_FREETYPE=1)
  target_link_options(stochas-juce INTERFACE -msimd128 -pthread -fexceptions -sUSE_FREETYPE=1)
endif()
target_compile_definitions(stochas-juce PUBLIC
  JUCE_LITTLE_ENDIAN=1 JUCE_32BIT=1
  SUPPORT_AFFINITIES=0 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=0
  JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_USE_FREETYPE=1 JUCE_USE_FONTCONFIG=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0
  JUCE_USE_XSHM=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0
  JUCE_PLUGINHOST_VST=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0
  JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_LV2=0)
set(processor_sources CommonComponents EditDialog EditorState FileDialog HelpBanner InfoDialog
  MidiDialog NotePanel OptionsPanel Persist PlayPanel PluginEditor PluginProcessor Scale
  SeqRandom SequenceData StateValidation SettingsTab StepPanel StandaloneSettings TransportDialog StochaEngine)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/version-data.py"
  "${STOCHAS_ROOT}" "${binary}/version.cpp" COMMAND_ERROR_IS_FATAL ANY)
add_library(stochas-editor STATIC "${binary}/BinaryData.cpp" "${binary}/version.cpp" "${STOCHAS_ROOT}/src/pcg_basic.c")
foreach(source IN LISTS processor_sources)
  target_sources(stochas-editor PRIVATE "${STOCHAS_ROOT}/src/${source}.cpp")
endforeach()
target_include_directories(stochas-editor PUBLIC include "${STOCHAS_ROOT}/src" "${binary}")
target_compile_definitions(stochas-editor PUBLIC JucePlugin_Name="Stochas"
  JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=1
  JucePlugin_IsSynth=0 JucePlugin_Build_Standalone=0 JUCE_TARGET_HAS_BINARY_DATA=1)
target_link_libraries(stochas-editor PUBLIC stochas-juce)
if(STOCHAS_WCLAP_DSP)
  add_executable(stochas-module wclap/Entry.cpp wclap/Plugin.cpp wclap/Wasi.cpp wclap/SequenceDelta.cpp)
  target_include_directories(stochas-module PRIVATE "${STOCHAS_ROOT}/lib/clap-juce-extensions/clap-libs/clap/include" vendor/clap-webview/include)
  target_link_libraries(stochas-module PRIVATE stochas-editor)
  set_target_properties(stochas-module PROPERTIES OUTPUT_NAME module SUFFIX .wasm RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/wclap")
  target_link_options(stochas-module PRIVATE -sSTANDALONE_WASM=1 -sPURE_WASI=1 --no-entry
    -sALLOW_TABLE_GROWTH=1
    -sINITIAL_MEMORY=33554432 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=536870912
    -sSTACK_SIZE=2097152 "-sEXPORTED_FUNCTIONS=['_malloc','_free']"
    -Wl,--export=clap_entry -Wl,--export-table --profiling-funcs)
  return()
endif()
add_executable(stochas-ui UiMain.cpp UiBridge.cpp wclap/SequenceDelta.cpp)
target_include_directories(stochas-ui PRIVATE "${STOCHAS_ROOT}/lib/clap-juce-extensions/clap-libs/clap/include")
target_link_libraries(stochas-ui PRIVATE stochas-editor)
set_target_properties(stochas-ui PROPERTIES SUFFIX .js RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ui")
target_link_options(stochas-ui PRIVATE
  -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=2 -sSTACK_SIZE=2097152
  -sINITIAL_MEMORY=67108864 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=536870912
  -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1 -lidbfs.js
  --preload-file "${CMAKE_CURRENT_SOURCE_DIR}/fonts@/fonts"
  "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/browser/worker-canvas.js"
  "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/browser/platform.js"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','FS','stringToNewUTF8','specialHTMLTargets','HEAPU8','HEAPU16','HEAPF32']")
