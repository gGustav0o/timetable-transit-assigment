cmake_minimum_required(VERSION 3.20)

project(timetable-transit-assigment
  VERSION 0.1.0
  LANGUAGES CXX
)

option(TIMETABLE_ENABLE_RUNTIME_OVERRIDES
  "Enable compile-time runtime override policy"
  OFF
)
option(TIMETABLE_OVERRIDE_DISABLE_CAPACITY_AWARE_ASSIGNMENT
  "Force-disable behavioral capacity-aware assignment when runtime overrides are enabled"
  OFF
)
option(TIMETABLE_OVERRIDE_DISABLE_VEHICLE_JOURNEY_ITEM_OVERLOAD_ASSESSMENT
  "Force-disable vehicle journey item overload assessment when runtime overrides are enabled"
  OFF
)
option(TIMETABLE_BUILD_TESTS
  "Build timetable unit tests"
  ON
)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectOptions.cmake)

set(MATHFP_ENABLE_RANGES ON CACHE BOOL "Enable mathfp ranges module")
set(MATHFP_ENABLE_LINALG ON CACHE BOOL "Enable mathfp linalg module")
set(MATHFP_ENABLE_GRAPH ON CACHE BOOL "Enable mathfp graph module")
set(MATHFP_ENABLE_INTEROP ON CACHE BOOL "Enable mathfp interop module")

add_subdirectory(libs/mathfp)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake)

set(_TIMETABLE_XLSXIO_READ_TARGET_CANDIDATES
  xlsxio::xlsxio_read
  xlsxio::xlsxio_read_STATIC
  xlsxio::xlsxio_read_SHARED
  xlsxio::xlsxio_read_static
  xlsxio::xlsxio_read_shared
  unofficial::xlsxio::xlsxio_read
  unofficial::xlsxio::xlsxio_read_STATIC
  unofficial::xlsxio::xlsxio_read_SHARED
  unofficial::xlsxio::xlsxio_read_static
  unofficial::xlsxio::xlsxio_read_shared
  xlsxio_read
  xlsxio_read_STATIC
  xlsxio_read_SHARED
  xlsxio_read_static
  xlsxio_read_shared
)

foreach(_target IN LISTS _TIMETABLE_XLSXIO_READ_TARGET_CANDIDATES)
  if(TARGET ${_target})
    set(TIMETABLE_XLSXIO_READ_TARGET ${_target})
    break()
  endif()
endforeach()

if(NOT TIMETABLE_XLSXIO_READ_TARGET)
  find_path(TIMETABLE_XLSXIO_INCLUDE_DIR NAMES xlsxio_read.h)
  find_library(TIMETABLE_XLSXIO_READ_LIBRARY NAMES xlsxio_read libxlsxio_read)

  if(TIMETABLE_XLSXIO_INCLUDE_DIR AND TIMETABLE_XLSXIO_READ_LIBRARY)
    add_library(timetable_xlsxio_read UNKNOWN IMPORTED)
    set_target_properties(timetable_xlsxio_read PROPERTIES
      IMPORTED_LOCATION "${TIMETABLE_XLSXIO_READ_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${TIMETABLE_XLSXIO_INCLUDE_DIR}"
    )
    set(TIMETABLE_XLSXIO_READ_TARGET timetable_xlsxio_read)
  else()
    message(FATAL_ERROR
      "xlsxio read target/library was not exported by the xlsxio package"
    )
  endif()
endif()

