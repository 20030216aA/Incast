
#incast

cmake_minimum_required(VERSION 3.5)
project(tna_exact_match)


set(TESTDATADIR "")
# install shared library
set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
# set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

set(GENERIC_FLAGS   "-DBFRT_GENERIC_FLAGS")
# set(PER_PIPE_TBL_LOCK   "-DPIPE_MGR_PER_PIPE_TABLE_LOCK_ENABLE")
# set(DRV_UCLI_CFLAGS "-DUCLI_CONFIG_INCLUDE_ELS_LOOP=1 -DLLDLIB_CONFIG_INCLUDE_UCLI=1 -DPORT_MGR_CONFIG_INCLUDE_UCLI=1 -DDVM_CONFIG_INCLUDE_UCLI=1 -DPIPE_MGR_CONFIG_INCLUDE_UCLI=1 -DMC_MGR_CONFIG_INCLUDE_UCLI=1 -DTRAFFIC_MGR_CONFIG_INCLUDE_UCLI=1 -DDEVDIAG_CONFIG_INCLUDE_UCLI=1")
# set(DRV_THRD_CFLAGS "-DPIPE_MGR_THREADS -DPIPE_MGR_INLINE_DR_SERVICE -DMC_MGR_THREADS -DMC_MGR_INLINE_DR_SERVICE -DTM_MT_SAFE")
# set(DRV_MISC_CFLAGS "-DTARGET_IS_LLD -DINCLUDE_SERDES_PKG -DDEBUG_MODE=0 -DHARLYN_DEBUG_MODE=0 ${GENERIC_FLAGS}")
# set(DRV_MISC_CFLAGS "${DRV_MISC_CFLAGS} ${PER_PIPE_TBL_LOCK}")
# set(DRV_CFLAGS "${DRV_UCLI_CFLAGS} ${DRV_THRD_CFLAGS} ${DRV_MISC_CFLAGS}")
set(DRV_CFLAGS ${GENERIC_FLAGS})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${DRV_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DRV_CFLAGS}")

include_directories(${CMAKE_INSTALL_PREFIX}/include)

link_directories(AFTER ${CMAKE_INSTALL_PREFIX}/lib) # same as LD_LIBRARY_PATH

find_package(Threads REQUIRED)

message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

file(GLOB APP_SOURCES *.cpp *.c)
foreach( testsourcefile ${APP_SOURCES})
    get_filename_component(testname ${testsourcefile} NAME_WE)
    add_executable(${testname} ${testsourcefile})
  # target_link_libraries(${testname}_example PUBLIC bfutils target_sys driver)
    target_link_libraries(${testname} PUBLIC bfutils bfsys driver clish Threads::Threads m)
    install(TARGETS ${testname} DESTINATION bin)
endforeach( testsourcefile ${APP_SOURCES})

