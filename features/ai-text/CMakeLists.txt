cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(EditHereAiText VERSION 0.13.0 LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_AUTOMOC ON)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
  qt_standard_project_setup()
endif()

# On this Chinese MSVC installation the compiler emits UTF-8, but CMake can
# record its include prefix as GBK-decoded text. Ninja then misses header edits.
if(MSVC AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX MATCHES "^娉ㄦ剰")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

get_filename_component(EDITHERE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
# The same native keychain backend is used by the application and this module.
include(FetchContent)
set(BUILD_WITH_QT6 ON CACHE BOOL "Build QtKeychain against Qt 6" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build bundled dependencies statically" FORCE)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "Do not build QtKeychain translations" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build dependency tests" FORCE)
FetchContent_Declare(qtkeychain
  URL https://codeload.github.com/frankosterfeld/qtkeychain/tar.gz/refs/tags/0.15.0
  URL_HASH SHA256=f4254dc8f0933b06d90672d683eab08ef770acd8336e44dfa030ce041dc2ca22
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(qtkeychain)

# OBJECT keeps the application's existing static link graph intact. Both entry
# points compile and run exactly these widgets, formatter and API client.
add_library(h2d_ai_text OBJECT
  src/aiconfig.cpp src/aifeedback.cpp src/aifeedbackformat.cpp src/aistyle.cpp
  src/aicredentials.cpp src/aiprovidericons.cpp
  src/aiinstructionsdialog.cpp src/aisettingspage.cpp
  include/aifeedback.h include/aicredentials.h
  include/aiinstructionsdialog.h include/aisettingspage.h)
target_include_directories(h2d_ai_text PUBLIC include PRIVATE "${EDITHERE_ROOT}/app")
target_compile_definitions(h2d_ai_text PRIVATE EDITHERE_VERSION="${PROJECT_VERSION}")
target_link_libraries(h2d_ai_text PUBLIC Qt6::Widgets Qt6::Svg Qt6::Network PRIVATE qt6keychain)
qt_add_resources(h2d_ai_text ai_assets PREFIX "/ai" BASE resources
  FILES resources/provider-presets.json resources/instructions-prompt.txt
    resources/analysis.qss resources/settings.qss)
qt_add_resources(h2d_ai_text ai_provider_icons PREFIX "/ai/providers" BASE resources/providers
  FILES resources/providers/openai.svg resources/providers/claude.svg
    resources/providers/deepseek.svg resources/providers/glm.svg resources/providers/kimi.svg)

# A small host for the shared theme and icons, without capture, video or editor.
add_library(ai_text_workbench_host STATIC EXCLUDE_FROM_ALL
  "${EDITHERE_ROOT}/app/ui.cpp" "${EDITHERE_ROOT}/app/fonts.cpp" "${EDITHERE_ROOT}/app/i18n.cpp")
target_include_directories(ai_text_workbench_host PUBLIC "${EDITHERE_ROOT}/app" include)
target_link_libraries(ai_text_workbench_host PUBLIC Qt6::Widgets Qt6::Svg)
file(GLOB AI_TEXT_UI_ICONS CONFIGURE_DEPENDS "${EDITHERE_ROOT}/assets/ui-icons/icons/*.svg")
qt_add_resources(ai_text_workbench_host ui_icons PREFIX "/ui-icons"
  BASE "${EDITHERE_ROOT}/assets/ui-icons"
  FILES "${EDITHERE_ROOT}/assets/ui-icons/manifest.json" ${AI_TEXT_UI_ICONS})
qt_add_executable(ai_text_workbench WIN32 EXCLUDE_FROM_ALL workbench/main.cpp)
target_compile_definitions(ai_text_workbench PRIVATE AI_TEXT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ai_text_workbench PRIVATE h2d_ai_text ai_text_workbench_host)
qt_add_resources(ai_text_workbench ai_text_samples PREFIX "/ai-samples" BASE workbench/fixtures
  FILES workbench/fixtures/screen.svg workbench/fixtures/screen-result.svg
    workbench/fixtures/feedback.json workbench/fixtures/reply.json)
set_target_properties(ai_text_workbench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" MACOSX_BUNDLE FALSE)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  qt_add_executable(ai_feedback_tests tests/aifeedback_test.cpp)
  target_link_libraries(ai_feedback_tests PRIVATE h2d_ai_text ai_text_workbench_host Qt6::Test)
  set_target_properties(ai_feedback_tests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
  add_test(NAME ai-feedback COMMAND ai_feedback_tests)
endif()
