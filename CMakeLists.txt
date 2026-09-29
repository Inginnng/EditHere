cmake_minimum_required(VERSION 3.24)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
endif()
project(EditHere VERSION 0.9.7 LANGUAGES CXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Test)
qt_standard_project_setup()
set(CORE app/model.cpp app/detector.cpp app/table_detector.cpp app/layout.cpp app/fonts.cpp)
set(UI app/agentserver.cpp app/ui.cpp app/i18n.cpp app/canvas.cpp app/editor.cpp app/inlinenoteedit.cpp app/overlay.cpp app/controller.cpp app/explosion.cpp app/settings.cpp app/settingsdialog.cpp app/updatechecker.cpp app/projectfiles.cpp app/guide.cpp app/autostart.cpp app/ocr.cpp app/capturesession.cpp app/capturetoolbar.cpp app/pinwindow.cpp app/ocrdialog.cpp)
if(WIN32)
  set(PLATFORM app/platform_win.cpp)
elseif(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)
  set(PLATFORM app/platform_mac.mm app/autostart_mac.mm app/ocr_mac.mm)
else()
  message(FATAL_ERROR "This release targets Windows and macOS.")
endif()
add_library(h2d_core STATIC ${CORE})
target_include_directories(h2d_core PUBLIC app)
target_link_libraries(h2d_core PUBLIC Qt6::Gui)
add_library(h2d_agent STATIC app/agentprotocol.cpp app/agentconnection.cpp)
target_include_directories(h2d_agent PUBLIC app)
target_link_libraries(h2d_agent PUBLIC Qt6::Core Qt6::Network)
add_library(h2d_ui STATIC ${UI} ${PLATFORM} app/platform.h)
target_include_directories(h2d_ui PUBLIC app)
target_link_libraries(h2d_ui PUBLIC h2d_agent h2d_core Qt6::Widgets Qt6::Concurrent Qt6::Network)
if(WIN32)
  target_link_libraries(h2d_ui PRIVATE user32 gdi32 dwmapi ole32 oleaut32 uuid advapi32)
