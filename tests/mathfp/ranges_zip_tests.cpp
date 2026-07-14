#include <mathfp/ranges/zip.hpp>

#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

    template <class T>
    concept HasConstBegin = requires(const T& value) {
        value.begin();
    };

    struct ConstSummation final {
        int operator()(int lhs, int rhs) const {
            return lhs + rhs;
        }
    };

    struct MutableCounter final {
        int calls{};

        int operator()(int value) {
            ++calls;
            return value;
        }
    };

    using MutableVector = std::vector<int>;
    using MutableZip = decltype(mathfp::ranges::zip(std::declval<MutableVector&>()));
    using MutableZipReference = decltype(*std::declval<MutableZip&>().begin());
    using ConstZipReference = decltype(*std::declval<const MutableZip&>().begin());

    static_assert(std::is_assignable_v<
        decltype(std::get<0>(std::declval<MutableZipReference>())),
        int
    >);

    static_assert(!std::is_assignable_v<
        decltype(std::get<0>(std::declval<ConstZipReference>())),
        int
    >);

    using SourceConstZip = decltype(
        mathfp::ranges::zip(std::declval<const MutableVector&>())
    );
    using SourceConstZipReference = decltype(
        *std::declval<SourceConstZip&>().begin()
    );

    static_assert(!std::is_assignable_v<
        decltype(std::get<0>(std::declval<SourceConstZipReference>())),
        int
    >);

    using MutableOnlyZipWith = decltype(
        mathfp::ranges::zip_with(
              std::declval<MutableCounter>()
            , std::declval<MutableVector&>()
        )
    );

    static_assert(!HasConstBegin<MutableOnlyZipWith>);

}  // namespace

TEST(MathfpRangesZip, MutableZipAllowsElementMutation) {
    std::vector<int> values{ 1, 2, 3 };
    auto zipped = mathfp::ranges::zip(values);

    std::get<0>(*zipped.begin()) = 10;

    EXPECT_EQ(values.front(), 10);
}

TEST(MathfpRangesZip, ConstZipWithUsesConstCallable) {
    std::vector<int> lhs{ 1, 2, 3 };
    std::vector<int> rhs{ 4, 5, 6 };
    const auto zipped = mathfp::ranges::zip_with(ConstSummation{}, lhs, rhs);

    std::vector<int> sums;
    for (const auto value : zipped) {
        sums.push_back(value);
    }

    EXPECT_EQ(sums, (std::vector<int>{ 5, 7, 9 }));
}
