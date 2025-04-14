cmake_minimum_required(VERSION 3.5)
project(bfrt_examples)


set(TESTDATADIR "")

set(CMAKE_INSTALL_RPATH $ORIGIN/../lib)
file( GLOB APP_SOURCES *.cpp *.c)
foreach( testsourcefile ${APP_SOURCES} )
    get_filename_component(testname ${testsourcefile} NAME_WE)
    add_executable( ${testname}_example ${testsourcefile} )
    target_link_libraries( ${testname}_example PUBLIC bfutils bfsys driver)
    install(TARGETS ${testname}_example DESTINATION bin)
endforeach( testsourcefile ${APP_SOURCES} )
