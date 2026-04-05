#include "timetable/domain/assignment/search_time_domain_plan.hpp"

#include <string>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<SearchTimeDomainExecutionPlan> plan_search_time_domain_execution(
          SearchArchitecture           architecture
        , SearchTimeDomainRolloutStage rollout_stage
        , SearchWindowMode             requested_mode
    ) {
        if (architecture != SearchArchitecture::OriginWideBranchAndBound) {
            return mathfp::unexpected(
                mathfp::invalid_arg("unsupported search architecture for time-domain planning")
                    .ctx("architecture", static_cast<std::int64_t>(architecture))
            );
        }

        if (rollout_stage == SearchTimeDomainRolloutStage::Disabled) {
            return SearchTimeDomainExecutionPlan{
                  .architecture   = architecture
                , .rollout_stage  = rollout_stage
                , .requested_mode = requested_mode
                , .enabled        = false
            };
        }

        if (rollout_stage == SearchTimeDomainRolloutStage::GlobalStrict) {
            if (requested_mode != SearchWindowMode::Global) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("current rollout stage only enables global strict time-domain search")
                        .ctx("rollout_stage" , static_cast<std::int64_t>(rollout_stage))
                        .ctx("requested_mode", static_cast<std::int64_t>(requested_mode))
                );
            }
            return SearchTimeDomainExecutionPlan{
                  .architecture         = architecture
                , .rollout_stage        = rollout_stage
                , .requested_mode       = requested_mode
                , .enabled              = true
                , .execution_adaptation = SearchTimeDomainAdaptation::Strict
            };
        }

        if (rollout_stage == SearchTimeDomainRolloutStage::PerOriginStrict) {
            if (requested_mode == SearchWindowMode::PerOd) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("current rollout stage does not yet enable exact or adapted per-OD time-domain search")
                        .ctx("rollout_stage" , static_cast<std::int64_t>(rollout_stage))
                        .ctx("requested_mode", static_cast<std::int64_t>(requested_mode))
                );
            }
            return SearchTimeDomainExecutionPlan{
                  .architecture         = architecture
                , .rollout_stage        = rollout_stage
                , .requested_mode       = requested_mode
                , .enabled              = true
                , .execution_adaptation = SearchTimeDomainAdaptation::Strict
            };
        }

        return SearchTimeDomainExecutionPlan{
              .architecture         = architecture
            , .rollout_stage        = rollout_stage
            , .requested_mode       = requested_mode
            , .enabled              = true
            , .execution_adaptation = requested_mode == SearchWindowMode::PerOd
                ? std::optional<SearchTimeDomainAdaptation>{
                    SearchTimeDomainAdaptation::ConservativeOriginFallback
                }
                : std::optional<SearchTimeDomainAdaptation>{
                    SearchTimeDomainAdaptation::Strict
                }
        };
    }

    mathfp::Expected<std::optional<SearchTimeDomainExecution>> prepare_search_time_domain_execution(
          const InputModel&              input
        , const SearchTimePaddingPolicy& padding_policy
        , const SplitParams&             split
        , SearchArchitecture             architecture
        , SearchTimeDomainRolloutStage   rollout_stage
        , SearchWindowMode               requested_mode
    ) {
        MATHFP_TRY_LET(
              SearchTimeDomainExecutionPlan
            , plan
            , plan_search_time_domain_execution(
                  architecture
                , rollout_stage
                , requested_mode
            )
        );

        const auto config_summary = summarize(
            SearchTimeDomainConfig{
                  .model = SearchTimeDomainModelConfig{
                      .requested_mode = requested_mode
                    , .padding_policy = padding_policy
                  }
                , .runtime = SearchTimeDomainRuntimeConfig{
                      .architecture  = architecture
                    , .rollout_stage = rollout_stage
                  }
            }
        );
        timetable::infra::progress::log(
            format_search_time_domain_config_summary(config_summary)
            , timetable::infra::LogLevel::Info
        );
        timetable::infra::progress::log(
            fmt::format(
                  "search-time plan: enabled={} adaptation={}"
                , plan.enabled ? "true" : "false"
                , plan.execution_adaptation.has_value()
                    ? std::to_string(static_cast<std::int64_t>(*plan.execution_adaptation))
                    : std::string{"<none>"}
            )
            , timetable::infra::LogLevel::Info
        );

        if (!plan.enabled) {
            return std::optional<SearchTimeDomainExecution>{};
        }

        MATHFP_TRY(validate_search_time_domain_builder_input(
              input
            , requested_mode
            , padding_policy
            , split
        ));
        MATHFP_TRY_LET(
              SearchTimeDomainCatalog
            , catalog
            , build_search_time_domain_catalog(
                  input
                , requested_mode
                , padding_policy
                , split
            )
        );
        timetable::infra::progress::log(
            format_search_time_domain_catalog_summary(summarize(catalog))
            , timetable::infra::LogLevel::Info
        );
        MATHFP_TRY(validate_search_time_domain_builder_output(
              catalog
            , input
            , requested_mode
        ));

        MATHFP_TRY_LET(
              SearchTimeDomainExecution
            , execution
            , adapt_search_time_domain_for_origin_search(
                  catalog
                , *plan.execution_adaptation
            )
        );
        MATHFP_TRY(validate_search_time_domain_execution(execution));
        timetable::infra::progress::log(
            format_search_time_domain_execution_summary(summarize(execution))
            , timetable::infra::LogLevel::Info
        );
        return std::optional<SearchTimeDomainExecution>{ std::move(execution) };
    }

}  // namespace timetable::domain::assignment
