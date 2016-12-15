add_library(object STATIC object.c path.c json_path.c ${RMUTIL_DIR}/vector.c rmalloc.c)

add_library(json_object STATIC json_object.c jsonsl.c ${RMUTIL_DIR}/sds.c rmalloc.c)
target_link_libraries(json_object object)

add_library(rejson SHARED module.c object_type.c ${RMUTIL_DIR}/util.c)
set_target_properties(rejson PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden LINK_FLAGS "-Bsymbolic")
target_compile_definitions(rejson PUBLIC IS_REDIS_MODULE)
target_link_libraries(rejson object json_object m)
