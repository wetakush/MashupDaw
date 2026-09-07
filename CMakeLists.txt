cmake_minimum_required(VERSION 3.22)
project(MashupDaw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(MASHUP_BUILD_TESTS "Build unit tests" ON)
option(MASHUP_ENABLE_LTO "Link-time optimisation (slow link)" OFF)

# ---- third party -------------------------------------------------------------
add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample)
find_package(RubberBand REQUIRED)
find_package(FFTW3f REQUIRED)
find_package(Threads REQUIRED)

# ---- common library with all non-UI + UI code --------------------------------
file(GLOB_RECURSE MASHUP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
list(FILTER MASHUP_SOURCES EXCLUDE REGEX ".*/Source/Main\\.cpp$")

add_library(MashupCore STATIC ${MASHUP_SOURCES})
target_include_directories(MashupCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(MashupCore PUBLIC
  JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
  JUCE_STANDALONE_APPLICATION=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_VST=0
  JUCE_PLUGINHOST_AU=0
  JUCE_PLUGINHOST_LADSPA=0
  JUCE_USE_MP3AUDIOFORMAT=0
  JUCE_USE_FLAC=1
  JUCE_USE_OGGVORBIS=1
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0
  JUCE_APPLICATION_NAME_STRING="MashupDaw"
  JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
  MASHUP_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Scripts"
  $<$<CONFIG:Debug>:DEBUG=1>)
target_link_libraries(MashupCore PUBLIC
  juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
  juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
  juce::juce_data_structures juce::juce_dsp juce::juce_events juce::juce_graphics
  juce::juce_gui_basics juce::juce_gui_extra
  juce::juce_recommended_config_flags $<$<BOOL:${MASHUP_ENABLE_LTO}>:juce::juce_recommended_lto_flags>
  FFmpeg::FFmpeg RubberBand::RubberBand FFTW3::fftw3f Threads::Threads)
# juce_recommended_warning_flags is too noisy for FFmpeg headers; add our own
if(MSVC)
  target_compile_options(MashupCore PUBLIC /utf-8 /bigobj /wd4244 /wd4267 /wd4305)
  target_compile_definitions(MashupCore PUBLIC _USE_MATH_DEFINES NOMINMAX)
else()
  target_compile_options(MashupCore PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_compile_options(MashupCore PUBLIC -ffast-math -fno-finite-math-only)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(MashupCore PUBLIC -march=x86-64-v2)
  endif()
endif()

# ---- application --------------------------------------------------------------
juce_add_gui_app(MashupDaw
  PRODUCT_NAME "MashupDaw"
  COMPANY_NAME "MashupDaw"
  VERSION ${PROJECT_VERSION}
  ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png")
target_sources(MashupDaw PRIVATE Source/Main.cpp)
target_link_libraries(MashupDaw PRIVATE MashupCore)
target_compile_definitions(MashupDaw PRIVATE JUCE_APPLICATION_NAME_STRING="MashupDaw")

# ---- tests --------------------------------------------------------------------
if(MASHUP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()

# ---- developer tools ------------------------------------------------------------
juce_add_console_app(mashup_analyse PRODUCT_NAME "mashup_analyse")
target_sources(mashup_analyse PRIVATE Tools/AnalyseCli.cpp)
target_link_libraries(mashup_analyse PRIVATE MashupCore)

# ---- install / packaging ----------------------------------------------------------
include(GNUInstallDirs)
if(APPLE)
  install(TARGETS MashupDaw BUNDLE DESTINATION .)
  install(DIRECTORY Scripts/ DESTINATION MashupDaw.app/Contents/Resources/Scripts USE_SOURCE_PERMISSIONS)
elseif(WIN32)
  install(TARGETS MashupDaw RUNTIME DESTINATION .)
  install(DIRECTORY $<TARGET_FILE_DIR:MashupDaw>/ DESTINATION . FILES_MATCHING PATTERN "*.dll")
  install(DIRECTORY Scripts/ DESTINATION Scripts USE_SOURCE_PERMISSIONS)
else()
  install(TARGETS MashupDaw RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY Scripts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mashupdaw/scripts USE_SOURCE_PERMISSIONS)
  install(FILES Resources/mashupdaw.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES Resources/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps RENAME mashupdaw.png)
endif()
set(CPACK_PACKAGE_NAME "mashupdaw")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "MashupDaw")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop DAW for mashups and remixes")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
