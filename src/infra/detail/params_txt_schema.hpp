#pragma once

#include <array>
#include <string>
#include <string_view>

#include "params_txt_draft.hpp"

namespace timetable::infra::params_txt::detail::schema {

    template <class Draft, class Value>
    struct FieldSpec final {
        std::string_view key{};
        std::string_view path{};
        std::string_view field{};
        Value Draft::*   member{};
    };

    template <class Draft>
    using ObjectFieldSpec = FieldSpec<Draft, const Object*>;

    template <class Draft>
    using NumberFieldSpec = FieldSpec<Draft, double>;

    template <class Draft>
    using StringFieldSpec = FieldSpec<Draft, std::string>;

    struct ChoiceModelExponentSpec final {
        std::string_view choice_model_token{};
        std::array<NumberFieldSpec<draft::ChoiceModelExponent>, 1> exponent{};
    };

    struct ParamsTxtSchema final {
        std::array<ObjectFieldSpec<draft::SearchParamsObjects>, 3> root_objects{
            ObjectFieldSpec<draft::SearchParamsObjects>{
                  "searchPara"
                , "root"
                , "root.search_para"
                , &draft::SearchParamsObjects::search_para
            },
            ObjectFieldSpec<draft::SearchParamsObjects>{
                  "choicePara"
                , "root"
                , "root.choice_para"
                , &draft::SearchParamsObjects::choice_para
            },
            ObjectFieldSpec<draft::SearchParamsObjects>{
                  "splitPara"
                , "root"
                , "root.split_para"
                , &draft::SearchParamsObjects::split_para
            }
        };

        std::array<ObjectFieldSpec<draft::SearchObjects>, 3> search_objects{
            ObjectFieldSpec<draft::SearchObjects>{
                  "ToleranceConstraints"
                , "root.searchPara"
                , "search.tolerances"
                , &draft::SearchObjects::tolerances
            },
            ObjectFieldSpec<draft::SearchObjects>{
                  "TemporalSuitability"
                , "root.searchPara"
                , "search.temporal"
                , &draft::SearchObjects::temporal
            },
            ObjectFieldSpec<draft::SearchObjects>{
                  "SearchImp"
                , "root.searchPara"
                , "search.impedance"
                , &draft::SearchObjects::impedance
            }
        };

        std::array<ObjectFieldSpec<draft::ChoiceObjects>, 1> choice_objects{
            ObjectFieldSpec<draft::ChoiceObjects>{
                  "ToleranceConstraints"
                , "root.choicePara"
                , "choice.tolerances"
                , &draft::ChoiceObjects::tolerances
            }
        };

        std::array<ObjectFieldSpec<draft::SplitObjects>, 2> split_objects{
            ObjectFieldSpec<draft::SplitObjects>{
                  "Independence"
                , "root.splitPara"
                , "split.independence"
                , &draft::SplitObjects::independence
            },
            ObjectFieldSpec<draft::SplitObjects>{
                  "SplitImp"
                , "root.splitPara"
                , "split.impedance"
                , &draft::SplitObjects::impedance
            }
        };

        std::array<ObjectFieldSpec<draft::SplitImpedanceObjects>, 1> split_imp_objects{
            ObjectFieldSpec<draft::SplitImpedanceObjects>{
                  "PerceivedJourneyTime"
                , "root.splitPara.SplitImp"
                , "split.perceived_journey_time"
                , &draft::SplitImpedanceObjects::perceived_journey_time
            }
        };

        std::array<NumberFieldSpec<draft::Tolerance>, 6> search_tolerances{
            NumberFieldSpec<draft::Tolerance>{ "minSearchImpFactor"      , "root.searchPara.ToleranceConstraints", "search_tolerances.imp_mult", &draft::Tolerance::imp_mult },
            NumberFieldSpec<draft::Tolerance>{ "minSearchImpAbs"         , "root.searchPara.ToleranceConstraints", "search_tolerances.imp_add" , &draft::Tolerance::imp_add  },
            NumberFieldSpec<draft::Tolerance>{ "minJourneyTimeFactor"    , "root.searchPara.ToleranceConstraints", "search_tolerances.jt_mult" , &draft::Tolerance::jt_mult  },
            NumberFieldSpec<draft::Tolerance>{ "minJourneyTimeAbs"       , "root.searchPara.ToleranceConstraints", "search_tolerances.jt_add"  , &draft::Tolerance::jt_add   },
            NumberFieldSpec<draft::Tolerance>{ "minNumberTransfersFactor", "root.searchPara.ToleranceConstraints", "search_tolerances.nt_mult" , &draft::Tolerance::nt_mult  },
            NumberFieldSpec<draft::Tolerance>{ "minNumberTransfersAbs"   , "root.searchPara.ToleranceConstraints", "search_tolerances.nt_add"  , &draft::Tolerance::nt_add   }
        };

