#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace timetable::domain::assignment {
namespace {

    struct MathematicalModule final {
        std::string_view path;
    };

    struct ForbiddenInclude final {
        std::string_view token;
        std::string_view reason;
    };

    const std::vector<MathematicalModule>& mathematical_modules() {
        static const auto modules = std::vector<MathematicalModule>{
              { "include/timetable/domain/assignment/capacity/load_projection.hpp" }
            , { "include/timetable/domain/assignment/capacity/overload.hpp" }
            , { "include/timetable/domain/assignment/capacity/overload_assessment.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/config.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/diagnostics.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/exposure.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/load_state.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/load_state_iteration.hpp" }
            , { "include/timetable/domain/assignment/capacity_aware/penalty.hpp" }
            , { "src/domain/assignment/capacity/load_projection.cpp" }
            , { "src/domain/assignment/capacity/overload.cpp" }
            , { "src/domain/assignment/capacity/overload_assessment.cpp" }
            , { "src/domain/assignment/capacity_aware/config.cpp" }
            , { "src/domain/assignment/capacity_aware/diagnostics.cpp" }
            , { "src/domain/assignment/capacity_aware/exposure.cpp" }
            , { "src/domain/assignment/capacity_aware/load_state.cpp" }
            , { "src/domain/assignment/capacity_aware/load_state_iteration.cpp" }
            , { "src/domain/assignment/capacity_aware/penalty.cpp" }
            , { "include/timetable/domain/assignment/day_path/alternative.hpp" }
            , { "include/timetable/domain/assignment/day_path/signature.hpp" }
            , { "include/timetable/domain/assignment/day_path/support.hpp" }
            , { "include/timetable/domain/assignment/day_path/retention.hpp" }
            , { "include/timetable/domain/assignment/day_path/finalization.hpp" }
            , { "include/timetable/domain/assignment/day_path/types.hpp" }
            , { "src/domain/assignment/day_path/alternative.cpp" }
            , { "src/domain/assignment/day_path/signature.cpp" }
            , { "src/domain/assignment/day_path/support.cpp" }
            , { "src/domain/assignment/day_path/retention.cpp" }
            , { "src/domain/assignment/day_path/finalization.cpp" }
            , { "include/timetable/domain/assignment/search/cost/capacity_index.hpp" }
            , { "include/timetable/domain/assignment/search/cost/exposure.hpp" }
            , { "include/timetable/domain/assignment/search/cost/impedance.hpp" }
            , { "include/timetable/domain/assignment/search/cost/types.hpp" }
            , { "include/timetable/domain/assignment/search/cost/validation.hpp" }
            , { "src/domain/assignment/search/cost/capacity_index.cpp" }
            , { "src/domain/assignment/search/cost/exposure.cpp" }
            , { "src/domain/assignment/search/cost/impedance.cpp" }
            , { "src/domain/assignment/search/cost/validation.cpp" }
            , { "include/timetable/domain/assignment/search/residual/closure.hpp" }
            , { "include/timetable/domain/assignment/search/residual/graph.hpp" }
            , { "include/timetable/domain/assignment/search/residual/lower_bound.hpp" }
            , { "include/timetable/domain/assignment/search/residual/query.hpp" }
            , { "include/timetable/domain/assignment/search/residual/transition.hpp" }
            , { "include/timetable/domain/assignment/search/residual/types.hpp" }
            , { "include/timetable/domain/assignment/search/residual/validation.hpp" }
            , { "src/domain/assignment/search/residual/closure.cpp" }
            , { "src/domain/assignment/search/residual/graph.cpp" }
            , { "src/domain/assignment/search/residual/lower_bound.cpp" }
            , { "src/domain/assignment/search/residual/query.cpp" }
            , { "src/domain/assignment/search/residual/transition.cpp" }
            , { "src/domain/assignment/search/residual/types.cpp" }
            , { "src/domain/assignment/search/residual/validation.cpp" }
            , { "include/timetable/domain/assignment/split/types.hpp" }
            , { "include/timetable/domain/assignment/split/choice_weight.hpp" }
            , { "include/timetable/domain/assignment/split/demand_projection.hpp" }
            , { "include/timetable/domain/assignment/split/impedance_transform.hpp" }
            , { "include/timetable/domain/assignment/split/independence.hpp" }
            , { "include/timetable/domain/assignment/split/load_accumulation.hpp" }
            , { "include/timetable/domain/assignment/split/probability.hpp" }
            , { "include/timetable/domain/assignment/split/split_allocation.hpp" }
            , { "include/timetable/domain/assignment/split/split_kernel.hpp" }
            , { "src/domain/assignment/split/choice_weight.cpp" }
            , { "src/domain/assignment/split/demand_projection.cpp" }
            , { "src/domain/assignment/split/impedance_transform.cpp" }
            , { "src/domain/assignment/split/independence.cpp" }
            , { "src/domain/assignment/split/load_accumulation.cpp" }
            , { "src/domain/assignment/split/probability.cpp" }
            , { "src/domain/assignment/split/split_allocation.cpp" }
            , { "src/domain/assignment/split/split_kernel.cpp" }
        };
        return modules;
    }

    const std::vector<ForbiddenInclude>& forbidden_includes() {
        static const auto includes = std::vector<ForbiddenInclude>{
              { "timetable/infra/", "infra adapter dependency" }
            , { "progress_bus", "progress reporting dependency" }
            , { "spdlog", "logging dependency" }
            , { "<filesystem>", "filesystem dependency" }
            , { "timetable/ui/", "UI dependency" }
            , { "ftxui", "UI dependency" }
            , { "<thread>", "threading dependency" }
            , { "<future>", "threading dependency" }
            , { "<mutex>", "threading dependency" }
            , { "<shared_mutex>", "threading dependency" }
            , { "<condition_variable>", "threading dependency" }
            , { "<atomic>", "threading dependency" }
        };
        return includes;
    }

    [[nodiscard]] std::filesystem::path source_path(
        std::string_view relative_path
    ) {
        return std::filesystem::path{ TIMETABLE_SOURCE_DIR }
            / std::filesystem::path{ relative_path };
    }

    [[nodiscard]] bool is_include_line(
        std::string_view line
    ) {
        const auto first = line.find_first_not_of(" \t");
        return first != std::string_view::npos
            && line.substr(first).starts_with("#include");
    }

}  // namespace

TEST(ArchitectureRules, MathematicalModulesDoNotIncludeEffectfulDependencies) {
    for (const auto& module : mathematical_modules()) {
        const auto path = source_path(module.path);
        ASSERT_TRUE(std::filesystem::exists(path)) << path.string();

        std::ifstream input{ path };
        ASSERT_TRUE(input.is_open()) << path.string();

        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            if (!is_include_line(line)) {
                continue;
            }
            for (const auto& forbidden : forbidden_includes()) {
                EXPECT_EQ(line.find(forbidden.token), std::string::npos)
                    << module.path << ":" << line_number
                    << " includes forbidden " << forbidden.reason
                    << " via " << line;
            }
        }
    }
}

}  // namespace timetable::domain::assignment
