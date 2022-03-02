cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)
project(nvalloc)


SET(CMAKE_C_FLAGS_DEBUG "-DNVALLOC_DEBUG -O0 -g3 -Wall -mclflushopt -mclwb ")
SET(CMAKE_C_FLAGS_RELEASE " -O3 -g3 -fno-omit-frame-pointer -fPIC -mclflushopt -mclwb ")


set(CMAKE_VERBOSE_MAKEFILE ON)


set (LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_LIST_DIR}/lib)

set (EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_LIST_DIR}/bin)


include_directories(${CMAKE_CURRENT_LIST_DIR}/include)


aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_PROJECT_NAME}_src)


add_library(${CMAKE_PROJECT_NAME}_static STATIC ${${CMAKE_PROJECT_NAME}_src})


target_compile_options(${CMAKE_PROJECT_NAME}_static PRIVATE -Wall -pipe -funroll-loops -D_GNU_SOURCE -D_REENTRANT )

target_link_libraries(${CMAKE_PROJECT_NAME}_static pmem)
target_link_libraries(${CMAKE_PROJECT_NAME}_static pthread)
target_link_libraries(${CMAKE_PROJECT_NAME}_static m)
target_link_libraries(${CMAKE_PROJECT_NAME}_static jemalloc "-L/usr/local/lib/ -Wl,-rpath,/usr/local/lib -lm -lstdc++ -pthread -ldl")



set_target_properties(${CMAKE_PROJECT_NAME}_static PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}")


enable_testing()


set(testsource )
string(APPEND testsource $ENV{testsource})

list(LENGTH testsource testsourcelength)
math(EXPR testsourcelength "${testsourcelength}-1")

if(NOT ${testsource} MATCHES "NOTFOUND")
list(LENGTH testsource testsourcelength)
math(EXPR testsourcelength "${testsourcelength}-1")


foreach(i  RANGE ${testsourcelength})
list(GET testsource ${i} testsourcename)
string(REGEX REPLACE "\.cpp|\.c" "" testname ${testsourcename})
message("-- add test ${testname}")
add_executable(${testname} ${CMAKE_CURRENT_LIST_DIR}/test/src/${testsourcename})
target_compile_options(${testname} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++11 -Iext/ralloc/test/benchmark -Iext/ralloc/src -faligned-new> -Wall -D_GNU_SOURCE -D_REENTRANT -DNVALLOC -I ../include/nvalloc/ )
target_link_libraries(${testname} ${CMAKE_PROJECT_NAME}_static)
add_test(${testname} ${EXECUTABLE_OUTPUT_PATH}/${testname})
endforeach(i)
endif() 