        std::array<NumberFieldSpec<draft::Tolerance>, 6> choice_tolerances{
            NumberFieldSpec<draft::Tolerance>{ "minSearchImpFactor"      , "root.choicePara.ToleranceConstraints", "choice_tolerances.imp_mult", &draft::Tolerance::imp_mult },
            NumberFieldSpec<draft::Tolerance>{ "minSearchImpAbs"         , "root.choicePara.ToleranceConstraints", "choice_tolerances.imp_add" , &draft::Tolerance::imp_add  },
            NumberFieldSpec<draft::Tolerance>{ "minJourneyTimeFactor"    , "root.choicePara.ToleranceConstraints", "choice_tolerances.jt_mult" , &draft::Tolerance::jt_mult  },
            NumberFieldSpec<draft::Tolerance>{ "minJourneyTimeAbs"       , "root.choicePara.ToleranceConstraints", "choice_tolerances.jt_add"  , &draft::Tolerance::jt_add   },
            NumberFieldSpec<draft::Tolerance>{ "minNumberTransfersFactor", "root.choicePara.ToleranceConstraints", "choice_tolerances.nt_mult" , &draft::Tolerance::nt_mult  },
            NumberFieldSpec<draft::Tolerance>{ "minNumberTransfersAbs"   , "root.choicePara.ToleranceConstraints", "choice_tolerances.nt_add"  , &draft::Tolerance::nt_add   }
        };

        std::array<NumberFieldSpec<draft::TransferLimits>, 1> transfer_limits{
            NumberFieldSpec<draft::TransferLimits>{
                  "maxNumTransfers"
                , "root.searchPara"
                , "transfer_limits.max_transfers"
                , &draft::TransferLimits::max_transfers
            }
        };

        std::array<NumberFieldSpec<draft::TransferLimits>, 2> temporal_suitability{
            NumberFieldSpec<draft::TransferLimits>{ "minTWT", "root.searchPara.TemporalSuitability", "transfer_limits.min_transfer_wait", &draft::TransferLimits::min_transfer_wait },
            NumberFieldSpec<draft::TransferLimits>{ "maxTWT", "root.searchPara.TemporalSuitability", "transfer_limits.max_transfer_wait", &draft::TransferLimits::max_transfer_wait }
        };

        std::array<NumberFieldSpec<draft::SearchImpedance>, 8> search_impedance{
            NumberFieldSpec<draft::SearchImpedance>{ "inVehTimeFactor"      , "root.searchPara.SearchImp", "search_impedance.in_vehicle_time"      , &draft::SearchImpedance::in_vehicle_time       },
            NumberFieldSpec<draft::SearchImpedance>{ "accessTimeFactor"     , "root.searchPara.SearchImp", "search_impedance.access_time"          , &draft::SearchImpedance::access_time           },
            NumberFieldSpec<draft::SearchImpedance>{ "egressTimeFactor"     , "root.searchPara.SearchImp", "search_impedance.egress_time"          , &draft::SearchImpedance::egress_time           },
            NumberFieldSpec<draft::SearchImpedance>{ "walkTimeFactor"       , "root.searchPara.SearchImp", "search_impedance.transfer_walk_time"   , &draft::SearchImpedance::transfer_walk_time    },
            NumberFieldSpec<draft::SearchImpedance>{ "transferWaitTimeFactor", "root.searchPara.SearchImp", "search_impedance.transfer_wait_time"   , &draft::SearchImpedance::transfer_wait_time    },
            NumberFieldSpec<draft::SearchImpedance>{ "numTransfersFactor"   , "root.searchPara.SearchImp", "search_impedance.transfer_count"       , &draft::SearchImpedance::transfer_count        },
            NumberFieldSpec<draft::SearchImpedance>{ "supplementsFactor"    , "root.searchPara.SearchImp", "search_impedance.fare"                 , &draft::SearchImpedance::fare                  },
            NumberFieldSpec<draft::SearchImpedance>{ "volCapRatioFactor"    , "root.searchPara.SearchImp", "search_impedance.volume_capacity_ratio", &draft::SearchImpedance::volume_capacity_ratio }
        };

        std::array<StringFieldSpec<draft::SplitChoiceModel>, 1> split_choice_model{
            StringFieldSpec<draft::SplitChoiceModel>{
                  "choiceModel"
                , "root.splitPara"
                , "split.choice_model"
                , &draft::SplitChoiceModel::choice_model
            }
        };

