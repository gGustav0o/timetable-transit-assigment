#include "detail/params_txt.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <mathfp/core/applicative.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/params_factory.hpp"

namespace timetable::infra::params_txt::detail {

    namespace {

        struct ObjectFieldSpec final {
            std::string_view key{};
            std::string_view path{};
            std::string_view field{};
        };

        struct NumberFieldSpec final {
            std::string_view key{};
            std::string_view path{};
            std::string_view field{};
        };

        struct StringFieldSpec final {
            std::string_view key{};
            std::string_view path{};
            std::string_view field{};
        };

        template <std::size_t N>
        using DoubleArray = std::array<double, N>;

        template <std::size_t N>
        using ObjectArray = std::array<const Object*, N>;

        template <std::size_t N>
        using StringArray = std::array<std::string, N>;

        struct ParsedToleranceBundle final {
            timetable::domain::SearchTolerances search{};
            timetable::domain::ChoiceTolerances choice{};
        };

        struct ParsedSearchBundle final {
            timetable::domain::PreprocessParams preprocess{};
            timetable::domain::SearchImpedance  impedance{};
            timetable::domain::TransferLimits   transfers{};
        };

        struct ToleranceFields final {
            double imp_mult{};
            double imp_add{};
            double jt_mult{};
            double jt_add{};
            double nt_mult{};
            double nt_add{};
        };

        struct TransferFields final {
            double max_transfers{};
            double min_transfer_wait{};
            double max_transfer_wait{};
        };

        struct SearchImpedanceFields final {
            double in_vehicle_time{};
            double access_time{};
            double egress_time{};
            double transfer_walk_time{};
            double transfer_wait_time{};
            double transfer_count{};
            double fare{};
        };

        struct SplitImpedanceFields final {
            double time{};
            double departure_early{};
            double departure_late{};
            double fare{};
        };

        struct PerceivedJourneyTimeFields final {
            double in_vehicle_time{};
            double access_time{};
            double egress_time{};
            double transfer_walk_time{};
            double transfer_wait_time{};
            double transfer_count{};
        };

        struct SplitIndependenceFields final {
            double gamma{};
            double temporal_similarity_scale{};
            double higher_quality_scale{};
            double lower_quality_scale{};
        };

        struct SkimMatrixFields final {
            bool        enabled{};
            std::string func{};
            double      volume_weighted{};
            double      quantile{};
            double      low_impedance_connection_share{};
        };

        struct AssignmentPeriodFields final {
            double pre_assign_period{};
            double post_assign_period{};
        };

        struct ConnectionDeletionFields final {
            bool delete_outside_assignment_period{};
            bool delete_departures_before_assignment_period_for_departure_based{};
            bool delete_arrivals_after_assignment_period_for_arrival_based{};
        };

        struct DemandSegmentTimeFields final {
            bool dep_based_demand_segment{};
            bool consider_connections_with_positive_delta_t{};
        };

        namespace schema {

            struct ParamsTxtSchema final {
                std::array<ObjectFieldSpec, 3> root_objects{
                    ObjectFieldSpec{ "searchPara", "root", "root.search_para" },
                    ObjectFieldSpec{ "choicePara", "root", "root.choice_para" },
                    ObjectFieldSpec{ "splitPara" , "root", "root.split_para" }
                };

                std::array<ObjectFieldSpec, 3> search_objects{
                    ObjectFieldSpec{ "ToleranceConstraints", "root.searchPara", "search.tolerances" },
                    ObjectFieldSpec{ "TemporalSuitability" , "root.searchPara", "search.temporal" },
                    ObjectFieldSpec{ "SearchImp"           , "root.searchPara", "search.impedance" }
                };

                std::array<ObjectFieldSpec, 1> choice_objects{
                    ObjectFieldSpec{ "ToleranceConstraints", "root.choicePara", "choice.tolerances" }
                };

                std::array<ObjectFieldSpec, 2> split_objects{
                    ObjectFieldSpec{ "Independence", "root.splitPara", "split.independence" },
                    ObjectFieldSpec{ "SplitImp"    , "root.splitPara", "split.impedance" }
                };

                std::array<ObjectFieldSpec, 1> split_imp_objects{
                    ObjectFieldSpec{
                          "PerceivedJourneyTime"
                        , "root.splitPara.SplitImp"
                        , "split.perceived_journey_time"
                    }
                };

