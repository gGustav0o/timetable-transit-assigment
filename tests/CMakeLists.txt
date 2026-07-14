include(GoogleTest)

add_executable(timetable_tests
  domain/branch_arena_tests.cpp
  domain/search_pruning_tests.cpp
  domain/support_prefix_tests.cpp
)

target_link_libraries(timetable_tests
  PRIVATE
    timetable_core
    GTest::gtest_main
)

gtest_discover_tests(timetable_tests)
