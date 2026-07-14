#include "detail/params_txt.hpp"
#include "detail/params_txt_codecs.hpp"
#include "detail/params_txt_converters.hpp"
#include "detail/params_txt_draft.hpp"
#include "detail/params_txt_reader.hpp"
#include "detail/params_txt_schema.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/validation.hpp"

namespace timetable::infra::params_txt::detail {

    namespace {

        mathfp::Expected<draft::Tolerance> read_tolerance_draft(
              const Object&                                         obj
            , const std::array<schema::NumberFieldSpec<draft::Tolerance>, 6>& field_specs
        ) {
            return reader::read_number_draft<draft::Tolerance>(obj, field_specs);
        }

        mathfp::Expected<draft::TransferLimits> read_transfer_draft(
              const Object& search_para
            , const Object& temporal
        ) {
            MATHFP_TRY_LET(
                  draft::TransferLimits
                , search_fields
                , reader::read_number_draft<draft::TransferLimits>(
                    search_para, schema::kParamsTxtSchema.transfer_limits
                )
            );
            MATHFP_TRY_LET(
                  draft::TransferLimits
                , temporal_fields
                , reader::read_number_draft<draft::TransferLimits>(
                    temporal, schema::kParamsTxtSchema.temporal_suitability
                )
            );

            return draft::TransferLimits{
                  .max_transfers     = search_fields.max_transfers
                , .min_transfer_wait = temporal_fields.min_transfer_wait
                , .max_transfer_wait = temporal_fields.max_transfer_wait
            };
        }

        mathfp::Expected<draft::SearchImpedance> read_search_impedance_draft(
              const Object&                                         obj
            , const std::array<schema::NumberFieldSpec<draft::SearchImpedance>, 8>& field_specs
        ) {
            return reader::read_number_draft<draft::SearchImpedance>(obj, field_specs);
        }

        mathfp::Expected<draft::SplitImpedance> read_split_impedance_draft(
              const Object&                                         obj
            , const std::array<schema::NumberFieldSpec<draft::SplitImpedance>, 4>& field_specs
        ) {
            return reader::read_number_draft<draft::SplitImpedance>(obj, field_specs);
        }

        mathfp::Expected<draft::PerceivedJourneyTime> read_perceived_journey_time_draft(
              const Object&                                         obj
            , const std::array<schema::NumberFieldSpec<draft::PerceivedJourneyTime>, 7>& field_specs
        ) {
            return reader::read_number_draft<draft::PerceivedJourneyTime>(obj, field_specs);
        }

