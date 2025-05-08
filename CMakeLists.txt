cmake_minimum_required(VERSION 3.10)
project(cb C CXX)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

option(COVERAGE "Build with test coverage" OFF)

set(CB_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_bst.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_log.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_objtable.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_print.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_ref.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_region.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_structmap.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_term.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/cb_thread.cpp"
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
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


add_executable(test_bst "${CMAKE_CURRENT_SOURCE_DIR}/test/test_bst.c")
target_link_libraries(test_bst libcb_static)
set_target_properties(test_bst PROPERTIES
    LINKER_LANGUAGE C)


add_executable(test_hash "${CMAKE_CURRENT_SOURCE_DIR}/test/test_hash.c")
target_link_libraries(test_hash libcb_static)
set_target_properties(test_hash PROPERTIES
    LINKER_LANGUAGE C)


add_executable(test_misc "${CMAKE_CURRENT_SOURCE_DIR}/test/test_misc.c")
set_target_properties(test_misc PROPERTIES
    LINKER_LANGUAGE C

)

add_executable(test_test_assert "${CMAKE_CURRENT_SOURCE_DIR}/test/test_test_assert.c")
target_link_libraries(test_test_assert libcb_static)
set_target_properties(test_test_assert PROPERTIES
    LINKER_LANGUAGE C)

add_executable(test_measure
    "${CMAKE_CURRENT_SOURCE_DIR}/test/test_measure.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/cb_random.c"
)
target_link_libraries(test_measure libcb_static)
set_target_properties(test_measure PROPERTIES
    LINKER_LANGUAGE CXX)

add_executable(test_structmap "${CMAKE_CURRENT_SOURCE_DIR}/test/test_structmap.c")
target_link_libraries(test_structmap libcb_static)
set_target_properties(test_structmap PROPERTIES
    LINKER_LANGUAGE C)


enable_testing()
set(CTEST_PROJECT_NAME "cb")
set(CTEST_NIGHTLY_START_TIME "00:00:00 EST")
include(CTest)

# NOTE: This test cannot be automated because CMake can't handle subprocess death due to abort() usage.
#add_test(NAME test_test_assert
#         COMMAND test_test_assert)
#set_property(TEST test_test_assert
#             PROPERTY WILL_FAIL TRUE)

add_test(NAME test_measure
         COMMAND test_measure)

add_test(NAME test_misc
         COMMAND test_misc)

add_test(NAME test_structmap
         COMMAND test_structmap)

add_test(NAME test_bst
         COMMAND test_bst)

add_test(NAME test_hash
         COMMAND test_hash)
