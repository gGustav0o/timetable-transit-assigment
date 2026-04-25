#include "timetable/infra/pair_file_data_source.hpp"

#include "timetable/domain/params_factory.hpp"
#include "timetable/infra/demand_csv.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/presegmented_input.hpp"
#include "timetable/infra/segments_csv.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <mathfp/core/applicative.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::infra {

    namespace {
        struct DefaultToleranceBundle final {
            timetable::domain::SearchTolerances search{};
            timetable::domain::ChoiceTolerances choice{};
        };

        struct DefaultSearchBundle final {
            timetable::domain::PreprocessParams preprocess{};
            timetable::domain::SearchImpedance  impedance{};
            timetable::domain::TransferLimits   transfers{};
        };

        struct PairDemandPaths final {
            std::filesystem::path intervals{};
            std::filesystem::path demand{};
        };

        struct PairResolvedPaths final {
            std::filesystem::path                segments{};
            std::filesystem::path                intervals{};
            std::filesystem::path                demand{};
            std::optional<std::filesystem::path> params_txt{};
        };

        struct PairRuntimeDefaults final {
            timetable::domain::SearchParams                       params{};
            timetable::domain::assignment::ChoiceConfig           choice{};
            timetable::domain::assignment::SearchPruningConfig    search_pruning{};
            timetable::domain::assignment::SearchTimeDomainConfig search_time_domain{};
        };

        mathfp::Expected<std::filesystem::path> resolve_pair_support_file(
              const std::filesystem::path& root
            , std::string_view             name
        ) {
            const auto direct_path    = root / std::string(name);
            const auto generated_path = root / "generated_demand" / std::string(name);

            if (std::filesystem::exists(direct_path)) {
                if (!std::filesystem::is_regular_file(direct_path)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("pair input support path is not a regular file")
                            .ctx("path", direct_path.string())
                    );
                }
                return direct_path;
            }
            if (std::filesystem::exists(generated_path)) {
                if (!std::filesystem::is_regular_file(generated_path)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("pair input support path is not a regular file")
                            .ctx("path", generated_path.string())
                    );
                }
                return generated_path;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("missing required pair input support file")
                    .ctx("file", std::string(name))
                    .ctx("searched_primary", direct_path.string())
                    .ctx("searched_generated_demand", generated_path.string())
            );
        }

        mathfp::Expected<PairDemandPaths> resolve_pair_demand_paths(
            const std::filesystem::path& root
        ) {
            MATHFP_TRY_LET(std::filesystem::path, intervals_path, resolve_pair_support_file(root, "time_intervals.csv"));
            MATHFP_TRY_LET(std::filesystem::path, demand_path, resolve_pair_support_file(root, "od_demand.csv"));
            return PairDemandPaths{
                  .intervals = std::move(intervals_path)
                , .demand    = std::move(demand_path)
            };
        }

        std::optional<std::filesystem::path> resolve_optional_pair_params_path(
            const std::filesystem::path& root
        ) {
            const auto path = root / "params.txt";
            if (std::filesystem::exists(path) && std::filesystem::is_regular_file(path)) {
                return path;
            }
            return std::nullopt;
        }

        mathfp::Expected<PairResolvedPaths> resolve_pair_file_paths(
            const std::filesystem::path& root
        ) {
            const auto segments_path = root / "connection_segments_input.csv";
            if (!std::filesystem::exists(segments_path)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing required connection_segments_input.csv")
                        .ctx("path", segments_path.string())
                );
            }
            if (!std::filesystem::is_regular_file(segments_path)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("pair segments path is not a regular file")
                        .ctx("path", segments_path.string())
                );
            }

            MATHFP_TRY_LET(PairDemandPaths, demand_paths, resolve_pair_demand_paths(root));
            return PairResolvedPaths{
                  .segments   = std::move(segments_path)
                , .intervals  = std::move(demand_paths.intervals)
                , .demand     = std::move(demand_paths.demand)
                , .params_txt = resolve_optional_pair_params_path(root)
            };
        }

        mathfp::Expected<timetable::domain::assignment::SearchTimeDomainConfig> make_pair_default_search_time_domain_config() {
            using namespace timetable::domain;
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(
                  SearchTimePaddingPolicy
                , padding_policy
                , make_fixed_search_time_padding_policy(
                    SearchTimePadding{
                          .before_start = Time{ 0.0 }
                        , .after_end    = Time{ 0.0 }
                    }
                )
            );

            return SearchTimeDomainConfig{
                  .model = SearchTimeDomainModelConfig{
                      .requested_mode = SearchWindowMode::Global
                    , .padding_policy = std::move(padding_policy)
                  }
                , .runtime = SearchTimeDomainRuntimeConfig{
                      .architecture  = SearchArchitecture::OriginWideBranchAndBound
                    , .rollout_stage = SearchTimeDomainRolloutStage::GlobalStrict
                  }
            };
        }

        mathfp::Expected<timetable::domain::assignment::SearchPruningConfig> make_pair_default_search_pruning_config() {
            using namespace timetable::domain::assignment;

            return SearchPruningConfig{
                  .model = SearchPruningModelConfig{
                      .requested_state_space = SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , .runtime = SearchPruningRuntimeConfig{
                      .rollout_stage = SearchPruningRolloutStage::ExactAndApproximateCurrentState
                  }
            };
        }

        mathfp::Expected<timetable::domain::assignment::ChoiceConfig> make_pair_default_choice_config() {
            using namespace timetable::domain::assignment;

            return ChoiceConfig{
                .rollout_stage = ChoiceRolloutStage::ExactAndApproximate
            };
        }

        mathfp::Expected<timetable::domain::SearchParams> make_pair_default_search_params() {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  DefaultSearchBundle
                , search_bundle
                , mathfp::app::lift3(
                    [](PreprocessParams preprocess, SearchImpedance impedance, TransferLimits transfers) {
                        return DefaultSearchBundle{
                              .preprocess = std::move(preprocess)
                            , .impedance  = std::move(impedance)
                            , .transfers  = std::move(transfers)
                        };
                    }
                    , make_preprocess_params(
                          WalkCostKind::Time
                        , WalkCostWeights{
                              .w_time   = Dimless{ 1.0 }
                            , .w_length = Dimless{ 0.0 }
                        }
                        , std::nullopt
                        , true
                        , false
                        , true
                        , true
                        , TimeAggregationKind::Mean
                        , true
                        , true
                    )
                    , make_search_impedance(
                          Dimless{ 1.0 }
                        , Dimless{ 12.0 }
                        , Dimless{ 1.0 }
                        , FareNormalization{
                              .kind        = FareNormalization::Kind::Median
                            , .fixed_scale = 1.0
                        }
                    )
                    , make_transfer_limits(
                          TransferCount{ 5 }
                        , Time         { 0.0 }
                        , Time         { 30.0 }
                        , true
                        , true
                    )
                )
            );
            MATHFP_TRY_LET(
                  DefaultToleranceBundle
                , tolerance_bundle
                , mathfp::app::lift2(
                    [](SearchTolerances search, ChoiceTolerances choice) {
                        return DefaultToleranceBundle{
                              .search = std::move(search)
                            , .choice = std::move(choice)
                        };
                    }
                    , make_search_tolerances(
                          Dimless{ 1.2 }
                        , Dimless{ 10.0 }
                        , Dimless{ 1.2 }
                        , Dimless{ 10.0 }
                        , Dimless{ 1.0 }
                        , Dimless{ 1.0 }
                    )
                    , make_choice_tolerances(
                          Dimless{ 1.2 }
                        , Dimless{ 10.0 }
                        , Dimless{ 1.2 }
                        , Dimless{ 10.0 }
                        , Dimless{ 1.0 }
                        , Dimless{ 1.0 }
                    )
                )
            );
            MATHFP_TRY_LET(
                  SplitParams
                , split
                , make_split_params(
                      Dimless{ 1.0 }
                    , Dimless{ 1.0 }
                    , Dimless{ 1.0 }
                    , PerceivedJourneyTimeWeights{
                          .journey_time   = Dimless{ 1.0 }
                        , .transfer_time  = Dimless{ 2.0 }
                        , .transfer_count = Dimless{ 2.0 }
                    }
                    , TemporalUtilityWeights{
                          .early_departure = Dimless{ 1.0 }
                        , .late_departure  = Dimless{ 1.0 }
                    }
                    , Dimless{ 4.0 }
                    , Dimless{ 1.0 }
                    , Dimless{ 1.0 }
                    , Dimless{ 60.0 }
                    , Dimless{ 0.6 }
                    , Dimless{ 0.3 }
                )
            );

            return make_search_params(
                  std::move(search_bundle.preprocess)
                , std::move(search_bundle.impedance)
                , std::move(search_bundle.transfers)
                , std::move(tolerance_bundle.search)
                , std::move(tolerance_bundle.choice)
                , std::move(split)
            );
        }

        mathfp::Expected<PairRuntimeDefaults> make_pair_runtime_defaults() {
            MATHFP_TRY_LET(
                  timetable::domain::SearchParams
                , params
                , make_pair_default_search_params()
            );
            MATHFP_TRY_LET(
                  timetable::domain::assignment::ChoiceConfig
                , choice
                , make_pair_default_choice_config()
            );
            MATHFP_TRY_LET(
                  timetable::domain::assignment::SearchPruningConfig
                , search_pruning
                , make_pair_default_search_pruning_config()
            );
            MATHFP_TRY_LET(
                  timetable::domain::assignment::SearchTimeDomainConfig
                , search_time_domain
                , make_pair_default_search_time_domain_config()
            );

            return PairRuntimeDefaults{
                  .params             = std::move(params)
                , .choice             = std::move(choice)
                , .search_pruning     = std::move(search_pruning)
                , .search_time_domain = std::move(search_time_domain)
            };
        }

        void log_pair_file_paths(const PairResolvedPaths& paths) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;

            log(
                fmt::format(
                      "pair input files:\n"
                      "  segments  = {}\n"
                      "  intervals = {}\n"
                      "  demand    = {}"
                    , paths.segments .string()
                    , paths.intervals.string()
                    , paths.demand   .string()
                )
                , LogLevel::Info
            );

            if (paths.params_txt.has_value()) {
                log(
                    fmt::format(
                          "pair input file present but intentionally ignored in current scope: {}"
                        , paths.params_txt->string()
                    )
                    , LogLevel::Info
                );
            }
        }

        mathfp::Expected<timetable::domain::AssignmentInput> load_pair_segments_input(
            const PairResolvedPaths& paths
        ) {
            using timetable::infra::progress::status;

            status("parsing: loading pair segments input");
            return csv::parse_connection_segments_csv(paths.segments)
                | mathfp::fp::pipe::and_then(build_default_presegmented_assignment_input);
        }

        mathfp::Expected<mathfp::Unit> load_pair_demand_input(
              timetable::domain::AssignmentInput& input
            , const PairResolvedPaths&            paths
        ) {
            using timetable::infra::progress::status;

            status("parsing: loading pair demand input");
            MATHFP_TRY_LET(
                  std::vector<timetable::domain::TimeInterval>
                , intervals
                , csv::parse_time_intervals_csv(paths.intervals)
            );
            MATHFP_TRY_LET(
                  std::vector<timetable::domain::DemandEntry>
                , demand
                , csv::parse_od_demand_csv(paths.demand, intervals)
            );

            input.input.intervals = std::move(intervals);
            input.input.demand    = std::move(demand);
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> apply_pair_runtime_defaults(
              timetable::domain::AssignmentInput& input
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            status("parsing: applying built-in pair defaults");
            log(
                  "parsing: pair-file runtime uses built-in defaults in the current scope"
                , LogLevel::Info
            );

            MATHFP_TRY_LET(
                  PairRuntimeDefaults
                , defaults
                , make_pair_runtime_defaults()
            );
            input.params             = std::move(defaults.params);
            input.choice             = std::move(defaults.choice);
            input.search_pruning     = std::move(defaults.search_pruning);
            input.search_time_domain = std::move(defaults.search_time_domain);
            return mathfp::kUnit;
        }

        mathfp::Expected<timetable::domain::AssignmentInput> load_pair_assignment_input(
            const PairResolvedPaths& paths
        ) {
            using timetable::infra::progress::status;

            log_pair_file_paths(paths);
            MATHFP_TRY_LET(
                  timetable::domain::AssignmentInput
                , input
                , load_pair_segments_input(paths)
            );
            MATHFP_TRY(load_pair_demand_input(input, paths));
            MATHFP_TRY(apply_pair_runtime_defaults(input));
            status("parsing: pair input ready");
            return input;
        }

        class PairFileDataSource final : public io::DataSource {
        public:
            explicit PairFileDataSource(PairResolvedPaths paths) : paths_(std::move(paths)) {}

            mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
                return load_pair_assignment_input(paths_);
            }

        private:
            PairResolvedPaths paths_{};
        };

    }  // namespace

    mathfp::Expected<std::unique_ptr<io::DataSource>> make_pair_file_data_source(
        io::PairDataDirSpec spec
    ) {
        if (spec.root.empty())
            return mathfp::unexpected(mathfp::invalid_arg("pair data dir path is empty"));

        if (!std::filesystem::exists(spec.root))
            return mathfp::unexpected(
                mathfp::invalid_arg("pair data dir does not exist")
                .ctx("path", spec.root.string())
            );

        if (!std::filesystem::is_directory(spec.root))
            return mathfp::unexpected(
                mathfp::invalid_arg("pair data dir is not a directory")
                .ctx("path", spec.root.string())
            );

        MATHFP_TRY_LET(PairResolvedPaths, paths, resolve_pair_file_paths(spec.root));

        return std::make_unique<PairFileDataSource>(std::move(paths));
    }

}  // namespace timetable::infra
