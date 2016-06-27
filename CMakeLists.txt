cmake_minimum_required(VERSION 3.0.0)
project(rejson VERSION 0.0.0)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")

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

add_executable(test_object test/test_object.c src/object.c src/path.c src/json_path.c src/rmalloc.c)
add_test(test_object test_object)

add_executable(test_json_object test/test_json_object.c src/json_object.c src/jsonsl.c src/object.c src/rmalloc.c)
target_link_libraries(test_json_object m)
add_test(test_json_object test_json_object)

add_library(rejson SHARED src/module.c src/json_object.c src/jsonsl.c src/object.c src/path.c src/json_path.c src/rmalloc.c)
target_compile_definitions(rejson PUBLIC IS_REDIS_MODULE)
target_link_libraries(rejson m)
set_target_properties(rejson PROPERTIES PREFIX "")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
