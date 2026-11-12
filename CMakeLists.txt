cmake_minimum_required(VERSION 3.21)
project(tio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON) # TioWindow is a Q_OBJECT type

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The tio launcher is a Windows x64 plugin")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# The same split as ra9fael.qr and WinShun's own winshun.capture: tio.exe is a
# starter without Qt (qmlapp/stub/main.cpp); it loads TioApp.dll, built on
# WinShun's Qt, which runs the plugin protocol on a worker thread and the
# standalone window on the GUI thread. plugin.json names tio.exe.
find_package(Qt6 6.12.0 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

# The QML module's resources under :/qt/qml/<URI>, as WinShun's own app does;
# without it they land at :/<URI>, where the engine never looks.
qt_policy(SET QTP0001 NEW)

add_executable(tio qmlapp/stub/main.cpp tio.rc)
target_include_directories(tio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# setupapi/bcrypt/advapi32 are what --probe and --probe verify use; the stub
# itself only needs kernel32.
target_link_libraries(tio PRIVATE setupapi bcrypt advapi32 user32)
# Beside TioApp.dll, where plugin.json's runtime.program points.
set_target_properties(tio PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(tio PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

# The plugin itself. controller.h / plugin.h / the rows are the same sources the
# stub's --serve and --probe paths use; the window is qmlapp/.
qt_add_library(TioApp SHARED
  plugin.h
  controller.h
  core.h
  logic.h
  native.h
  rows.h
  rows_build.h
  window_hook.h
  qmlapp/qmlmain.cpp
  qmlapp/ThemeBridge.cpp
  qmlapp/ThemeBridge.h
  qt_window_icon.h
  qmlapp/TioWindow.cpp
  qmlapp/TioWindow.h
)
target_include_directories(TioApp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TioApp PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
  setupapi bcrypt advapi32 user32)
set_target_properties(TioApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(TioApp PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()

# Only the modules the shared-Qt contract guarantees: QtQuick, QtQuick.Controls
# .Basic and QtQuick.Layouts. No Mica, no WinShun QML controls: a plugin must not
# depend on another plugin's or the host's private module.
set(tio_qml
  qmlapp/qml/TioWindow.qml
  qmlapp/qml/DesignTokens.qml
  qmlapp/qml/GroupCard.qml
  qmlapp/qml/FieldRow.qml
  qmlapp/qml/DetailLine.qml
  qmlapp/qml/LinkLabel.qml
  qmlapp/qml/TokenComboBox.qml
  qmlapp/qml/TokenSegmented.qml
  qmlapp/qml/TokenButton.qml)
set_source_files_properties("qmlapp/qml/DesignTokens.qml" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Flattened into the module root (qrc:/qt/qml/Tio/<Name>.qml), as ra9fael.qr and
# binarylab do: the components import each other and the DesignTokens singleton by
# name, which resolves beside them the way a qmldir type does.
foreach(file IN LISTS tio_qml)
  get_filename_component(name "${file}" NAME)
  set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
# The window island is one module; the files keep their names.
set(qml_import_scan "")
if(QT6_IS_SHARED_LIBS_BUILD)
  set(qml_import_scan NO_IMPORT_SCAN) # as WinShun's (src/app/CMakeLists.txt)
endif()
qt_add_qml_module(TioApp
  URI Tio
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Tio"
  VERSION 1.0
  NO_PLUGIN
  ${qml_import_scan}
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${tio_qml}
)
# Bytecode only, no QML compiled to C++: the plugin runs on whichever Qt 6 its
# WinShun comes with (see capture's CMakeLists).
set_target_properties(TioApp PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")

# The window icon's frames, embedded beside the QML that shows them: the same
# pixels winshun-plugin-icons rasterised into app.ico, so the title bar, the
# taskbar and Explorer's views agree. qt_window_icon.h sets them.
qt_add_resources(TioApp "plugin_window_icon"
  PREFIX "/plugin-icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/icons"
  FILES
    icons/app-16.png
    icons/app-20.png
    icons/app-24.png
    icons/app-32.png
    icons/app-40.png
    icons/app-48.png
    icons/app-64.png
    icons/app-256.png
)

# Pure-logic and row tests: no Qt, no Windows, so they run anywhere.
add_executable(tio_tests tests/test_logic.cpp)
target_include_directories(tio_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(tio_tests PRIVATE /utf-8 /W4 /permissive-)
else()
  target_compile_options(tio_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -O2)
endif()
set_target_properties(tio_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(tio_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()

enable_testing()
add_test(NAME tio_logic COMMAND tio_tests)
add_test(NAME tio_window_model COMMAND tio_window_tests)

# The window's model (path B): the rules the search box does not have — a form
# edited in a copy, saved explicitly, and an outside change that must not
# overwrite what the user is typing. It needs Qt (QObject, QVariant) but no
# window server: no widget is created.
add_executable(tio_window_tests tests/test_window.cpp qmlapp/TioWindow.cpp qmlapp/TioWindow.h)
target_include_directories(tio_window_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tio_window_tests PRIVATE Qt6::Core Qt6::Gui setupapi bcrypt advapi32)
if(MSVC)
  target_compile_options(tio_window_tests PRIVATE /utf-8 /W4 /permissive-)
else()
  target_compile_options(tio_window_tests PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra)
endif()
set_target_properties(tio_window_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(tio_window_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
endforeach()
# The test binary imports Qt directly (the plugin loads it from WinShun's folder,
# but a plain test exe needs the DLLs beside it). Only what it imports is
# copied: the test does not need Quick, only Core and Gui.
if(WIN32)
  add_custom_command(TARGET tio_window_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      -DTEST_EXE=$<TARGET_FILE:tio_window_tests>
      -DQT_BIN_DIR=${QT6_INSTALL_PREFIX}/bin
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/copy_qt_dlls.cmake"
    VERBATIM)
endif()

# The native checks need the packaged runtime, so they run from the plugin folder
# and are not part of the default suite: they start real processes.
add_executable(tio_native_probe tests/native_probe.cpp)
target_include_directories(tio_native_probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tio_native_probe PRIVATE setupapi bcrypt advapi32)
if(MSVC)
  target_compile_options(tio_native_probe PRIVATE /utf-8 /W4 /permissive-)
endif()
set_target_properties(tio_native_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
