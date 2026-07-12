#pragma once

#include <cstdint>

#include <mathfp/types/strong_type.hpp>

namespace timetable::domain {

    template <typename Tag, typename Raw>
    using OrderedDomainValue = mathfp::StrongType<
          Raw
        , Tag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
        , mathfp::strong_detail::Hashable
    >;

    template <typename Tag>
    using DomainId = OrderedDomainValue<Tag, std::int64_t>;

}  // namespace timetable::domain