                std::array<NumberFieldSpec, 6> search_tolerances{
                    NumberFieldSpec{ "minSearchImpFactor"      , "root.searchPara.ToleranceConstraints", "search_tolerances.imp_mult" },
                    NumberFieldSpec{ "minSearchImpAbs"         , "root.searchPara.ToleranceConstraints", "search_tolerances.imp_add" },
                    NumberFieldSpec{ "minJourneyTimeFactor"    , "root.searchPara.ToleranceConstraints", "search_tolerances.jt_mult" },
                    NumberFieldSpec{ "minJourneyTimeAbs"       , "root.searchPara.ToleranceConstraints", "search_tolerances.jt_add" },
                    NumberFieldSpec{ "minNumberTransfersFactor", "root.searchPara.ToleranceConstraints", "search_tolerances.nt_mult" },
                    NumberFieldSpec{ "minNumberTransfersAbs"   , "root.searchPara.ToleranceConstraints", "search_tolerances.nt_add" }
                };

                std::array<NumberFieldSpec, 6> choice_tolerances{
                    NumberFieldSpec{ "minSearchImpFactor"      , "root.choicePara.ToleranceConstraints", "choice_tolerances.imp_mult" },
                    NumberFieldSpec{ "minSearchImpAbs"         , "root.choicePara.ToleranceConstraints", "choice_tolerances.imp_add" },
                    NumberFieldSpec{ "minJourneyTimeFactor"    , "root.choicePara.ToleranceConstraints", "choice_tolerances.jt_mult" },
                    NumberFieldSpec{ "minJourneyTimeAbs"       , "root.choicePara.ToleranceConstraints", "choice_tolerances.jt_add" },
                    NumberFieldSpec{ "minNumberTransfersFactor", "root.choicePara.ToleranceConstraints", "choice_tolerances.nt_mult" },
                    NumberFieldSpec{ "minNumberTransfersAbs"   , "root.choicePara.ToleranceConstraints", "choice_tolerances.nt_add" }
                };

                std::array<NumberFieldSpec, 1> transfer_limits{
                    NumberFieldSpec{ "maxNumTransfers", "root.searchPara", "transfer_limits.max_transfers" }
                };

                std::array<NumberFieldSpec, 2> temporal_suitability{
                    NumberFieldSpec{ "minTWT", "root.searchPara.TemporalSuitability", "transfer_limits.min_transfer_wait" },
                    NumberFieldSpec{ "maxTWT", "root.searchPara.TemporalSuitability", "transfer_limits.max_transfer_wait" }
                };

                std::array<NumberFieldSpec, 7> search_impedance{
                    NumberFieldSpec{ "inVehTimeFactor"      , "root.searchPara.SearchImp", "search_impedance.in_vehicle_time" },
                    NumberFieldSpec{ "accessTimeFactor"     , "root.searchPara.SearchImp", "search_impedance.access_time" },
                    NumberFieldSpec{ "egressTimeFactor"     , "root.searchPara.SearchImp", "search_impedance.egress_time" },
                    NumberFieldSpec{ "walkTimeFactor"       , "root.searchPara.SearchImp", "search_impedance.transfer_walk_time" },
                    NumberFieldSpec{ "transferWaitTimeFactor", "root.searchPara.SearchImp", "search_impedance.transfer_wait_time" },
                    NumberFieldSpec{ "numTransfersFactor"   , "root.searchPara.SearchImp", "search_impedance.transfer_count" },
                    NumberFieldSpec{ "supplementsFactor"    , "root.searchPara.SearchImp", "search_impedance.fare" }
                };

                std::array<StringFieldSpec, 1> split_choice_model{
                    StringFieldSpec{ "choiceModel", "root.splitPara", "split.choice_model" }
                };

                std::array<NumberFieldSpec, 4> split_impedance{
                    NumberFieldSpec{ "perceivedJourneyTimeFactor" , "root.splitPara.SplitImp", "split_impedance.q_time" },
                    NumberFieldSpec{ "temporalUtilityFactor_early", "root.splitPara.SplitImp", "split_impedance.q_departure_early" },
                    NumberFieldSpec{ "temporalUtilityFactor_late" , "root.splitPara.SplitImp", "split_impedance.q_departure_late" },
                    NumberFieldSpec{ "fareFactor"                 , "root.splitPara.SplitImp", "split_impedance.q_fare" }
                };

