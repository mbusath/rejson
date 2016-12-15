# object functionality tests
add_executable(test_object test_object.c)
target_link_libraries(test_object object)
add_test(test_object test_object)

# JSON object functionality tests
add_executable(json_printer json_printer.c)
target_link_libraries(json_printer json_object m)
add_executable(test_json_object test_json_object.c)
target_link_libraries(test_json_object json_object m)
add_executable(json_validator json_validator.c)
target_link_libraries(json_validator json_object m)
add_test(test_json_object test_json_object)
add_test(NAME valid_json_files COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/json-validatortron.sh valid 0)
add_test(NAME invalid_json_files COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/json-validatortron.sh invalid 1)

# Module tests
add_test(NAME redis_module COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_CURRENT_SOURCE_DIR}/pytest)
