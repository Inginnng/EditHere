# QtKeychain 0.15.0 adds the MSVC-only /utf-8 flag to every Windows compiler.
# Patch the pinned source before its subdirectory is configured, including for
# standalone AI-module exports. Preserve the original flag for MSVC.
set(path "${SOURCE_DIR}/qtkeychain/CMakeLists.txt")
file(READ "${path}" source)
set(original "    add_definitions( /utf-8 -DUNICODE )")
set(patched "    if(MSVC)\n        add_compile_options(/utf-8)\n    endif()\n    add_compile_definitions(UNICODE)")
if(source MATCHES "add_definitions\\( /utf-8 -DUNICODE \\)")
  string(REPLACE "${original}" "${patched}" source "${source}")
  file(WRITE "${path}" "${source}")
elseif(NOT source MATCHES "add_compile_definitions\\(UNICODE\\)")
  message(FATAL_ERROR "QtKeychain compiler-flag patch no longer matches the pinned source.")
endif()