                std::array<NumberFieldSpec, 6> split_perceived_journey_time{
                    NumberFieldSpec{
                          "inVehTimeFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.in_vehicle_time"
                    },
                    NumberFieldSpec{
                          "accessTimeFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.access_time"
                    },
                    NumberFieldSpec{
                          "egressTimeFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.egress_time"
                    },
                    NumberFieldSpec{
                          "walkTimeFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.transfer_walk_time"
                    },
                    NumberFieldSpec{
                          "transferWaitTimeFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.transfer_wait_time"
                    },
                    NumberFieldSpec{
                          "numTransfersFactor"
                        , "root.splitPara.SplitImp.PerceivedJourneyTime"
                        , "split_perceived_journey_time.transfer_count"
                    }
                };

                std::array<NumberFieldSpec, 4> split_independence{
                    NumberFieldSpec{ "gamma"                  , "root.splitPara.Independence", "split_independence.gamma" },
                    NumberFieldSpec{ "indepMaxDelta"          , "root.splitPara.Independence", "split_independence.temporal_similarity_scale" },
                    NumberFieldSpec{ "indepHigherQualityCoeff", "root.splitPara.Independence", "split_independence.higher_quality_scale" },
                    NumberFieldSpec{ "indepLowerQualityCoeff" , "root.splitPara.Independence", "split_independence.lower_quality_scale" }
                };

                std::array<NumberFieldSpec, 1> split_scalars{
                    NumberFieldSpec{ "BoxCoxPara", "root.splitPara", "split.choice_model.boxcox_t" }
                };

                std::array<NumberFieldSpec, 1> split_choice_model_exponent{
                    NumberFieldSpec{ "KirchhoffExp", "root.splitPara", "split.choice_model.exponent" }
                };

                std::array<NumberFieldSpec, 1> split_logit_exponent{
                    NumberFieldSpec{ "logitExp", "root.splitPara", "split.choice_model.exponent" }
                };

                std::array<NumberFieldSpec, 1> split_lohse_exponent{
                    NumberFieldSpec{ "LohseExp", "root.splitPara", "split.choice_model.exponent" }
                };

                std::array<NumberFieldSpec, 1> split_boxcox_exponent{
                    NumberFieldSpec{ "BoxCoxExp", "root.splitPara", "split.choice_model.exponent" }
                };
            };

            inline const ParamsTxtSchema kParamsTxtSchema{};

        }  // namespace schema

        template <class T, std::size_t N>
        std::array<T, N> to_array(std::vector<T> values) {
            std::array<T, N> out{};
            std::move(values.begin(), values.end(), out.begin());
            return out;
        }

        template <std::size_t N>
        mathfp::Expected<ObjectArray<N>> read_object_array(
              const Object&                         obj
            , const std::array<ObjectFieldSpec, N>& specs
        ) {
            MATHFP_TRY_LET(
                  std::vector<const Object*>
                , values
                , mathfp::trv::traverse(specs, [&](const auto& spec) {
                    return object_at(obj, spec.key, spec.path);
                })
            );
            return to_array<const Object*, N>(std::move(values));
        }

        template <std::size_t N>
        mathfp::Expected<DoubleArray<N>> read_number_array(
              const Object&                        obj
            , const std::array<NumberFieldSpec, N>& specs
        ) {
            MATHFP_TRY_LET(
                  std::vector<double>
                , values
                , mathfp::trv::traverse(specs, [&](const auto& spec) {
                    return number_at(obj, spec.key, spec.path);
                })
            );
            return to_array<double, N>(std::move(values));
        }

        template <std::size_t N>
        mathfp::Expected<StringArray<N>> read_string_array(
              const Object&                        obj
            , const std::array<StringFieldSpec, N>& specs
        ) {
            MATHFP_TRY_LET(
                  std::vector<std::string>
                , values
                , mathfp::trv::traverse(specs, [&](const auto& spec) {
                    return string_at(obj, spec.key, spec.path);
                })
            );
            return to_array<std::string, N>(std::move(values));
        }

        mathfp::Expected<ToleranceFields> read_tolerance_fields(
              const Object&                                         obj
            , const std::array<NumberFieldSpec, 6>& field_specs
        ) {
            MATHFP_TRY_LET(DoubleArray<6>, values, read_number_array(obj, field_specs));
            const auto [imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add] = values;
            return ToleranceFields{
                  .imp_mult = imp_mult
                , .imp_add  = imp_add
                , .jt_mult  = jt_mult
                , .jt_add   = jt_add
                , .nt_mult  = nt_mult
                , .nt_add   = nt_add
            };
        }

