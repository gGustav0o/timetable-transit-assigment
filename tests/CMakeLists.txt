include(GoogleTest)

add_executable(timetable_tests
  domain/branch_arena_tests.cpp
  domain/friedrich_hofsaess_wekeck_regression_tests.cpp
  domain/od_day_successor_tests.cpp
  domain/od_day_supply_graph_tests.cpp
  domain/od_day_path_contract_tests.cpp
  domain/residual_reachability_tests.cpp
  domain/search_execution_tests.cpp
  domain/search_generation_spec_tests.cpp
  domain/search_kernel_contract_tests.cpp
  domain/search_problem_tests.cpp
  domain/search_projection_tests.cpp
  domain/search_pruning_tests.cpp
  domain/search_runtime_diagnostics_tests.cpp
  domain/suffix_lower_bound_tests.cpp
  domain/support_prefix_tests.cpp
  mathfp/ranges_zip_tests.cpp
)

target_link_libraries(timetable_tests
  PRIVATE
    timetable_core
    GTest::gtest_main
)

gtest_discover_tests(timetable_tests)