set(TIMETABLE_CORE_SOURCES
  src/app/app.cpp
  src/app/cli.cpp
  src/domain/assignment/assignment_period.cpp
  src/domain/assignment/capacity.cpp
  src/domain/assignment/capacity_aware_assignment.cpp
  src/domain/assignment/choice/choice.cpp
  src/domain/assignment/complete_connection_diagnostics.cpp
  src/domain/assignment/complete_connection_retention.cpp
  src/domain/assignment/connection_admissibility.cpp
  src/domain/assignment/connection.cpp
  src/domain/assignment/day_path.cpp
  src/domain/assignment/execution_config.cpp
  src/domain/assignment/output.cpp
  src/domain/assignment/output_assembly.cpp
  src/domain/assignment/output_connections.cpp
  src/domain/assignment/output_loads.cpp
  src/domain/assignment/output_lookup.cpp
  src/domain/assignment/output_validation.cpp
  src/domain/assignment/pipeline.cpp
  src/domain/assignment/detail/pipeline_capacity.cpp
  src/domain/assignment/detail/pipeline_choice_split.cpp
  src/domain/assignment/detail/pipeline_common.cpp
  src/domain/assignment/detail/pipeline_internal.cpp
  src/domain/assignment/detail/pipeline_od_day.cpp
  src/domain/assignment/detail/pipeline_search.cpp
  src/domain/assignment/detail/pipeline_validation.cpp
  src/domain/assignment/search_pruning.cpp
  src/domain/assignment/search_pruning_diagnostics.cpp
  src/domain/assignment/search_pruning_plan.cpp
  src/domain/assignment/search_cost.cpp
  src/domain/assignment/search_time_domain.cpp
  src/domain/assignment/search_time_domain_diagnostics.cpp
  src/domain/assignment/search_time_domain_builder.cpp
  src/domain/assignment/search_time_domain_execution.cpp
  src/domain/assignment/search_time_domain_plan.cpp
  src/domain/assignment/skim.cpp
  src/domain/assignment/skim_config.cpp
  src/domain/assignment/projection/csv.cpp
  src/domain/assignment/projection/summary.cpp
  src/domain/assignment/projection/text_summary.cpp
  src/domain/assignment/search/frontier/paper_connection_retention.cpp
  src/domain/assignment/search/frontier/retention_operations.cpp
  src/domain/assignment/search/generation/successor.cpp
  src/domain/assignment/search/execution.cpp
  src/domain/assignment/search/kernel.cpp
  src/domain/assignment/search/policy.cpp
  src/domain/assignment/search/problem.cpp
  src/domain/assignment/search/projection.cpp
  src/domain/assignment/search/projection/contract.cpp
  src/domain/assignment/search/projection/complete_connection.cpp
  src/domain/assignment/search/projection/sink.cpp
  src/domain/assignment/search/branch_state.cpp
  src/domain/assignment/search/model/branch.cpp
  src/domain/assignment/search/model/retention.cpp
  src/domain/assignment/search/model/support.cpp
  src/domain/assignment/search/od_day/successor.cpp
  src/domain/assignment/search/od_day/supply_graph.cpp
  src/domain/assignment/search/preprocessed_network.cpp
  src/domain/assignment/search/pruning/suffix_lower_bound.cpp
  src/domain/assignment/search/residual_reachability.cpp
  src/domain/assignment/search/generation/branch_transition.cpp
  src/domain/assignment/search/relations/branch_metrics.cpp
  src/domain/assignment/search/relations/branch_state_projection.cpp
  src/domain/assignment/search/relations/paper_connection_relevance.cpp
  src/domain/assignment/search/tree/level_expansion.cpp
  src/domain/assignment/search/tree/paper_successor_step.cpp
  src/domain/assignment/search/runtime/accepted_successor_application.cpp
  src/domain/assignment/search/runtime/all_zone_runner.cpp
  src/domain/assignment/search/runtime/batch_diagnostics.cpp
  src/domain/assignment/search/runtime/batch_planning.cpp
  src/domain/assignment/search/runtime/batch_runner.cpp
  src/domain/assignment/search/runtime/batch_state.cpp
  src/domain/assignment/search/runtime/batch_tree_execution.cpp
  src/domain/assignment/search/runtime/branch_enqueue.cpp
  src/domain/assignment/search/runtime/cancellation.cpp
  src/domain/assignment/search/runtime/continuation_filter.cpp
  src/domain/assignment/search/runtime/diagnostics.cpp
  src/domain/assignment/search/runtime/demand_runner.cpp
  src/domain/assignment/search/runtime/od_day_frontier_synchronization.cpp
  src/domain/assignment/search/runtime/od_day_runner.cpp
  src/domain/assignment/search/runtime/parallel.cpp
  src/domain/assignment/search/runtime/projection_application.cpp
  src/domain/assignment/search/runtime/reachability_masks.cpp
  src/domain/assignment/search/runtime/result_finalization.cpp
  src/domain/assignment/search/runtime/result_materialization.cpp
  src/domain/assignment/search/runtime/root_initialization.cpp
  src/domain/assignment/search/search.cpp
  src/domain/assignment/run.cpp
  src/domain/assignment/runtime_overrides.cpp
  src/domain/assignment/split/split.cpp
  src/domain/assignment/validation/choice.cpp
  src/domain/assignment/validation/complete_connection_dominance.cpp
  src/domain/assignment/validation/preprocessing.cpp
  src/domain/assignment/validation/search_task_builder.cpp
  src/domain/assignment/validation/search_pruning.cpp
  src/domain/assignment/validation/search_time_domain.cpp
  src/domain/assignment/validation/search.cpp
  src/domain/assignment/validation/split.cpp
  src/domain/preprocessing/segments_index.cpp
  src/domain/preprocessing/connection_segments.cpp
  src/domain/preprocessing/route_segments.cpp
  src/infra/clipboard.cpp
  src/infra/demand_csv.cpp
  src/infra/assignment_output_csv.cpp
  src/infra/assignment_output_files.cpp
  src/infra/assignment_output_json.cpp
  src/infra/assignment_output_text.cpp
  src/infra/capacity_csv.cpp
  src/infra/data_sources.cpp
  src/infra/file_data_source.cpp
  src/infra/log_buffer.cpp
  src/infra/logging.cpp
  src/infra/pair_file_data_source.cpp
  src/infra/params_txt.cpp
  src/infra/params_txt_access.cpp
  src/infra/params_txt_mapping.cpp
  src/infra/params_txt_syntax.cpp
  src/infra/presegmented_input_columns.cpp
  src/infra/presegmented_input_finalize.cpp
  src/infra/presegmented_input_pipeline.cpp
  src/infra/presegmented_input_rows.cpp
  src/infra/presegmented_input_segments.cpp
  src/infra/progress_bus.cpp
  src/infra/segments_csv.cpp
  src/infra/single_file_data_source.cpp
  src/infra/txt_single_file_segments.cpp
  src/infra/ui_log_sink.cpp
  src/ui/model.cpp
  src/ui/result_bus.cpp
  src/ui/result_snapshot.cpp
  src/ui/ui_commands.cpp
  src/ui/ui_panels.cpp
  src/ui/ui_renderer.cpp
  src/ui/ui.cpp
  include/timetable/timetable.hpp
)

