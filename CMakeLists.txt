cmake_minimum_required(VERSION 2.8.5)
project(cb C CXX)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option(COVERAGE "Build with test coverage" OFF)

set(CB_SOURCES
  "${CMAKE_SOURCE_DIR}/src/cb.c"
  "${CMAKE_SOURCE_DIR}/src/cb_bst.c"
  "${CMAKE_SOURCE_DIR}/src/cb_interpreter.c"
  "${CMAKE_SOURCE_DIR}/src/cb_lb_set.c"
  "${CMAKE_SOURCE_DIR}/src/cb_log.c"
  "${CMAKE_SOURCE_DIR}/src/cb_map.c"
  "${CMAKE_SOURCE_DIR}/src/cb_print.c"
  "${CMAKE_SOURCE_DIR}/src/cb_random.c"
  "${CMAKE_SOURCE_DIR}/src/cb_region.c"
  "${CMAKE_SOURCE_DIR}/src/cb_structmap.c"
  "${CMAKE_SOURCE_DIR}/src/cb_term.c"
)

include_directories(
  ${CMAKE_SOURCE_DIR}
  "${CMAKE_SOURCE_DIR}/src"
)

# Remove -Wno-array-bounds when xxhash stops breaking in release build with it.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -D_GNU_SOURCE -Wall -Wextra -Werror -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-function -Wno-array-bounds")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wmissing-declarations -Wno-unused-function -Wno-array-bounds")

# Used for flamegraphs.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -DCB_ASSERT_ON -DCB_HEAVY_ASSERT_ON")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -DCB_ASSERT_ON -DCB_HEAVY_ASSERT_ON")

# Used for latency graphs.
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mtune=native")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=native")

set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -mtune=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -mtune=native")

set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -mtune=native")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -mtune=native")

# Used for coverage report.
set(CMAKE_C_FLAGS_COVERAGE "${CMAKE_C_FLAGS_COVERAGE} -O0 -g -DCB_ASSERT_ON -DCB_HEAVY_ASSERT_ON -DCB_VERBOSE -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_COVERAGE "${CMAKE_CXX_FLAGS_COVERAGE} -O0 -g -DCB_ASSERT_ON -DCB_HEAVY_ASSERT_ON -DCB_VERBOSE -fprofile-arcs -ftest-coverage")
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE "${CMAKE_EXE_LINKER_FLAGS_COVERAGE} -fprofile-arcs -ftest-coverage")


add_library(libcb_static STATIC ${CB_SOURCES})
set_target_properties(libcb_static PROPERTIES
    OUTPUT_NAME cb
    LINKER_LANGUAGE C
)
install(TARGETS libcb_static DESTINATION lib)


add_executable(test_unit_bst "${CMAKE_SOURCE_DIR}/test/test_unit_bst.c")
target_link_libraries(test_unit_bst libcb_static)
set_target_properties(test_unit_bst PROPERTIES
    LINKER_LANGUAGE C)


add_executable(test_unit_hash "${CMAKE_SOURCE_DIR}/test/test_unit_hash.c")
target_link_libraries(test_unit_hash libcb_static)
set_target_properties(test_unit_hash PROPERTIES
    LINKER_LANGUAGE C)


add_executable(test_misc "${CMAKE_SOURCE_DIR}/test/test_misc.c")
set_target_properties(test_misc PROPERTIES
    LINKER_LANGUAGE C

)

add_executable(test_main "${CMAKE_SOURCE_DIR}/test/main.c")
target_link_libraries(test_main libcb_static)
set_target_properties(test_main PROPERTIES
    LINKER_LANGUAGE C)

add_executable(test_assert "${CMAKE_SOURCE_DIR}/test/test_assert.c")
target_link_libraries(test_assert libcb_static)
set_target_properties(test_assert PROPERTIES
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


enable_testing()
add_test(test_main test_main)
add_test(test_misc test_misc)
add_test(test_lb_set test_lb_set)
add_test(test_measure test_measure)
add_test(test_measure_ll test_measure_ll)
add_test(test_interpreter test_interpreter)
add_test(test_structmap test_structmap)
