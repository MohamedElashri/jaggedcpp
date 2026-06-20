#include <jagged/jagged.hpp>

#include <cassert>
#include <optional>
#include <vector>

int main() {
    using MaybeInt = std::optional<int>;

    const auto array = jagged::Array<MaybeInt>::from_rows({
        {MaybeInt{1}, std::nullopt, MaybeInt{3}},
        {},
        {std::nullopt},
        {MaybeInt{4}},
    });

    const auto none_mask = array.is_none();
    assert(none_mask.to_rows() == std::vector<std::vector<bool>>({{false, true, false}, {}, {true}, {false}}));
    assert(none_mask.at(0, 1));
    assert(!none_mask.at(3, 0));

    const auto dropped = array.drop_none();
    assert(dropped.to_rows() == std::vector<std::vector<int>>({{1, 3}, {}, {}, {4}}));

    const auto filled = array.fill_none(9);
    assert(filled.to_rows() == std::vector<std::vector<int>>({{1, 9, 3}, {}, {9}, {4}}));

    const auto padded = array.pad_none(3);
    assert(padded.to_rows() == std::vector<std::vector<MaybeInt>>({
                                   {MaybeInt{1}, std::nullopt, MaybeInt{3}},
                                   {std::nullopt, std::nullopt, std::nullopt},
                                   {std::nullopt, std::nullopt, std::nullopt},
                                   {MaybeInt{4}, std::nullopt, std::nullopt},
                               }));

    const auto unchanged = array.pad_none(1);
    assert(unchanged.to_rows() == std::vector<std::vector<MaybeInt>>({
                                     {MaybeInt{1}, std::nullopt, MaybeInt{3}},
                                     {std::nullopt},
                                     {std::nullopt},
                                     {MaybeInt{4}},
                                 }));

    return 0;
}