        std::array<NumberFieldSpec<draft::SplitImpedance>, 4> split_impedance{
            NumberFieldSpec<draft::SplitImpedance>{ "perceivedJourneyTimeFactor" , "root.splitPara.SplitImp", "split_impedance.q_time"            , &draft::SplitImpedance::time            },
            NumberFieldSpec<draft::SplitImpedance>{ "temporalUtilityFactor_early", "root.splitPara.SplitImp", "split_impedance.q_departure_early", &draft::SplitImpedance::departure_early },
            NumberFieldSpec<draft::SplitImpedance>{ "temporalUtilityFactor_late" , "root.splitPara.SplitImp", "split_impedance.q_departure_late" , &draft::SplitImpedance::departure_late  },
            NumberFieldSpec<draft::SplitImpedance>{ "fareFactor"                 , "root.splitPara.SplitImp", "split_impedance.q_fare"           , &draft::SplitImpedance::fare            }
        };

        std::array<NumberFieldSpec<draft::PerceivedJourneyTime>, 7> split_perceived_journey_time{
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "inVehTimeFactor"      , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.in_vehicle_time"      , &draft::PerceivedJourneyTime::in_vehicle_time       },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "accessTimeFactor"     , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.access_time"          , &draft::PerceivedJourneyTime::access_time           },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "egressTimeFactor"     , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.egress_time"          , &draft::PerceivedJourneyTime::egress_time           },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "walkTimeFactor"       , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.transfer_walk_time"   , &draft::PerceivedJourneyTime::transfer_walk_time    },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "transferWaitTimeFactor", "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.transfer_wait_time"   , &draft::PerceivedJourneyTime::transfer_wait_time    },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "numTransfersFactor"   , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.transfer_count"       , &draft::PerceivedJourneyTime::transfer_count        },
            NumberFieldSpec<draft::PerceivedJourneyTime>{ "volCapRatioFactor"    , "root.splitPara.SplitImp.PerceivedJourneyTime", "split_perceived_journey_time.volume_capacity_ratio", &draft::PerceivedJourneyTime::volume_capacity_ratio }
        };

        std::array<NumberFieldSpec<draft::SplitIndependence>, 2> split_independence{
            NumberFieldSpec<draft::SplitIndependence>{ "gamma"        , "root.splitPara.Independence", "split_independence.gamma"                    , &draft::SplitIndependence::gamma                     },
            NumberFieldSpec<draft::SplitIndependence>{ "indepMaxDelta", "root.splitPara.Independence", "split_independence.temporal_similarity_scale", &draft::SplitIndependence::temporal_similarity_scale }
        };

        std::array<NumberFieldSpec<draft::SplitScalars>, 1> split_scalars{
            NumberFieldSpec<draft::SplitScalars>{
                  "BoxCoxPara"
                , "root.splitPara"
                , "split.impedance_transform.boxcox_t"
                , &draft::SplitScalars::boxcox_t
            }
        };

        std::array<ChoiceModelExponentSpec, 4> split_choice_model_exponents{
            ChoiceModelExponentSpec{
                  "Kirchhoff"
                , std::array{
                      NumberFieldSpec<draft::ChoiceModelExponent>{
                            "KirchhoffExp"
                          , "root.splitPara"
                          , "split.choice_model.exponent"
                          , &draft::ChoiceModelExponent::exponent
                      }
                  }
            },
            ChoiceModelExponentSpec{
                  "Logit"
                , std::array{
                      NumberFieldSpec<draft::ChoiceModelExponent>{
                            "logitExp"
                          , "root.splitPara"
                          , "split.choice_model.exponent"
                          , &draft::ChoiceModelExponent::exponent
                      }
                  }
            },
            ChoiceModelExponentSpec{
                  "Lohse"
                , std::array{
                      NumberFieldSpec<draft::ChoiceModelExponent>{
                            "LohseExp"
                          , "root.splitPara"
                          , "split.choice_model.exponent"
                          , &draft::ChoiceModelExponent::exponent
                      }
                  }
            },
            ChoiceModelExponentSpec{
                  "BoxCox"
                , std::array{
                      NumberFieldSpec<draft::ChoiceModelExponent>{
                            "BoxCoxExp"
                          , "root.splitPara"
                          , "split.choice_model.exponent"
                          , &draft::ChoiceModelExponent::exponent
                      }
                  }
            }
        };
    };

    inline constexpr ParamsTxtSchema kParamsTxtSchema{};

}  // namespace timetable::infra::params_txt::detail::schema
