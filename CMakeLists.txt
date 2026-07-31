cmake_minimum_required(VERSION 3.20)
# Keep PROJECT_VERSION in sync with the macOS port's plugin version.
project(CSVLint VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Host plugin interface + Scintilla (read-only; NO host changes)
set(NPP_LINUX_DIR      "${CMAKE_SOURCE_DIR}/../../nextpad-plus-plus-gtk4")
set(NPP_PLUGIN_INCLUDE "${NPP_LINUX_DIR}/src")
set(SCINTILLA_INCLUDE  "${NPP_LINUX_DIR}/scintilla/include")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk4 glib-2.0 gio-2.0)

set(CSV_ENGINE_SOURCES
  src/engine/CsvDefinition.cpp
  src/engine/CsvAnalyze.cpp
  src/engine/CsvValidate.cpp
  src/engine/CsvEdit.cpp
  src/engine/CsvConvert.cpp
  src/engine/CsvGenerateCode.cpp
  src/engine/CsvSchemaIni.cpp
  src/engine/DateTimeMask.cpp)

add_library(CSVLint SHARED
  src/PluginEntry.cpp
  src/CsvLintPanel.cpp
  src/CsvLintDialogs.cpp
  src/CsvSettingsIO.cpp
  src/LinuxViewBridge.cpp
  ${CSV_ENGINE_SOURCES})
target_include_directories(CSVLint PRIVATE
    ${NPP_PLUGIN_INCLUDE} ${SCINTILLA_INCLUDE}
    "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(CSVLint PRIVATE PkgConfig::GTK ${CMAKE_DL_LIBS})
target_compile_options(CSVLint PRIVATE
    -Wall -Wno-unused-parameter -fvisibility=hidden -fPIC)
# scintilla_view_send_message resolves at dlopen from the host's libscintilla.so.
target_link_options(CSVLint PRIVATE -Wl,--allow-shlib-undefined)
set_target_properties(CSVLint PROPERTIES PREFIX "" OUTPUT_NAME "CSVLint")

# ---- tests ------------------------------------------------------------------
# engine_test: headless engine tests against the vendored upstream testdata
# fixtures (test/testdata). Engine-only — no GTK, no display needed.
add_executable(engine_test test/engine_test.cpp ${CSV_ENGINE_SOURCES})
target_include_directories(engine_test PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(engine_test PRIVATE
    CSVLINT_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/test/testdata")

# loader: dlopen smoke test (getName + menu + FuncItem ABI), no host needed.
add_executable(loader test/loader.cpp)
target_include_directories(loader PRIVATE
    "${CMAKE_SOURCE_DIR}/src" ${NPP_PLUGIN_INCLUDE} ${SCINTILLA_INCLUDE})
target_link_libraries(loader PRIVATE PkgConfig::GTK ${CMAKE_DL_LIBS})

enable_testing()
add_test(NAME csv_engine COMMAND engine_test)
add_test(NAME plugin_loader COMMAND loader $<TARGET_FILE:CSVLint>)

# ---- install ----------------------------------------------------------------
set(PLUGIN_INSTALL_DIR "$ENV{HOME}/.local/share/nextpad++/plugins/CSVLint"
    CACHE PATH "Where to install the plugin folder")
install(TARGETS CSVLint DESTINATION "${PLUGIN_INSTALL_DIR}")
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${PLUGIN_INSTALL_DIR}")

# ---- distributable zip ------------------------------------------------------
set(CSV_STAGE "${CMAKE_BINARY_DIR}/pkg/CSVLint")
file(MAKE_DIRECTORY "${CSV_STAGE}")
add_custom_target(package_zip
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:CSVLint> "${CSV_STAGE}/CSVLint.so"
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/LICENSE" "${CSV_STAGE}/"
  COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/pkg"
          ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/CSVLint.linux.zip"
          --format=zip CSVLint
  DEPENDS CSVLint
  COMMENT "Packaging CSVLint.linux.zip")