elseif(APPLE)
  target_compile_options(h2d_ui PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_libraries(h2d_ui PRIVATE "-framework Cocoa" "-framework ScreenCaptureKit" "-framework ApplicationServices" "-framework Carbon" "-framework ServiceManagement" "-framework Vision")
endif()
qt_add_executable(EditHere WIN32 MACOSX_BUNDLE app/main.cpp)
target_link_libraries(EditHere PRIVATE h2d_ui)
qt_add_executable(edithere-cli app/cli.cpp)
set_target_properties(edithere-cli PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
target_link_libraries(edithere-cli PRIVATE h2d_core h2d_agent)
target_compile_definitions(edithere-cli PRIVATE EDITHERE_VERSION="${PROJECT_VERSION}")
if(APPLE)
  add_dependencies(EditHere edithere-cli)
  add_custom_command(TARGET EditHere POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:edithere-cli>" "$<TARGET_BUNDLE_DIR:EditHere>/Contents/MacOS/edithere-cli"
    VERBATIM)
endif()
set(EDITHERE_ICON_PNGS)
foreach(size IN ITEMS 16 20 24 32 48 64 128 256 512)
  list(APPEND EDITHERE_ICON_PNGS "assets/icons/edithere-${size}.png")
endforeach()
qt_add_resources(EditHere app_icons
  PREFIX "/icons"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
  FILES ${EDITHERE_ICON_PNGS})
# The Windows screen reader is driven through a PowerShell bridge, so the script is
# embedded rather than installed beside the executable: a partially copied
# installation cannot lose it, and no script file has to be written at run time.
# scripts/check-packaging.py keeps it ASCII-only, which is what lets the bridge
# encode it as UTF-16LE without the byte order mark PowerShell would break on.
# It belongs to the static library so the tests can reach it as well.
if(WIN32)
  qt_add_resources(h2d_ui ocr_bridge
    PREFIX "/ocr"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/app"
    FILES app/ocrbridge.ps1)
endif()
# Interface translations. Simplified Chinese is the source language written in the
# code, so only English needs a .ts. The Linguist tools compile it when they are
# installed; the minimal Qt used by CI has no Linguist, which is why the compiled
# .qm is committed under app/translations/built and used as the fallback.
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
  qt_add_translations(EditHere TS_FILES app/translations/edithere_en.ts)
else()
  message(STATUS "Qt Linguist tools not found; embedding the prebuilt translations.")
  qt_add_resources(EditHere app_translations
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/app/translations/built"
    FILES app/translations/built/edithere_en.qm)
endif()
if(WIN32)
  enable_language(RC)
  configure_file(packaging/edithere.rc.in "${CMAKE_CURRENT_BINARY_DIR}/edithere.rc" @ONLY)
  target_sources(EditHere PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/edithere.rc")
elseif(APPLE)
  target_sources(EditHere PRIVATE assets/icons/edithere.icns)
  set_source_files_properties(assets/icons/edithere.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(EditHere PROPERTIES MACOSX_BUNDLE_ICON_FILE edithere.icns)
endif()
target_compile_definitions(h2d_ui PUBLIC EDITHERE_VERSION="${PROJECT_VERSION}")
set_target_properties(EditHere PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.edithere.capture"
  MACOSX_BUNDLE_BUNDLE_NAME "EditHere"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist")
# Package the version of the successfully linked executable, not just the latest configuration.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version-configured.txt" CONTENT "${PROJECT_VERSION}\n")
add_custom_command(TARGET EditHere POST_BUILD
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/version.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_BINARY_DIR}/version-configured.txt" "${CMAKE_CURRENT_BINARY_DIR}/version.txt"
  VERBATIM)
enable_testing()
qt_add_executable(agent_cli_tests tests/agent_cli_test.cpp)
target_link_libraries(agent_cli_tests PRIVATE h2d_ui Qt6::Test)
set_target_properties(agent_cli_tests PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)
add_dependencies(agent_cli_tests edithere-cli)
add_test(NAME agent-cli COMMAND agent_cli_tests -platform offscreen)
qt_add_executable(detector_tests tests/detector_test.cpp)
target_link_libraries(detector_tests PRIVATE h2d_core Qt6::Test)
add_test(NAME detector COMMAND detector_tests)
qt_add_executable(core_tests tests/core_test.cpp)
target_link_libraries(core_tests PRIVATE h2d_core Qt6::Test)
add_test(NAME core COMMAND core_tests)
qt_add_executable(layout_tests tests/layout_test.cpp)
target_link_libraries(layout_tests PRIVATE h2d_core Qt6::Test)
add_test(NAME layout COMMAND layout_tests)
qt_add_executable(ui_tests tests/ui_test.cpp)
target_link_libraries(ui_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME ui COMMAND ui_tests -platform offscreen)
qt_add_executable(inline_note_tests tests/inlinenoteedit_test.cpp)
target_link_libraries(inline_note_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME inline-note COMMAND inline_note_tests -platform offscreen)
qt_add_executable(canvas_feedback_tests tests/canvas_feedback_test.cpp)
target_link_libraries(canvas_feedback_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME canvas-feedback COMMAND canvas_feedback_tests -platform offscreen)
qt_add_executable(settings_tests tests/settings_test.cpp)
target_link_libraries(settings_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME settings COMMAND settings_tests -platform offscreen)
qt_add_executable(i18n_tests tests/i18n_test.cpp)
target_link_libraries(i18n_tests PRIVATE h2d_ui Qt6::Test)
# installLanguage() resolves ":/i18n/edithere_en.qm", which the application embeds. The
# test embeds the same committed file so it exercises that lookup instead of a path.
qt_add_resources(i18n_tests i18n_test_translations
  PREFIX "/i18n"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/app/translations/built"
  FILES app/translations/built/edithere_en.qm)
add_test(NAME i18n COMMAND i18n_tests -platform offscreen)
qt_add_executable(guide_tests tests/guide_test.cpp)
target_link_libraries(guide_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME guide COMMAND guide_tests -platform offscreen)
qt_add_executable(autostart_tests tests/autostart_test.cpp)
target_link_libraries(autostart_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME autostart COMMAND autostart_tests)
qt_add_executable(startup_flow_tests tests/startup_flow_test.cpp)
target_link_libraries(startup_flow_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME startup-flow COMMAND startup_flow_tests -platform offscreen)
qt_add_executable(update_tests tests/update_test.cpp)
target_link_libraries(update_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME updates COMMAND update_tests)
qt_add_executable(ocr_tests tests/ocr_test.cpp)
target_link_libraries(ocr_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME ocr COMMAND ocr_tests -platform offscreen)
qt_add_executable(capture_tests tests/capture_test.cpp)
target_link_libraries(capture_tests PRIVATE h2d_ui Qt6::Test)
add_test(NAME capture COMMAND capture_tests -platform offscreen)
if(WIN32)
  set_target_properties(settings_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  set_target_properties(i18n_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  qt_add_executable(platform_tests tests/platform_test.cpp)
  target_link_libraries(platform_tests PRIVATE h2d_ui Qt6::Test)
  set_target_properties(core_tests ui_tests layout_tests platform_tests PROPERTIES WIN32_EXECUTABLE FALSE)
  add_dependencies(platform_tests EditHere)
  add_test(NAME windows-platform COMMAND platform_tests)
endif()
# Guards for the incidents recorded in docs/REGRESSIONS.md: installer encoding, NSIS
# macros, the shape of the portable archive, release asset names and hashes, the
# version living in one place, and the committed translations matching their sources.
# check-translations.py needs lupdate, which the minimal Qt used by CI does not ship;
# the tool-independent half of the translation check lives in check-packaging.py.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME packaging
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-packaging.py")
  if(TARGET Qt6::lupdate)
    add_test(NAME translations
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-translations.py"
              --lupdate "$<TARGET_FILE:Qt6::lupdate>")
  else()
    message(STATUS "Qt Linguist tools not found; the translations test is not registered.")
  endif()
else()
  message(STATUS "Python 3 not found; the packaging and translations tests are not registered.")
endif()
install(TARGETS EditHere edithere-cli BUNDLE DESTINATION . RUNTIME DESTINATION .)
