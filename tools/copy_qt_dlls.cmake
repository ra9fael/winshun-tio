# copy_qt_dlls.cmake - put the Qt DLLs a test binary imports beside it.
#
# The plugin loads Qt from WinShun's folder (the shared-Qt contract), but a
# plain test executable imports Qt directly and the loader looks beside the exe.
# So the DLLs it actually imports are copied next to it.
#
# Called as: cmake -DTEST_EXE=<path> -DQT_BIN_DIR=<qt>/bin -P this_file
if(NOT TEST_EXE OR NOT QT_BIN_DIR)
  message(FATAL_ERROR "TEST_EXE and QT_BIN_DIR are required")
endif()

get_filename_component(test_dir "${TEST_EXE}" DIRECTORY)
get_filename_component(test_name "${TEST_EXE}" NAME)

# The DLLs the test needs: QtCore for the object system, QtGui for the types
# QVariant carries here. Nothing else is linked.
set(needed Qt6Core.dll Qt6Gui.dll)

foreach(dll IN LISTS needed)
  if(EXISTS "${QT_BIN_DIR}/${dll}")
    configure_file("${QT_BIN_DIR}/${dll}" "${test_dir}/${dll}" COPYONLY)
  endif()
endforeach()

# The platforms plugin is not needed (no window is created), but the Qt
# directory layout is: without it Qt complains about a missing plugin path.
message(STATUS "copied Qt DLLs for ${test_name} into ${test_dir}")
