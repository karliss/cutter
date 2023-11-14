
enable_testing(true)

function(cutter_add_test source)
    get_filename_component(_name ${source} NAME_WE)
    add_executable(${_name} ${source})
    add_test(NAME ${_name} COMMAND ${_name})
    target_link_libraries(terminal_test PRIVATE Cutter ${QT_PREFIX}::Test)
endfunction()

cutter_add_test(terminal_test.cpp)
