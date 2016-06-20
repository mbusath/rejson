cmake_minimum_required(VERSION 3.0.0)
project(rejson VERSION 0.0.0)

# Use gnu99, source: http://stackoverflow.com/a/30564223/3160475 
if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_C_FLAGS "--std=gnu99 ${CMAKE_C_FLAGS}")
  endif ()
else ()
  set (CMAKE_C_STANDARD 99)
endif ()

include(CTest)

enable_testing()
add_executable(test_object test/test_object.c src/object.c src/path.c src/json_path.c)

add_test(test_object test_object)

add_library(rejson src/object.c)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
