#include "timetable/infra/pair_file_data_source.hpp"

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/params_factory.hpp"
#include "timetable/infra/demand_csv.hpp"
#include "timetable/infra/params_txt.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/presegmented_input.hpp"
#include "timetable/infra/segments_csv.hpp"

#include <cstdint>
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
            timetable::domain::assignment::SkimMatrixConfig       skim_matrix{};
            timetable::domain::assignment::AssignmentPeriodConfig assignment_period{};
        };

        struct PairSearchTimeDomainSpec final {
            timetable::domain::assignment::SearchWindowMode             requested_mode{};
            timetable::domain::assignment::SearchArchitecture           architecture{};
            timetable::domain::assignment::SearchTimeDomainRolloutStage rollout_stage{};
        };

        struct PairSearchPruningSpec final {
            timetable::domain::assignment::SearchPruningStateSpace   state_space{};
            timetable::domain::assignment::SearchPruningRolloutStage rollout_stage{};
        };

        struct PairChoiceSpec final {
            timetable::domain::assignment::ChoiceRolloutStage rollout_stage{};
        };

        struct PairPreprocessSpec final {
            double                                  walk_time_weight{};
            double                                  walk_length_weight{};
            std::optional<timetable::domain::Speed> line_speed{};
            bool                                    strict_trips{};
            bool                                    allow_overnight{};
            bool                                    overnight_add_24h{};
            bool                                    strict_stop_times{};
            bool                                    deduplicate_walk_segments{};
            bool                                    stable_ordering{};
            timetable::domain::WalkCostKind         walk_cost_kind{};
            timetable::domain::TimeAggregationKind  time_aggregation{};
        };

        struct PairSearchImpedanceSpec final {
            double                               in_vehicle_time{};
            double                               access_time{};
            double                               egress_time{};
            double                               transfer_walk_time{};
            double                               transfer_wait_time{};
            double                               transfer_count{};
            double                               fare{};
            timetable::domain::FareNormalization fare_normalization{};
        };

        struct PairTransferSpec final {
            std::int32_t max_transfers{};
            double       min_transfer_wait{};
            double       max_transfer_wait{};
            bool         allow_start_wait{};
            bool         allow_end_wait{};
        };

        struct PairToleranceSpec final {
            double imp_mult{};
            double imp_add{};
            double jt_mult{};
            double jt_add{};
            double nt_mult{};
            double nt_add{};
        };

        struct PairSplitSpec final {
            double q_time{};
            double q_departure{};
            double q_fare{};
            double pjt_in_vehicle_time{};
            double pjt_access_time{};
            double pjt_egress_time{};
            double pjt_transfer_walk_time{};
            double pjt_transfer_wait_time{};
            double pjt_transfer_count{};
            double departure_early{};
            double departure_late{};
            double beta{};
            double boxcox_t{};
            double gamma{};
            double temporal_similarity_scale{};
            double higher_quality_scale{};
            double lower_quality_scale{};
        };

        struct PairRuntimeDefaultSpec final {
            PairPreprocessSpec       preprocess{};
            PairSearchImpedanceSpec  search_impedance{};
            PairTransferSpec         transfers{};
            PairToleranceSpec        search_tolerances{};
            PairToleranceSpec        choice_tolerances{};
            PairSplitSpec            split{};
            PairChoiceSpec           choice{};
            PairSearchPruningSpec    search_pruning{};
            PairSearchTimeDomainSpec search_time_domain{};
            bool                     skim_matrix_enabled{};
            double                   pre_assign_period{};
            double                   post_assign_period{};
        };

        inline constexpr PairToleranceSpec kPairDefaultToleranceSpec{
              .imp_mult = 1.2
            , .imp_add  = 10.0
            , .jt_mult  = 1.2
            , .jt_add   = 10.0
            , .nt_mult  = 1.0
            , .nt_add   = 1.0
        };

        inline constexpr PairRuntimeDefaultSpec kPairRuntimeDefaultSpec{
              .preprocess = PairPreprocessSpec{
                    .walk_time_weight           = 1.0
                  , .walk_length_weight         = 0.0
                  , .line_speed                 = std::nullopt
                  , .strict_trips               = true
                  , .allow_overnight            = false
                  , .overnight_add_24h          = true
                  , .strict_stop_times          = true
                  , .deduplicate_walk_segments  = true
                  , .stable_ordering            = true
                  , .walk_cost_kind             = timetable::domain::WalkCostKind::Time
                  , .time_aggregation           = timetable::domain::TimeAggregationKind::Mean
                }
            , .search_impedance = PairSearchImpedanceSpec{
                    .in_vehicle_time    = 1.0
                  , .access_time        = 1.2
                  , .egress_time        = 1.3
                  , .transfer_walk_time = 1.5
                  , .transfer_wait_time = 2.25
                  , .transfer_count     = 12.0
                  , .fare               = 1.0
                  , .fare_normalization = timetable::domain::FareNormalization{
                          .kind        = timetable::domain::FareNormalization::Kind::Median
                        , .fixed_scale = 1.0
                    }
                }
            , .transfers = PairTransferSpec{
                    .max_transfers     = 5
                  , .min_transfer_wait = 0.0
                  , .max_transfer_wait = 30.0
                  , .allow_start_wait  = true
                  , .allow_end_wait    = true
                }
            , .search_tolerances = kPairDefaultToleranceSpec
            , .choice_tolerances = kPairDefaultToleranceSpec
            , .split = PairSplitSpec{
                    .q_time                    = 1.0
                  , .q_departure               = 1.0
                  , .q_fare                    = 1.0
                  , .pjt_in_vehicle_time       = 1.0
                  , .pjt_access_time           = 1.2
                  , .pjt_egress_time           = 1.3
                  , .pjt_transfer_walk_time    = 1.5
                  , .pjt_transfer_wait_time    = 2.25
                  , .pjt_transfer_count        = 2.0
                  , .departure_early           = 1.0
                  , .departure_late            = 1.0
                  , .beta                      = 4.0
                  , .boxcox_t                  = 1.0
                  , .gamma                     = 1.0
                  , .temporal_similarity_scale = 60.0
                  , .higher_quality_scale      = 0.6
                  , .lower_quality_scale       = 0.3
                }
            , .choice = PairChoiceSpec{
                    .rollout_stage = timetable::domain::assignment::ChoiceRolloutStage::ExactAndApproximate
                }
            , .search_pruning = PairSearchPruningSpec{
                    .state_space   = timetable::domain::assignment::SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  , .rollout_stage = timetable::domain::assignment::SearchPruningRolloutStage::ExactAndApproximateCurrentState
                }
            , .search_time_domain = PairSearchTimeDomainSpec{
                    .requested_mode = timetable::domain::assignment::SearchWindowMode::PerOd
                  , .architecture   = timetable::domain::assignment::SearchArchitecture::OriginWideBranchAndBound
                  , .rollout_stage  = timetable::domain::assignment::SearchTimeDomainRolloutStage::PerOdConservativeFallback
                }
            , .skim_matrix_enabled = false
            , .pre_assign_period   = 0.0
            , .post_assign_period  = 0.0
        };

        mathfp::Expected<std::filesystem::path> resolve_pair_support_file(
              const std::filesystem::path& root
            , std::string_view             name
        ) {
            const auto direct_path    = root / std::string(name);
            const auto generated_path = root / "generated_demand_informative_300" / std::string(name);

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
                , make_split_temporal_utility_padding_policy(Dimless{ 0.0 })
            );

            return SearchTimeDomainConfig{
                  .model = SearchTimeDomainModelConfig{
                      .requested_mode = kPairRuntimeDefaultSpec.search_time_domain.requested_mode
                    , .padding_policy = std::move(padding_policy)
                  }
                , .runtime = SearchTimeDomainRuntimeConfig{
                      .architecture  = kPairRuntimeDefaultSpec.search_time_domain.architecture
                    , .rollout_stage = kPairRuntimeDefaultSpec.search_time_domain.rollout_stage
                  }
            };
        }

        mathfp::Expected<timetable::domain::assignment::SearchPruningConfig> make_pair_default_search_pruning_config() {
            using namespace timetable::domain::assignment;

            return SearchPruningConfig{
                  .model = SearchPruningModelConfig{
                      .requested_state_space = kPairRuntimeDefaultSpec.search_pruning.state_space
                  }
                , .runtime = SearchPruningRuntimeConfig{
                      .rollout_stage = kPairRuntimeDefaultSpec.search_pruning.rollout_stage
                  }
            };
        }

        mathfp::Expected<timetable::domain::assignment::ChoiceConfig> make_pair_default_choice_config() {
            using namespace timetable::domain::assignment;

            return ChoiceConfig{
                .rollout_stage = kPairRuntimeDefaultSpec.choice.rollout_stage
            };
        }

        mathfp::Expected<timetable::domain::assignment::SkimMatrixConfig> make_pair_default_skim_matrix_config() {
            using namespace timetable::domain::assignment;

            return make_skim_matrix_config(
                  kPairRuntimeDefaultSpec.skim_matrix_enabled
                , SkimAggregationFunc::Mean
                , true
                , 0.5
                , 1.0
            );
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentPeriodConfig> make_pair_default_assignment_period_config() {
            using namespace timetable::domain::assignment;

            return make_assignment_period_config(
                  kPairRuntimeDefaultSpec.pre_assign_period
                , kPairRuntimeDefaultSpec.post_assign_period
            );
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
                          kPairRuntimeDefaultSpec.preprocess.walk_cost_kind
                        , WalkCostWeights{
                              .w_time   = Dimless{ kPairRuntimeDefaultSpec.preprocess.walk_time_weight }
                            , .w_length = Dimless{ kPairRuntimeDefaultSpec.preprocess.walk_length_weight }
                        }
                        , kPairRuntimeDefaultSpec.preprocess.line_speed
                        , kPairRuntimeDefaultSpec.preprocess.strict_trips
                        , kPairRuntimeDefaultSpec.preprocess.allow_overnight
                        , kPairRuntimeDefaultSpec.preprocess.overnight_add_24h
                        , kPairRuntimeDefaultSpec.preprocess.strict_stop_times
                        , kPairRuntimeDefaultSpec.preprocess.time_aggregation
                        , kPairRuntimeDefaultSpec.preprocess.deduplicate_walk_segments
                        , kPairRuntimeDefaultSpec.preprocess.stable_ordering
                    )
                    , make_search_impedance(
                          Dimless{ kPairRuntimeDefaultSpec.search_impedance.in_vehicle_time }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.access_time }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.egress_time }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.transfer_walk_time }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.transfer_wait_time }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.transfer_count }
                        , Dimless{ kPairRuntimeDefaultSpec.search_impedance.fare }
                        , kPairRuntimeDefaultSpec.search_impedance.fare_normalization
                    )
                    , make_transfer_limits(
                          TransferCount{ kPairRuntimeDefaultSpec.transfers.max_transfers }
                        , Time{ kPairRuntimeDefaultSpec.transfers.min_transfer_wait }
                        , Time{ kPairRuntimeDefaultSpec.transfers.max_transfer_wait }
                        , kPairRuntimeDefaultSpec.transfers.allow_start_wait
                        , kPairRuntimeDefaultSpec.transfers.allow_end_wait
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
                          Dimless{ kPairRuntimeDefaultSpec.search_tolerances.imp_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.search_tolerances.imp_add }
                        , Dimless{ kPairRuntimeDefaultSpec.search_tolerances.jt_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.search_tolerances.jt_add }
                        , Dimless{ kPairRuntimeDefaultSpec.search_tolerances.nt_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.search_tolerances.nt_add }
                    )
                    , make_choice_tolerances(
                          Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.imp_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.imp_add }
                        , Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.jt_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.jt_add }
                        , Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.nt_mult }
                        , Dimless{ kPairRuntimeDefaultSpec.choice_tolerances.nt_add }
                    )
                )
            );
            MATHFP_TRY_LET(
                  SplitParams
                , split
                , make_split_params(
                      Dimless{ kPairRuntimeDefaultSpec.split.q_time }
                    , Dimless{ kPairRuntimeDefaultSpec.split.q_departure }
                    , Dimless{ kPairRuntimeDefaultSpec.split.q_fare }
                    , PerceivedJourneyTimeWeights{
                          .in_vehicle_time    = Dimless{ kPairRuntimeDefaultSpec.split.pjt_in_vehicle_time }
                        , .access_time        = Dimless{ kPairRuntimeDefaultSpec.split.pjt_access_time }
                        , .egress_time        = Dimless{ kPairRuntimeDefaultSpec.split.pjt_egress_time }
                        , .transfer_walk_time = Dimless{ kPairRuntimeDefaultSpec.split.pjt_transfer_walk_time }
                        , .transfer_wait_time = Dimless{ kPairRuntimeDefaultSpec.split.pjt_transfer_wait_time }
                        , .transfer_count     = Dimless{ kPairRuntimeDefaultSpec.split.pjt_transfer_count }
                    }
                    , TemporalUtilityWeights{
                          .early_departure = Dimless{ kPairRuntimeDefaultSpec.split.departure_early }
                        , .late_departure  = Dimless{ kPairRuntimeDefaultSpec.split.departure_late }
                    }
                    , Dimless{ kPairRuntimeDefaultSpec.split.beta }
                    , Dimless{ kPairRuntimeDefaultSpec.split.boxcox_t }
                    , Dimless{ kPairRuntimeDefaultSpec.split.gamma }
                    , Dimless{ kPairRuntimeDefaultSpec.split.temporal_similarity_scale }
                    , Dimless{ kPairRuntimeDefaultSpec.split.higher_quality_scale }
                    , Dimless{ kPairRuntimeDefaultSpec.split.lower_quality_scale }
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
            MATHFP_TRY_LET(
                  timetable::domain::assignment::SkimMatrixConfig
                , skim_matrix
                , make_pair_default_skim_matrix_config()
            );
            MATHFP_TRY_LET(
                  timetable::domain::assignment::AssignmentPeriodConfig
                , assignment_period
                , make_pair_default_assignment_period_config()
            );

            return PairRuntimeDefaults{
                  .params             = std::move(params)
                , .choice             = std::move(choice)
                , .search_pruning     = std::move(search_pruning)
                , .search_time_domain = std::move(search_time_domain)
                , .skim_matrix        = std::move(skim_matrix)
                , .assignment_period  = std::move(assignment_period)
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
                          "pair input file: {}"
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

        mathfp::Expected<mathfp::Unit> apply_pair_runtime_configuration(
              timetable::domain::AssignmentInput& input
            , const PairResolvedPaths&            paths
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            MATHFP_TRY_LET(
                  PairRuntimeDefaults
                , defaults
                , make_pair_runtime_defaults()
            );
            input.params             = std::move(defaults.params);
            input.choice             = std::move(defaults.choice);
            input.search_pruning     = std::move(defaults.search_pruning);
            input.search_time_domain = std::move(defaults.search_time_domain);
            input.skim_matrix        = std::move(defaults.skim_matrix);
            input.assignment_period  = std::move(defaults.assignment_period);

            if (!paths.params_txt.has_value()) {
                status("parsing: applying built-in pair defaults");
                log(
                      "parsing: pair-file runtime uses built-in parameter defaults; params.txt not found"
                    , LogLevel::Info
                );
                return mathfp::kUnit;
            }

            status("parsing: applying params.txt");
            MATHFP_TRY_LET(
                  timetable::domain::AssignmentRuntimeParams
                , parsed_params
                , timetable::infra::params_txt::parse_assignment_runtime_params_file(*paths.params_txt)
            );
            input.params            = std::move(parsed_params.search);
            input.skim_matrix       = std::move(parsed_params.skim_matrix);
            input.assignment_period = std::move(parsed_params.assignment_period);
            log(
                  "parsing: pair-file runtime uses SearchParams, SkimMatrixConfig, and AssignmentPeriodConfig from params.txt; runtime choice/pruning/search-time configs keep built-in rollout defaults"
                , LogLevel::Info
            );
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
            MATHFP_TRY(apply_pair_runtime_configuration(input, paths));
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