add_library(timetable_core STATIC
  ${TIMETABLE_CORE_SOURCES}
)

add_executable(timetable-transit-assigment
  src/main.cpp
)

file(GLOB_RECURSE TIMETABLE_HEADERS
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/include/*.hpp
)

target_sources(timetable_core PRIVATE
  ${TIMETABLE_HEADERS}
)

target_include_directories(timetable_core
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(timetable_core PUBLIC cxx_std_20)

if(TIMETABLE_ENABLE_RUNTIME_OVERRIDES)
  target_compile_definitions(timetable_core
    PRIVATE
      TIMETABLE_ENABLE_RUNTIME_OVERRIDES
  )

  if(TIMETABLE_OVERRIDE_DISABLE_CAPACITY_AWARE_ASSIGNMENT)
    target_compile_definitions(timetable_core
      PRIVATE
        TIMETABLE_OVERRIDE_DISABLE_CAPACITY_AWARE_ASSIGNMENT
    )
  endif()

  if(TIMETABLE_OVERRIDE_DISABLE_VEHICLE_JOURNEY_ITEM_OVERLOAD_ASSESSMENT)
    target_compile_definitions(timetable_core
      PRIVATE
        TIMETABLE_OVERRIDE_DISABLE_VEHICLE_JOURNEY_ITEM_OVERLOAD_ASSESSMENT
    )
  endif()
endif()

target_link_libraries(timetable_core
  PUBLIC
    mathfp::all
    Boost::boost
    Boost::container
    Boost::unordered
    fmt::fmt
    spdlog::spdlog
    unofficial::vincentlaucsb-csv-parser::csv
    ${TIMETABLE_XLSXIO_READ_TARGET}
    ftxui::component
    ftxui::dom
    ftxui::screen
)

target_link_libraries(timetable-transit-assigment
  PRIVATE
    timetable_core
)

if(TIMETABLE_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
