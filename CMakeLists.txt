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

# source: https://cmake.org/pipermail/cmake/2007-May/014345.html
# KDE's with -fPIC
IF(UNIX AND NOT WIN32)
  FIND_PROGRAM(CMAKE_UNAME uname /bin /usr/bin /usr/local/bin )
  IF(CMAKE_UNAME)
    EXEC_PROGRAM(uname ARGS -m OUTPUT_VARIABLE CMAKE_SYSTEM_PROCESSOR)
    SET(CMAKE_SYSTEM_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR} CACHE INTERNAL
"processor type (i386 and x86_64)")
    IF(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      ADD_DEFINITIONS(-fPIC)
    ENDIF(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  ENDIF(CMAKE_UNAME)
ENDIF(UNIX AND NOT WIN32)

include(CTest)
enable_testing()

add_library(object STATIC src/object.c src/path.c src/json_path.c deps/rmutil/vector.c src/rmalloc.c)
add_executable(test_object test/test_object.c)
target_link_libraries(test_object object)
add_test(test_object test_object)

add_library(json_object STATIC src/json_object.c src/jsonsl.c deps/rmutil/sds.c src/rmalloc.c)
target_link_libraries(json_object object)
add_executable(json_printer test/json_printer.c)
target_link_libraries(json_printer json_object m)
add_executable(test_json_object test/test_json_object.c)
target_link_libraries(test_json_object json_object m)
add_executable(json_validator test/json_validator.c)
target_link_libraries(json_validator json_object m)
add_test(test_json_object test_json_object)
add_test(NAME valid_json_files COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/json-validatortron.sh valid 0)
add_test(NAME invalid_json_files COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/json-validatortron.sh invalid 1)

add_library(rejson SHARED src/module.c deps/rmutil/sds.c)
target_compile_definitions(rejson PUBLIC IS_REDIS_MODULE)
target_link_libraries(rejson object json_object m)
set_target_properties(rejson PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden LINK_FLAGS "-Bsymbolic")

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
