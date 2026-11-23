find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_qheif tst_qheif.cpp)
target_link_libraries(tst_qheif PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_compile_definitions(tst_qheif PRIVATE
    QHEIF_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins"
    QHEIF_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
add_dependencies(tst_qheif qheif)

add_test(NAME tst_qheif COMMAND tst_qheif)
set_tests_properties(tst_qheif PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(WIN32 AND QHEIF_DEPS_PREFIX)
    # The plugin finds heif.dll, libde265.dll and dav1d.dll on the PATH.
    set_property(TEST tst_qheif PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QHEIF_DEPS_PREFIX}/bin")
endif()