        mathfp::Expected<TransferFields> read_transfer_fields(
              const Object& search_para
            , const Object& temporal
        ) {
            MATHFP_TRY_LET(
                  DoubleArray<1>
                , search_values
                , read_number_array(search_para, schema::kParamsTxtSchema.transfer_limits)
            );
            MATHFP_TRY_LET(
                  DoubleArray<2>
                , temporal_values
                , read_number_array(temporal, schema::kParamsTxtSchema.temporal_suitability)
            );

            const auto [max_transfers] = search_values;
            const auto [min_twt, max_twt] = temporal_values;
            return TransferFields{
                  .max_transfers     = max_transfers
                , .min_transfer_wait = min_twt
                , .max_transfer_wait = max_twt
            };
        }

        mathfp::Expected<SearchImpedanceFields> read_search_impedance_fields(
              const Object&                                         obj
            , const std::array<NumberFieldSpec, 7>& field_specs
        ) {
            MATHFP_TRY_LET(DoubleArray<7>, values, read_number_array(obj, field_specs));
            const auto [
                  in_vehicle_time
                , access_time
                , egress_time
                , transfer_walk_time
                , transfer_wait_time
                , transfer_count
                , fare
            ] = values;
            return SearchImpedanceFields{
                  .in_vehicle_time    = in_vehicle_time
                , .access_time        = access_time
                , .egress_time        = egress_time
                , .transfer_walk_time = transfer_walk_time
                , .transfer_wait_time = transfer_wait_time
                , .transfer_count     = transfer_count
                , .fare               = fare
            };
        }

        mathfp::Expected<SplitImpedanceFields> read_split_impedance_fields(
              const Object&                                         obj
            , const std::array<NumberFieldSpec, 4>& field_specs
        ) {
            MATHFP_TRY_LET(DoubleArray<4>, values, read_number_array(obj, field_specs));
            const auto [time, departure_early, departure_late, fare] = values;
            return SplitImpedanceFields{
                  .time            = time
                , .departure_early = departure_early
                , .departure_late  = departure_late
                , .fare            = fare
            };
        }

        mathfp::Expected<PerceivedJourneyTimeFields> read_perceived_journey_time_fields(
              const Object&                                         obj
            , const std::array<NumberFieldSpec, 6>& field_specs
        ) {
            MATHFP_TRY_LET(DoubleArray<6>, values, read_number_array(obj, field_specs));
            const auto [
                  in_vehicle_time
                , access_time
                , egress_time
                , transfer_walk_time
                , transfer_wait_time
                , transfer_count
            ] = values;
            return PerceivedJourneyTimeFields{
                  .in_vehicle_time    = in_vehicle_time
                , .access_time        = access_time
                , .egress_time        = egress_time
                , .transfer_walk_time = transfer_walk_time
                , .transfer_wait_time = transfer_wait_time
                , .transfer_count     = transfer_count
            };
        }

        mathfp::Expected<SplitIndependenceFields> read_split_independence_fields(
              const Object&                                         obj
            , const std::array<NumberFieldSpec, 4>& field_specs
        ) {
            MATHFP_TRY_LET(DoubleArray<4>, values, read_number_array(obj, field_specs));
            const auto [gamma, temporal_similarity_scale, higher_quality_scale, lower_quality_scale] = values;
            return SplitIndependenceFields{
                  .gamma                     = gamma
                , .temporal_similarity_scale = temporal_similarity_scale
                , .higher_quality_scale      = higher_quality_scale
                , .lower_quality_scale       = lower_quality_scale
            };
        }

        mathfp::Expected<timetable::domain::SearchTolerances> parse_search_tolerances(
            const Object& search_tol
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  ToleranceFields
                , fields
                , read_tolerance_fields(
                    search_tol, schema::kParamsTxtSchema.search_tolerances
                )
            );
            return make_search_tolerances(
                  Dimless{ fields.imp_mult }
                , Dimless{ fields.imp_add }
                , Dimless{ fields.jt_mult }
                , Dimless{ fields.jt_add }
                , Dimless{ fields.nt_mult }
                , Dimless{ fields.nt_add }
            );
        }

