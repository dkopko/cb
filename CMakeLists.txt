cmake_minimum_required(VERSION 2.8.5)
project(cb C CXX)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option(COVERAGE "Build with test coverage" OFF)

set(CB_SOURCES
  "${CMAKE_SOURCE_DIR}/src/cb.c"
  "${CMAKE_SOURCE_DIR}/src/cb_interpreter.c"
  "${CMAKE_SOURCE_DIR}/src/cb_lb_set.c"
  "${CMAKE_SOURCE_DIR}/src/cb_map.c"
  "${CMAKE_SOURCE_DIR}/src/cb_random.c"
  "${CMAKE_SOURCE_DIR}/src/cb_structmap.c"
)

include_directories(
  ${CMAKE_SOURCE_DIR}
  "${CMAKE_SOURCE_DIR}/src"
)

set(CMAKE_C_FLAGS_COVERAGE "${CMAKE_C_FLAGS_COVERAGE} -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_COVERAGE} -fprofile-arcs -ftest-coverage")
set(CMAKE_LDFLAGS_COVERAGE "${CMAKE_LDFLAGS_COVERAGE} -fprofile-arcs -ftest-coverage")

set(CB_CFLAGS "-std=c99 -Wall -Wno-unused-function -g -mtune=native -D_GNU_SOURCE -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes ${COVERAGE_CFLAGS}")


add_library(libcb_static STATIC ${CB_SOURCES})
set_target_properties(libcb_static PROPERTIES
    OUTPUT_NAME cb
    COMPILE_FLAGS ${CB_CFLAGS}
    LINKER_LANGUAGE C
)
install(TARGETS libcb_static DESTINATION lib)



add_executable(test_misc "${CMAKE_SOURCE_DIR}/test/test_misc.c")
set_target_properties(test_misc PROPERTIES
    LINKER_LANGUAGE C

)

add_executable(test_main "${CMAKE_SOURCE_DIR}/test/main.c")
target_link_libraries(test_main libcb_static)
set_target_properties(test_main PROPERTIES
    LINKER_LANGUAGE C)

add_executable(test_lb_set "${CMAKE_SOURCE_DIR}/test/test_lb_set.cpp")
target_link_libraries(test_lb_set libcb_static)
set_target_properties(test_lb_set PROPERTIES
    LINKER_LANGUAGE CXX)

add_executable(test_measure "${CMAKE_SOURCE_DIR}/test/test_measure.cpp")
target_link_libraries(test_measure libcb_static)
set_target_properties(test_measure PROPERTIES
    LINKER_LANGUAGE CXX)

add_executable(test_measure_ll
  "${CMAKE_SOURCE_DIR}/test/test_measure_ll.c"
  "${CMAKE_SOURCE_DIR}/test/test_measure_ll_f.c"
)
set_target_properties(test_measure_ll PROPERTIES
  LINKER_LANGUAGE C)

add_executable(test_interpreter "${CMAKE_SOURCE_DIR}/test/test_interpreter.c")
target_link_libraries(test_interpreter libcb_static)
set_target_properties(test_interpreter PROPERTIES
    LINKER_LANGUAGE C)

add_executable(test_structmap "${CMAKE_SOURCE_DIR}/test/test_structmap.c")
target_link_libraries(test_structmap libcb_static)
set_target_properties(test_structmap PROPERTIES
    LINKER_LANGUAGE C)
