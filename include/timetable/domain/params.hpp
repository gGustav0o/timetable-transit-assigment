#pragma once

#include "timetable/domain/params/search_params.hpp"

namespace timetable::domain {

    /*
     * Compatibility umbrella for the parameter domain model.
     *
     * The target model is:
     *   external format -> infra draft/schema/codec -> params/make.hpp -> domain values
     *
     * This header intentionally exposes value types only. Canonical
     * constructors and invariant checks live in timetable/domain/params/make.hpp.
     */

}  // namespace timetable::domain