        mathfp::Expected<timetable::domain::ChoiceTolerances> parse_choice_tolerances(
            const Object& choice_tol
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  ToleranceFields
                , fields
                , read_tolerance_fields(
                    choice_tol, schema::kParamsTxtSchema.choice_tolerances
                )
            );
            return make_choice_tolerances(
                  Dimless{ fields.imp_mult }
                , Dimless{ fields.imp_add }
                , Dimless{ fields.jt_mult }
                , Dimless{ fields.jt_add }
                , Dimless{ fields.nt_mult }
                , Dimless{ fields.nt_add }
            );
        }

        mathfp::Expected<timetable::domain::TransferLimits> parse_transfer_limits(
              const Object& search_para
            , const Object& temporal
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  TransferFields
                , fields
                , read_transfer_fields(search_para, temporal)
            );

            return make_transfer_limits(
                  TransferCount{ static_cast<std::int32_t>(fields.max_transfers) }
                , Time{ fields.min_transfer_wait }
                , Time{ fields.max_transfer_wait }
                , true
                , true
            );
        }

        mathfp::Expected<timetable::domain::SearchImpedance> parse_search_impedance(
            const Object& search_imp
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  SearchImpedanceFields
                , fields
                , read_search_impedance_fields(
                    search_imp, schema::kParamsTxtSchema.search_impedance
                )
            );
            return make_search_impedance(
                  Dimless{ fields.in_vehicle_time }
                , Dimless{ fields.access_time }
                , Dimless{ fields.egress_time }
                , Dimless{ fields.transfer_walk_time }
                , Dimless{ fields.transfer_wait_time }
                , Dimless{ fields.transfer_count }
                , Dimless{ fields.fare }
            );
        }

        mathfp::Expected<timetable::domain::PreprocessParams> make_default_preprocess_params() {
            using namespace timetable::domain;

            return make_preprocess_params(
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
            );
        }

        mathfp::Expected<timetable::domain::SplitChoiceModelConfig> parse_split_choice_model_config(
            const Object& split_para
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  StringArray<1>
                , values
                , read_string_array(split_para, schema::kParamsTxtSchema.split_choice_model)
            );
            const auto& choice_model = values[0];
            const auto parsed_model = split_choice_model_from_string(choice_model);
            if (!parsed_model.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported choiceModel")
                    .ctx("choiceModel", choice_model)
                );
            }

            struct ChoiceModelSpec final {
                SplitChoiceModel model{};
                const std::array<NumberFieldSpec, 1>* exponent_spec{};
            };

            constexpr auto specs = std::array{
                  ChoiceModelSpec{ SplitChoiceModel::Kirchhoff, &schema::kParamsTxtSchema.split_choice_model_exponent }
                , ChoiceModelSpec{ SplitChoiceModel::Logit    , &schema::kParamsTxtSchema.split_logit_exponent }
                , ChoiceModelSpec{ SplitChoiceModel::Lohse    , &schema::kParamsTxtSchema.split_lohse_exponent }
                , ChoiceModelSpec{ SplitChoiceModel::BoxCox   , &schema::kParamsTxtSchema.split_boxcox_exponent }
            };

            const auto it = std::find_if(
                  specs.begin()
                , specs.end()
                , [&](const ChoiceModelSpec& spec) {
                    return spec.model == *parsed_model;
                }
            );
            if (it == specs.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported choiceModel")
                    .ctx("choiceModel", choice_model)
                );
            }

            MATHFP_TRY_LET(DoubleArray<1>, exponent_values, read_number_array(split_para, *it->exponent_spec));
            auto boxcox_t = 0.0;
            if (*parsed_model == SplitChoiceModel::BoxCox) {
                MATHFP_TRY_LET(DoubleArray<1>, boxcox_values, read_number_array(
                    split_para, schema::kParamsTxtSchema.split_scalars
                ));
                boxcox_t = boxcox_values[0];
            }

            return SplitChoiceModelConfig{
                  .model    = *parsed_model
                , .exponent = Dimless{ exponent_values[0] }
                , .boxcox_t = Dimless{ boxcox_t }
            };
        }

        mathfp::Expected<timetable::domain::SplitParams> parse_split_params(
              const Object& split_para
            , const Object& indep
            , const Object& split_imp
            , const Object& split_pjt
        ) {
            using namespace timetable::domain;

            MATHFP_TRY_LET(
                  SplitImpedanceFields
                , split_imp_fields
                , read_split_impedance_fields(
                    split_imp, schema::kParamsTxtSchema.split_impedance
                )
            );
            MATHFP_TRY_LET(
                  PerceivedJourneyTimeFields
                , split_pjt_fields
                , read_perceived_journey_time_fields(
                    split_pjt, schema::kParamsTxtSchema.split_perceived_journey_time
                )
            );
            MATHFP_TRY_LET(
                  SplitIndependenceFields
                , indep_fields
                , read_split_independence_fields(
                    indep, schema::kParamsTxtSchema.split_independence
                )
            );
            MATHFP_TRY_LET(
                  SplitChoiceModelConfig
                , choice_model
                , parse_split_choice_model_config(split_para)
            );

            return make_split_params(
                  Dimless{ split_imp_fields.time }
                , Dimless{ 1.0 }
                , Dimless{ split_imp_fields.fare }
                , PerceivedJourneyTimeWeights{
                      .in_vehicle_time    = Dimless{ split_pjt_fields.in_vehicle_time }
                    , .access_time        = Dimless{ split_pjt_fields.access_time }
                    , .egress_time        = Dimless{ split_pjt_fields.egress_time }
                    , .transfer_walk_time = Dimless{ split_pjt_fields.transfer_walk_time }
                    , .transfer_wait_time = Dimless{ split_pjt_fields.transfer_wait_time }
                    , .transfer_count     = Dimless{ split_pjt_fields.transfer_count }
                }
                , TemporalUtilityWeights{
                      .early_departure = Dimless{ split_imp_fields.departure_early }
                    , .late_departure  = Dimless{ split_imp_fields.departure_late }
                }
                , choice_model
                , Dimless{ indep_fields.gamma }
                , Dimless{ indep_fields.temporal_similarity_scale }
                , Dimless{ indep_fields.higher_quality_scale }
                , Dimless{ indep_fields.lower_quality_scale }
            );
        }

        mathfp::Expected<bool> parse_numeric_bool(
              double           value
            , std::string_view field_name
        ) {
            if (value == 0.0) {
                return false;
            }
            if (value == 1.0) {
                return true;
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("expected numeric bool encoded as 0 or 1")
                    .ctx("field", std::string(field_name))
                    .ctx("value", value)
            );
        }

        mathfp::Expected<bool> bool_like_at(
              const Object&    obj
            , std::string_view key
            , std::string_view path
        ) {
            const auto it = obj.find(std::string(key));
            if (it == obj.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing required key")
                        .ctx("path", std::string(path))
                        .ctx("key" , std::string(key))
                );
            }
            if (const auto* value = std::get_if<bool>(&it->second.data)) {
                return *value;
            }
            if (const auto* value = std::get_if<double>(&it->second.data)) {
                return parse_numeric_bool(
                      *value
                    , std::string(path) + "." + std::string(key)
                );
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("expected bool or numeric bool encoded as 0 or 1")
                    .ctx("path", std::string(path))
                    .ctx("key" , std::string(key))
            );
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentExecutionConfig> parse_assignment_execution_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, base_para, object_at(root, "basePara", "root"));
            MATHFP_TRY_LET(bool, calculate_assignment, bool_like_at(
                *base_para, "calcAssign", "root.basePara"
            ));

            AssignmentExecutionConfig config{
                .calculate_assignment = calculate_assignment
            };
            MATHFP_TRY(validate_assignment_execution_config(config));
            return config;
        }

        mathfp::Expected<timetable::domain::assignment::SkimAggregationFunc> parse_skim_func(
            const std::string& token
        ) {
            const auto parsed = timetable::domain::assignment::skim_aggregation_func_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported skim matrix aggregation function")
                        .ctx("func", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<SkimMatrixFields> read_skim_matrix_fields(
              const Object& base_para
            , const Object& skim_para
        ) {
            MATHFP_TRY_LET(bool, enabled, bool_at(base_para, "calcSkimMatr", "root.basePara"));
            MATHFP_TRY_LET(std::string, func, string_at(skim_para, "func", "root.skimMatrixPara"));
            MATHFP_TRY_LET(double, volume_weighted, number_at(
                skim_para, "volumeWeighted", "root.skimMatrixPara"
            ));
            MATHFP_TRY_LET(double, quantile, number_at(
                skim_para, "quantile", "root.skimMatrixPara"
            ));
            MATHFP_TRY_LET(double, low_impedance_connection_share, number_at(
                skim_para, "lowImpConnShare", "root.skimMatrixPara"
            ));

            return SkimMatrixFields{
                  .enabled                         = enabled
                , .func                            = std::move(func)
                , .volume_weighted                 = volume_weighted
                , .quantile                        = quantile
                , .low_impedance_connection_share  = low_impedance_connection_share
            };
        }

        mathfp::Expected<timetable::domain::assignment::SkimMatrixConfig> parse_skim_matrix_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, base_para, object_at(root, "basePara", "root"));
            MATHFP_TRY_LET(const Object*, skim_para, object_at(root, "skimMatrixPara", "root"));
            MATHFP_TRY_LET(SkimMatrixFields, fields, read_skim_matrix_fields(*base_para, *skim_para));
            MATHFP_TRY_LET(SkimAggregationFunc, func, parse_skim_func(fields.func));
            MATHFP_TRY_LET(bool, volume_weighted, parse_numeric_bool(
                  fields.volume_weighted
                , "skimMatrixPara.volumeWeighted"
            ));

            return make_skim_matrix_config(
                  fields.enabled
                , func
                , volume_weighted
                , fields.quantile
                , fields.low_impedance_connection_share
            );
        }

        mathfp::Expected<AssignmentPeriodFields> read_assignment_period_fields(
            const Object& base_para
        ) {
            MATHFP_TRY_LET(double, pre_assign_period, number_at(
                base_para, "preAssignPeriod", "root.basePara"
            ));
            MATHFP_TRY_LET(double, post_assign_period, number_at(
                base_para, "postAssignPeriod", "root.basePara"
            ));

            return AssignmentPeriodFields{
                  .pre_assign_period  = pre_assign_period
                , .post_assign_period = post_assign_period
            };
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentPeriodConfig> parse_assignment_period_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, base_para, object_at(root, "basePara", "root"));
            MATHFP_TRY_LET(
                  AssignmentPeriodFields
                , fields
                , read_assignment_period_fields(*base_para)
            );

            return make_assignment_period_config(
                  fields.pre_assign_period
                , fields.post_assign_period
            );
        }

        mathfp::Expected<ConnectionDeletionFields> read_connection_deletion_fields(
            const Object& choice_para
        ) {
            MATHFP_TRY_LET(bool, delete_outside_assignment_period, bool_like_at(
                choice_para, "deleteConnsOutsideAssignmentPeriod", "root.choicePara"
            ));
            MATHFP_TRY_LET(bool, delete_departures_before_assignment_period_for_departure_based, bool_like_at(
                choice_para, "deleteConnsWithDepBeforeAssPeriodForDepBasedDSeg", "root.choicePara"
            ));
            MATHFP_TRY_LET(bool, delete_arrivals_after_assignment_period_for_arrival_based, bool_like_at(
                choice_para, "deleteConnsWithArrAfterAssPeriodForArrBasedDSeg", "root.choicePara"
            ));

            return ConnectionDeletionFields{
                  .delete_outside_assignment_period =
                      delete_outside_assignment_period
                , .delete_departures_before_assignment_period_for_departure_based =
                      delete_departures_before_assignment_period_for_departure_based
                , .delete_arrivals_after_assignment_period_for_arrival_based =
                      delete_arrivals_after_assignment_period_for_arrival_based
            };
        }

        mathfp::Expected<timetable::domain::assignment::ConnectionDeletionConfig> parse_connection_deletion_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, choice_para, object_at(root, "choicePara", "root"));
            MATHFP_TRY_LET(
                  ConnectionDeletionFields
                , fields
                , read_connection_deletion_fields(*choice_para)
            );

            ConnectionDeletionConfig config{
                  .delete_outside_assignment_period =
                      fields.delete_outside_assignment_period
                , .delete_departures_before_assignment_period_for_departure_based =
                      fields.delete_departures_before_assignment_period_for_departure_based
                , .delete_arrivals_after_assignment_period_for_arrival_based =
                      fields.delete_arrivals_after_assignment_period_for_arrival_based
            };
            MATHFP_TRY(validate_connection_deletion_config(config));
            return config;
        }

        mathfp::Expected<DemandSegmentTimeFields> read_demand_segment_time_fields(
            const Object& split_para
        ) {
            MATHFP_TRY_LET(bool, dep_based_demand_segment, bool_like_at(
                split_para, "depBasedDSeg", "root.splitPara"
            ));
            MATHFP_TRY_LET(bool, consider_connections_with_positive_delta_t, bool_like_at(
                split_para, "considerConnsWithPosDeltaT", "root.splitPara"
            ));

            return DemandSegmentTimeFields{
                  .dep_based_demand_segment =
                      dep_based_demand_segment
                , .consider_connections_with_positive_delta_t =
                      consider_connections_with_positive_delta_t
            };
        }

        mathfp::Expected<timetable::domain::assignment::DemandSegmentTimeConfig> parse_demand_segment_time_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, split_para, object_at(root, "splitPara", "root"));
            MATHFP_TRY_LET(
                  DemandSegmentTimeFields
                , fields
                , read_demand_segment_time_fields(*split_para)
            );

            DemandSegmentTimeConfig config{
                  .basis = fields.dep_based_demand_segment
                      ? DemandSegmentBasis::Departure
                      : DemandSegmentBasis::Arrival
                , .consider_connections_with_positive_delta_t =
                      fields.consider_connections_with_positive_delta_t
            };
            MATHFP_TRY(validate_demand_segment_time_config(config));
            return config;
        }

    }  // namespace

    mathfp::Expected<timetable::domain::SearchParams> map_params(
        const Object& root
    ) {
        using namespace timetable::domain;

        MATHFP_TRY_LET(
              ObjectArray<3>
            , root_objects
            , read_object_array(root, schema::kParamsTxtSchema.root_objects)
        );
        const auto [search_para, choice_para, split_para] = root_objects;

        MATHFP_TRY_LET(
              ObjectArray<3>
            , search_objects
            , read_object_array(*search_para, schema::kParamsTxtSchema.search_objects)
        );
        const auto [search_tol, temporal, search_imp] = search_objects;

        MATHFP_TRY_LET(
              ObjectArray<1>
            , choice_objects
            , read_object_array(*choice_para, schema::kParamsTxtSchema.choice_objects)
        );
        const auto [choice_tol] = choice_objects;

        MATHFP_TRY_LET(
              ObjectArray<2>
            , split_objects
            , read_object_array(*split_para, schema::kParamsTxtSchema.split_objects)
        );
        const auto [indep, split_imp] = split_objects;

        MATHFP_TRY_LET(
              ObjectArray<1>
            , split_imp_objects
            , read_object_array(*split_imp, schema::kParamsTxtSchema.split_imp_objects)
        );
        const auto [split_pjt] = split_imp_objects;

        MATHFP_TRY_LET(
              ParsedToleranceBundle
            , tolerances
            , mathfp::app::lift2(
                [](SearchTolerances search, ChoiceTolerances choice) {
                    return ParsedToleranceBundle{
                          .search = std::move(search)
                        , .choice = std::move(choice)
                    };
                }
                , parse_search_tolerances(*search_tol)
                , parse_choice_tolerances(*choice_tol)
            )
        );

        MATHFP_TRY_LET(
              ParsedSearchBundle
            , search_bundle
            , mathfp::app::lift3(
                [](PreprocessParams preprocess, SearchImpedance impedance, TransferLimits transfers) {
                    return ParsedSearchBundle{
                          .preprocess = std::move(preprocess)
                        , .impedance  = std::move(impedance)
                        , .transfers  = std::move(transfers)
                    };
                }
                , make_default_preprocess_params()
                , parse_search_impedance(*search_imp)
                , parse_transfer_limits(*search_para, *temporal)
            )
        );
        MATHFP_TRY_LET(SplitParams, split, parse_split_params(*split_para, *indep, *split_imp, *split_pjt));

        return make_search_params(
              std::move(search_bundle.preprocess)
            , std::move(search_bundle.impedance)
            , std::move(search_bundle.transfers)
            , std::move(tolerances.search)
            , std::move(tolerances.choice)
            , std::move(split)
        );
    }

    mathfp::Expected<timetable::domain::AssignmentRuntimeParams> map_assignment_runtime_params(
        const Object& root
    ) {
        MATHFP_TRY_LET(timetable::domain::SearchParams, search_params, map_params(root));
        MATHFP_TRY_LET(
              timetable::domain::assignment::AssignmentExecutionConfig
            , execution
            , parse_assignment_execution_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::SkimMatrixConfig
            , skim_matrix
            , parse_skim_matrix_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::AssignmentPeriodConfig
            , assignment_period
            , parse_assignment_period_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::ConnectionDeletionConfig
            , connection_deletion
            , parse_connection_deletion_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::DemandSegmentTimeConfig
            , demand_segment_time
            , parse_demand_segment_time_config(root)
        );

        return timetable::domain::AssignmentRuntimeParams{
              .search              = std::move(search_params)
            , .execution           = std::move(execution)
            , .skim_matrix         = std::move(skim_matrix)
            , .assignment_period   = std::move(assignment_period)
            , .connection_deletion = std::move(connection_deletion)
            , .demand_segment_time = std::move(demand_segment_time)
        };
    }

}  // namespace timetable::infra::params_txt::detail