        mathfp::Expected<draft::SplitIndependence> read_split_independence_draft(
              const Object&                                         obj
            , const std::array<schema::NumberFieldSpec<draft::SplitIndependence>, 2>& field_specs
        ) {
            MATHFP_TRY_LET(bool, enabled, codecs::bool_like_at(
                obj, "useIndependence", "root.splitPara.Independence"
            ));
            MATHFP_TRY_LET(
                  draft::SplitIndependence
                , fields
                , reader::read_number_draft<draft::SplitIndependence>(obj, field_specs)
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , higher_quality_scale
                , codecs::optional_number_at(
                      obj
                    , "indepHigherQualityCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , lower_quality_scale
                , codecs::optional_number_at(
                      obj
                    , "indepLowerQualityCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , higher_pjt_scale
                , codecs::optional_number_at(
                      obj
                    , "indepHigherPjtCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , lower_pjt_scale
                , codecs::optional_number_at(
                      obj
                    , "indepLowerPjtCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , higher_fare_scale
                , codecs::optional_number_at(
                      obj
                    , "indepHigherFareCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            MATHFP_TRY_LET(
                  std::optional<double>
                , lower_fare_scale
                , codecs::optional_number_at(
                      obj
                    , "indepLowerFareCoeff"
                    , "root.splitPara.Independence"
                  )
            );
            const auto required_scale = [](
                  std::optional<double> specific
                , std::optional<double> legacy
                , std::string_view      key
                , std::string_view      fallback_key
            ) -> mathfp::Expected<double> {
                if (specific) {
                    return *specific;
                }
                if (legacy) {
                    return *legacy;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing split independence scale")
                        .ctx("key", std::string(key))
                        .ctx("fallback_key", std::string(fallback_key))
                );
            };
            MATHFP_TRY_LET(
                  double
                , higher_pjt
                , required_scale(
                      higher_pjt_scale
                    , higher_quality_scale
                    , "indepHigherPjtCoeff"
                    , "indepHigherQualityCoeff"
                  )
            );
            MATHFP_TRY_LET(
                  double
                , lower_pjt
                , required_scale(
                      lower_pjt_scale
                    , lower_quality_scale
                    , "indepLowerPjtCoeff"
                    , "indepLowerQualityCoeff"
                  )
            );
            MATHFP_TRY_LET(
                  double
                , higher_fare
                , required_scale(
                      higher_fare_scale
                    , higher_quality_scale
                    , "indepHigherFareCoeff"
                    , "indepHigherQualityCoeff"
                  )
            );
            MATHFP_TRY_LET(
                  double
                , lower_fare
                , required_scale(
                      lower_fare_scale
                    , lower_quality_scale
                    , "indepLowerFareCoeff"
                    , "indepLowerQualityCoeff"
                  )
            );
            return draft::SplitIndependence{
                  .enabled                   = enabled
                , .gamma                     = fields.gamma
                , .temporal_similarity_scale = fields.temporal_similarity_scale
                , .higher_quality_scale      = higher_quality_scale.value_or(higher_pjt)
                , .lower_quality_scale       = lower_quality_scale.value_or(lower_pjt)
                , .higher_perceived_journey_time_scale = higher_pjt
                , .lower_perceived_journey_time_scale  = lower_pjt
                , .higher_fare_scale = higher_fare
                , .lower_fare_scale  = lower_fare
            };
        }

        mathfp::Expected<draft::ChoiceModelExponent> read_choice_model_exponent_draft(
              const Object&            split_para
            , draft::SplitChoiceModel  model_fields
        ) {
            const auto it = std::find_if(
                  schema::kParamsTxtSchema.split_choice_model_exponents.begin()
                , schema::kParamsTxtSchema.split_choice_model_exponents.end()
                , [&](const schema::ChoiceModelExponentSpec& spec) {
                    return spec.choice_model_token == model_fields.choice_model;
                }
            );
            if (it == schema::kParamsTxtSchema.split_choice_model_exponents.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported choiceModel")
                        .ctx("choiceModel", model_fields.choice_model)
                );
            }

            return reader::read_number_draft<draft::ChoiceModelExponent>(
                split_para, it->exponent
            );
        }

        mathfp::Expected<draft::SearchParams> read_search_params_draft(
            const Object& root
        ) {
            MATHFP_TRY_LET(
                  draft::SearchParamsObjects
                , root_objects
                , reader::read_object_draft<draft::SearchParamsObjects>(
                    root, schema::kParamsTxtSchema.root_objects
                )
            );
            const auto* search_para = root_objects.search_para;
            const auto* choice_para = root_objects.choice_para;
            const auto* split_para = root_objects.split_para;

            MATHFP_TRY_LET(
                  draft::SearchObjects
                , search_objects
                , reader::read_object_draft<draft::SearchObjects>(
                    *search_para, schema::kParamsTxtSchema.search_objects
                )
            );
            MATHFP_TRY_LET(
                  draft::ChoiceObjects
                , choice_objects
                , reader::read_object_draft<draft::ChoiceObjects>(
                    *choice_para, schema::kParamsTxtSchema.choice_objects
                )
            );
            MATHFP_TRY_LET(
                  draft::SplitObjects
                , split_objects
                , reader::read_object_draft<draft::SplitObjects>(
                    *split_para, schema::kParamsTxtSchema.split_objects
                )
            );
            MATHFP_TRY_LET(
                  draft::SplitImpedanceObjects
                , split_imp_objects
                , reader::read_object_draft<draft::SplitImpedanceObjects>(
                    *split_objects.impedance
                  , schema::kParamsTxtSchema.split_imp_objects
                )
            );

            MATHFP_TRY_LET(
                  draft::SplitChoiceModel
                , split_choice_model
                , reader::read_string_draft<draft::SplitChoiceModel>(
                    *split_para, schema::kParamsTxtSchema.split_choice_model
                )
            );
            MATHFP_TRY_LET(
                  draft::ChoiceModelExponent
                , split_choice_model_exponent
                , read_choice_model_exponent_draft(*split_para, split_choice_model)
            );
            MATHFP_TRY_LET(bool, split_boxcox_transform_enabled, codecs::bool_like_at(
                *split_para, "BoxCoxTransformImp", "root.splitPara"
            ));
            MATHFP_TRY_LET(
                  draft::Tolerance
                , search_tolerances
                , read_tolerance_draft(
                    *search_objects.tolerances
                  , schema::kParamsTxtSchema.search_tolerances
                )
            );
            MATHFP_TRY_LET(
                  draft::Tolerance
                , choice_tolerances
                , read_tolerance_draft(
                    *choice_objects.tolerances
                  , schema::kParamsTxtSchema.choice_tolerances
                )
            );
            MATHFP_TRY_LET(
                  draft::TransferLimits
                , transfers
                , read_transfer_draft(*search_para, *search_objects.temporal)
            );
            MATHFP_TRY_LET(
                  draft::SearchImpedance
                , impedance
                , read_search_impedance_draft(
                    *search_objects.impedance
                  , schema::kParamsTxtSchema.search_impedance
                )
            );
            MATHFP_TRY_LET(
                  draft::SplitImpedance
                , split_impedance
                , read_split_impedance_draft(
                    *split_objects.impedance
                  , schema::kParamsTxtSchema.split_impedance
                )
            );
            MATHFP_TRY_LET(
                  draft::PerceivedJourneyTime
                , split_perceived_journey_time
                , read_perceived_journey_time_draft(
                    *split_imp_objects.perceived_journey_time
                  , schema::kParamsTxtSchema.split_perceived_journey_time
                )
            );
            MATHFP_TRY_LET(
                  draft::SplitIndependence
                , split_independence
                , read_split_independence_draft(
                    *split_objects.independence
                  , schema::kParamsTxtSchema.split_independence
                )
            );
            MATHFP_TRY_LET(
                  draft::SplitScalars
                , split_scalars
                , reader::read_number_draft<draft::SplitScalars>(
                    *split_para, schema::kParamsTxtSchema.split_scalars
                )
            );

            return draft::SearchParams{
                  .search_tolerances = search_tolerances
                , .choice_tolerances = choice_tolerances
                , .transfers = transfers
                , .impedance = impedance
                , .split_impedance = split_impedance
                , .split_perceived_journey_time = split_perceived_journey_time
                , .split_independence = split_independence
                , .split_choice_model = std::move(split_choice_model)
                , .split_choice_model_exponent = split_choice_model_exponent
                , .split_scalars = split_scalars
                , .split_boxcox_transform_enabled = split_boxcox_transform_enabled
            };
        }

        mathfp::Expected<timetable::domain::assignment::SearchExecutionMode>
        parse_search_execution_mode_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_execution_mode_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search execution mode")
                        .ctx("mode", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentCalculationFormulation>
        parse_assignment_calculation_formulation_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::assignment_calculation_formulation_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported assignment calculation formulation")
                        .ctx("formulation", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::SearchOriginScope>
        parse_search_origin_scope_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_origin_scope_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search origin scope")
                        .ctx("originScope", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::SearchTimeDomainSource>
        parse_search_time_domain_source_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_time_domain_source_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search time-domain source")
                        .ctx("timeDomainSource", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::SearchDestinationScope>
        parse_search_destination_scope_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_destination_scope_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search destination scope")
                        .ctx("destinationScope", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::SearchResultProjection>
        parse_search_result_projection_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_result_projection_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search result projection")
                        .ctx("resultProjection", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::SearchPartialRetentionScope>
        parse_search_partial_retention_scope_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::search_partial_retention_scope_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported search partial retention scope")
                        .ctx("partialRetentionScope", token)
                );
            }
            return *parsed;
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentOutputExportProfile>
        parse_assignment_output_export_profile_token(const std::string& token) {
            const auto parsed =
                timetable::domain::assignment::assignment_output_export_profile_from_string(token);
            if (!parsed.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unsupported assignment output export profile")
                        .ctx("outputExportProfile", token)
                );
            }
            return *parsed;
        }

        [[nodiscard]] timetable::domain::assignment::SearchPartialRetentionScope
        default_partial_retention_scope_for_projection(
            timetable::domain::assignment::SearchResultProjection result_projection
        ) noexcept {
            using namespace timetable::domain::assignment;
            return result_projection == SearchResultProjection::CompletionTargets
                || result_projection == SearchResultProjection::OdDayPairs
                ? SearchPartialRetentionScope::TreeGlobal
                : SearchPartialRetentionScope::ProjectionSlotLocal;
        }

        mathfp::Expected<timetable::domain::assignment::SearchExecutionConfig>
        parse_search_execution_config(const Object& root) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(
                  const Object*
                , obj
                , codecs::optional_object_at(root, "searchExecution", "root")
            );
            if (obj == nullptr) {
                return make_default_search_execution_config();
            }

            MATHFP_TRY_LET(
                  std::optional<std::string>
                , formulation_token
                , codecs::optional_string_at(*obj, "formulation", "root.searchExecution")
            );
            MATHFP_TRY_LET(
                  std::optional<std::string>
                , mode_token
                , codecs::optional_string_at(*obj, "mode", "root.searchExecution")
            );
            auto config = make_default_search_execution_config();
            if (formulation_token.has_value()) {
                MATHFP_TRY_LET(
                      AssignmentCalculationFormulation
                    , formulation
                    , parse_assignment_calculation_formulation_token(*formulation_token)
                );
                config = make_search_execution_config(formulation);
            }
            if (mode_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchExecutionMode
                    , parsed_mode
                    , parse_search_execution_mode_token(*mode_token)
                );
                config.mode = parsed_mode;
            }

            MATHFP_TRY_LET(
                  std::optional<std::string>
                , origin_scope_token
                , codecs::optional_string_at(*obj, "originScope", "root.searchExecution")
            );
            if (origin_scope_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchOriginScope
                    , parsed_scope
                    , parse_search_origin_scope_token(*origin_scope_token)
                );
                config.origin_scope = parsed_scope;
            }

            MATHFP_TRY_LET(
                  std::optional<std::string>
                , time_domain_source_token
                , codecs::optional_string_at(*obj, "timeDomainSource", "root.searchExecution")
            );
            if (time_domain_source_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchTimeDomainSource
                    , parsed_source
                    , parse_search_time_domain_source_token(*time_domain_source_token)
                );
                config.time_domain_source = parsed_source;
            }

            MATHFP_TRY_LET(
                  std::optional<std::string>
                , destination_scope_token
                , codecs::optional_string_at(*obj, "destinationScope", "root.searchExecution")
            );
            if (destination_scope_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchDestinationScope
                    , parsed_scope
                    , parse_search_destination_scope_token(*destination_scope_token)
                );
                config.destination_scope = parsed_scope;
            }

            MATHFP_TRY_LET(
                  std::optional<std::string>
                , result_projection_token
                , codecs::optional_string_at(*obj, "resultProjection", "root.searchExecution")
            );
            if (result_projection_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchResultProjection
                    , parsed_projection
                    , parse_search_result_projection_token(*result_projection_token)
                );
                config.result_projection = parsed_projection;
                config.partial_retention_scope =
                    default_partial_retention_scope_for_projection(parsed_projection);
                if (!formulation_token.has_value()) {
                    switch (parsed_projection) {
                        case SearchResultProjection::DemandTasks:
                            config.formulation =
                                AssignmentCalculationFormulation::TimedConnectionDiagnostics;
                            config.diagnostic_mode = true;
                            break;

                        case SearchResultProjection::OdDayPairs:
                            config.formulation =
                                AssignmentCalculationFormulation::OdDayAssignment;
                            config.diagnostic_mode = false;
                            break;

                        case SearchResultProjection::CompletionTargets:
                            config.formulation =
                                AssignmentCalculationFormulation::AllZoneSearch;
                            config.diagnostic_mode = true;
                            break;
                    }
                }
            }
            MATHFP_TRY_LET(
                  std::optional<std::string>
                , partial_retention_scope_token
                , codecs::optional_string_at(
                      *obj
                    , "partialRetentionScope"
                    , "root.searchExecution"
                  )
            );
            if (partial_retention_scope_token.has_value()) {
                MATHFP_TRY_LET(
                      SearchPartialRetentionScope
                    , parsed_scope
                    , parse_search_partial_retention_scope_token(
                          *partial_retention_scope_token
                      )
                );
                config.partial_retention_scope = parsed_scope;
            }

            MATHFP_TRY_LET(
                  std::optional<std::size_t>
                , max_parallel_batches
                , codecs::optional_positive_size_at(
                      *obj
                    , "maxParallelBatches"
                    , "root.searchExecution"
                  )
            );
            if (max_parallel_batches.has_value()) {
                config.max_parallel_batches = *max_parallel_batches;
            }

            MATHFP_TRY_LET(
                  std::optional<std::size_t>
                , max_parallel_memory_mb
                , codecs::optional_positive_size_at(
                      *obj
                    , "maxParallelMemoryMb"
                    , "root.searchExecution"
                  )
            );
            if (max_parallel_memory_mb.has_value()) {
                config.max_parallel_memory_mb = *max_parallel_memory_mb;
            }

            MATHFP_TRY_LET(
                  std::optional<std::size_t>
                , estimated_memory_mb_per_parallel_batch
                , codecs::optional_positive_size_at(
                      *obj
                    , "estimatedMemoryMbPerParallelBatch"
                    , "root.searchExecution"
                  )
            );
            if (estimated_memory_mb_per_parallel_batch.has_value()) {
                config.estimated_memory_mb_per_parallel_batch =
                    *estimated_memory_mb_per_parallel_batch;
            }

            MATHFP_TRY_LET(
                  std::optional<bool>
                , validate_phase_invariants
                , codecs::optional_bool_like_at(
                      *obj
                    , "validatePhaseInvariants"
                    , "root.searchExecution"
                  )
            );
            if (validate_phase_invariants.has_value()) {
                config.validate_phase_invariants = *validate_phase_invariants;
            }

            MATHFP_TRY_LET(
                  std::optional<bool>
                , log_projection_details
                , codecs::optional_bool_like_at(
                      *obj
                    , "logProjectionDetails"
                    , "root.searchExecution"
                  )
            );
            if (log_projection_details.has_value()) {
                config.log_projection_details = *log_projection_details;
            }

            return config;
        }

        mathfp::Expected<timetable::domain::assignment::AssignmentExecutionConfig> parse_assignment_execution_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, base_para, object_at(root, "basePara", "root"));
            MATHFP_TRY_LET(bool, calculate_assignment, codecs::bool_like_at(
                *base_para, "calcAssign", "root.basePara"
            ));

