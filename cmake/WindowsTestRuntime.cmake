cmake_minimum_required(VERSION 3.24)

if(CMAKE_SCRIPT_MODE_FILE)
  if(NOT IS_ABSOLUTE "${runtime_dir}" OR NOT runtime_dlls OR NOT platform_plugins)
    message(FATAL_ERROR "Run Windows test runtime setup through CTest.")
  endif()

  # The Windows loader may ignore PATH. Stage DLLs beside the test executables,
  # and fail in CTest before launching any of them if a required file is missing.
  function(stage_runtime source destination)
    if(NOT EXISTS "${source}")
      message(FATAL_ERROR "Windows test runtime is missing: ${source}\nRestore the configured Qt/compiler installation, then reconfigure the build.")
    endif()
    get_filename_component(name "${source}" NAME)
    file(COPY_FILE "${source}" "${destination}/${name}" ONLY_IF_DIFFERENT)
  endfunction()

  if(with_multimedia)
    # FFmpeg is loaded at runtime by Qt Multimedia and is not a CMake link target.
    file(GLOB ffmpeg_dlls LIST_DIRECTORIES false
      "${qt_bin}/avcodec-*.dll" "${qt_bin}/avformat-*.dll" "${qt_bin}/avutil-*.dll"
      "${qt_bin}/swresample-*.dll" "${qt_bin}/swscale-*.dll")
  endif()
  file(MAKE_DIRECTORY "${runtime_dir}/platforms" "${test_temp}")
  foreach(dll IN LISTS runtime_dlls ffmpeg_dlls)
    stage_runtime("${dll}" "${runtime_dir}")
  endforeach()
  foreach(plugin IN LISTS platform_plugins)
    stage_runtime("${plugin}" "${runtime_dir}/platforms")
  endforeach()
  return()
endif()

function(edithere_prepare_windows_qt_tests output_target)
  get_target_property(qt_core_type Qt6::Core TYPE)
  if(NOT qt_core_type STREQUAL "SHARED_LIBRARY")
    return()
  endif()

  set(runtime_dlls)
  foreach(module Core Gui Widgets Network Svg Test Concurrent Multimedia MultimediaWidgets)
    if(TARGET Qt6::${module})
      list(APPEND runtime_dlls "$<TARGET_FILE:Qt6::${module}>")
    endif()
  endforeach()
  if(MSVC)
    # Use the compiler's redistributable files when available, without changing
    # install rules or installing anything into Windows.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_NO_WARNINGS TRUE)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
      set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    endif()
    include(InstallRequiredSystemLibraries)
    list(APPEND runtime_dlls ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
  elseif(MINGW)
    get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
      list(APPEND runtime_dlls "${compiler_bin}/${dll}")
    endforeach()
  endif()

  set(runtime_dir "$<TARGET_FILE_DIR:${output_target}>")
  set(test_temp "${runtime_dir}/test-tmp")
  if(NOT "$ENV{EDITHERE_TEST_TEMP}" STREQUAL "")
    if(NOT IS_ABSOLUTE "$ENV{EDITHERE_TEST_TEMP}")
      message(FATAL_ERROR "EDITHERE_TEST_TEMP must be an absolute directory.")
    endif()
    set(test_temp "$ENV{EDITHERE_TEST_TEMP}")
  endif()
  set(platform_plugins "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>;$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>")
  add_test(NAME windows-qt-runtime
    COMMAND "${CMAKE_COMMAND}"
      "-Druntime_dir=${runtime_dir}"
      "-Dtest_temp=${test_temp}"
      "-Druntime_dlls=${runtime_dlls}"
      "-Dqt_bin=$<TARGET_FILE_DIR:Qt6::Core>"
      "-Dwith_multimedia=$<TARGET_EXISTS:Qt6::Multimedia>"
      "-Dplatform_plugins=${platform_plugins}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
  set_tests_properties(windows-qt-runtime PROPERTIES FIXTURES_SETUP windows-qt-runtime)
  set_property(TEST ${ARGN} APPEND PROPERTY FIXTURES_REQUIRED windows-qt-runtime)
  set_property(TEST ${ARGN} APPEND PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM_PLUGIN_PATH=${runtime_dir}/platforms"
    "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/.."
    "TEMP=${test_temp}"
    "TMP=${test_temp}")
endfunction()
