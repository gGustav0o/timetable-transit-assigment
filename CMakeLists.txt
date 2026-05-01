cmake_minimum_required(VERSION 3.20)

project(timetable-transit-assigment
  VERSION 0.1.0
  LANGUAGES CXX
)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/ProjectOptions.cmake)

set(MATHFP_ENABLE_RANGES ON CACHE BOOL "Enable mathfp ranges module")
set(MATHFP_ENABLE_LINALG ON CACHE BOOL "Enable mathfp linalg module")
set(MATHFP_ENABLE_GRAPH ON CACHE BOOL "Enable mathfp graph module")
set(MATHFP_ENABLE_INTEROP ON CACHE BOOL "Enable mathfp interop module")

add_subdirectory(libs/mathfp)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake)

add_executable(timetable-transit-assigment
  src/main.cpp
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
  src/domain/assignment/execution_config.cpp
  src/domain/assignment/output.cpp
  src/domain/assignment/output_assembly.cpp
  src/domain/assignment/output_connections.cpp
  src/domain/assignment/output_loads.cpp
  src/domain/assignment/output_lookup.cpp
  src/domain/assignment/output_validation.cpp
  src/domain/assignment/pipeline.cpp
  src/domain/assignment/search_pruning.cpp
  src/domain/assignment/search_pruning_diagnostics.cpp
  src/domain/assignment/search_pruning_plan.cpp
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
  src/domain/assignment/search/branch_state.cpp
  src/domain/assignment/search/preprocessed_network.cpp
  src/domain/assignment/search/search.cpp
  src/domain/assignment/run.cpp
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

file(GLOB_RECURSE TIMETABLE_HEADERS
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/include/*.hpp
)

target_sources(timetable-transit-assigment PRIVATE
  ${TIMETABLE_HEADERS}
)

target_include_directories(timetable-transit-assigment
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_features(timetable-transit-assigment PRIVATE cxx_std_20)

target_link_libraries(timetable-transit-assigment
  PRIVATE
    mathfp::all
    Boost::boost
    fmt::fmt
    spdlog::spdlog
    unofficial::vincentlaucsb-csv-parser::csv
    ftxui::component
    ftxui::dom
    ftxui::screen
)