            AssignmentExecutionConfig config{
                .calculate_assignment = calculate_assignment
            };
            MATHFP_TRY_LET(
                  std::optional<std::string>
                , output_export_profile_token
                , codecs::optional_string_at(
                      *base_para
                    , "outputExportProfile"
                    , "root.basePara"
                  )
            );
            if (output_export_profile_token.has_value()) {
                MATHFP_TRY_LET(
                      AssignmentOutputExportProfile
                    , profile
                    , parse_assignment_output_export_profile_token(
                          *output_export_profile_token
                      )
                );
                config.output_export_profile = profile;
            }
            MATHFP_TRY(validate_assignment_execution_config(config));
            return config;
        }

        mathfp::Expected<timetable::domain::assignment::SearchPruningConfig> parse_search_pruning_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, search_para, object_at(root, "searchPara", "root"));
            MATHFP_TRY_LET(bool, allow_equivalent_dominance, codecs::bool_like_at(
                *search_para, "allowDominanceForEquivalentConnections", "root.searchPara"
            ));
            MATHFP_TRY_LET(bool, use_last_stop_for_equivalent_connections, codecs::bool_like_at(
                *search_para, "useLastStopForEquivalentConnections", "root.searchPara"
            ));

            const auto equivalent_stop_reference =
                use_last_stop_for_equivalent_connections
                    ? EquivalentConnectionStopReference::LastTimedStopOccurrence
                    : EquivalentConnectionStopReference::CurrentStopOccurrence;

            return SearchPruningConfig{
                  .model = SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                    , .equivalent_connection_dominance =
                          EquivalentConnectionDominanceConfig{
                              .allow_dominance_for_equivalent_connections =
                                  allow_equivalent_dominance
                            , .stop_reference = equivalent_stop_reference
                          }
                  }
                , .runtime = SearchPruningRuntimeConfig{}
            };
        }

        mathfp::Expected<timetable::domain::assignment::CompleteConnectionDominanceConfig>
        parse_complete_connection_dominance_config(
            const Object& root
        ) {
            using namespace timetable::domain::assignment;

            MATHFP_TRY_LET(const Object*, search_para, object_at(root, "searchPara", "root"));
            MATHFP_TRY_LET(bool, deactivate_direct_dominance, codecs::bool_like_at(
                *search_para, "deactivateDominanceOfDirectConnections", "root.searchPara"
            ));

            CompleteConnectionDominanceConfig config{
                .deactivate_dominance_of_direct_connections =
                    deactivate_direct_dominance
            };
            MATHFP_TRY(validate_complete_connection_dominance_config(config));
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

        mathfp::Expected<draft::SkimMatrix> read_skim_matrix_fields(
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

            return draft::SkimMatrix{
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
            MATHFP_TRY_LET(draft::SkimMatrix, fields, read_skim_matrix_fields(*base_para, *skim_para));
            MATHFP_TRY_LET(SkimAggregationFunc, func, parse_skim_func(fields.func));
            MATHFP_TRY_LET(bool, volume_weighted, codecs::numeric_bool(
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

        mathfp::Expected<draft::AssignmentPeriod> read_assignment_period_fields(
            const Object& base_para
        ) {
            MATHFP_TRY_LET(double, pre_assign_period, number_at(
                base_para, "preAssignPeriod", "root.basePara"
            ));
            MATHFP_TRY_LET(double, post_assign_period, number_at(
                base_para, "postAssignPeriod", "root.basePara"
            ));

            return draft::AssignmentPeriod{
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
                  draft::AssignmentPeriod
                , fields
                , read_assignment_period_fields(*base_para)
            );

            return make_assignment_period_config(
                  fields.pre_assign_period
                , fields.post_assign_period
            );
        }

        mathfp::Expected<draft::ConnectionDeletion> read_connection_deletion_fields(
            const Object& choice_para
        ) {
            MATHFP_TRY_LET(bool, delete_outside_assignment_period, codecs::bool_like_at(
                choice_para, "deleteConnsOutsideAssignmentPeriod", "root.choicePara"
            ));
            MATHFP_TRY_LET(bool, delete_departures_before_assignment_period_for_departure_based, codecs::bool_like_at(
                choice_para, "deleteConnsWithDepBeforeAssPeriodForDepBasedDSeg", "root.choicePara"
            ));
            MATHFP_TRY_LET(bool, delete_arrivals_after_assignment_period_for_arrival_based, codecs::bool_like_at(
                choice_para, "deleteConnsWithArrAfterAssPeriodForArrBasedDSeg", "root.choicePara"
            ));

            return draft::ConnectionDeletion{
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
                  draft::ConnectionDeletion
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

        mathfp::Expected<draft::DemandSegmentTime> read_demand_segment_time_fields(
            const Object& split_para
        ) {
            MATHFP_TRY_LET(bool, dep_based_demand_segment, codecs::bool_like_at(
                split_para, "depBasedDSeg", "root.splitPara"
            ));
            MATHFP_TRY_LET(bool, consider_connections_with_positive_delta_t, codecs::bool_like_at(
                split_para, "considerConnsWithPosDeltaT", "root.splitPara"
            ));

            return draft::DemandSegmentTime{
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
                  draft::DemandSegmentTime
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
        MATHFP_TRY_LET(
              draft::SearchParams
            , fields
            , read_search_params_draft(root)
        );
        return converters::to_search_params(std::move(fields));
    }

    mathfp::Expected<timetable::domain::AssignmentRuntimeParams> map_assignment_runtime_params(
        const Object& root
    ) {
        MATHFP_TRY_LET(timetable::domain::SearchParams, search_params, map_params(root));
        MATHFP_TRY_LET(
            timetable::domain::assignment::CapacityAwareAssignmentConfig
            , capacity_aware_assignment
            , converters::capacity_aware_assignment_config_from_params(search_params)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::AssignmentExecutionConfig
            , execution
            , parse_assignment_execution_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::SearchPruningConfig
            , search_pruning
            , parse_search_pruning_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::CompleteConnectionDominanceConfig
            , complete_connection_dominance
            , parse_complete_connection_dominance_config(root)
        );
        MATHFP_TRY_LET(
              timetable::domain::assignment::SearchExecutionConfig
            , search_execution
            , parse_search_execution_config(root)
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
            , .complete_connection_dominance = complete_connection_dominance
            , .search_execution    = search_execution
            , .search_pruning      = std::move(search_pruning)
            , .skim_matrix         = std::move(skim_matrix)
            , .assignment_period   = std::move(assignment_period)
            , .connection_deletion = std::move(connection_deletion)
            , .demand_segment_time = std::move(demand_segment_time)
            , .capacity_aware_assignment = std::move(capacity_aware_assignment)
        };
    }

}  // namespace timetable::infra::params_txt::detail
