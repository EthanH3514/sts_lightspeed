// Standalone regression (no simulator, JSON or Python dependencies):
// g++ -std=c++17 -Iinclude tests/card_colors.cpp -o card_colors
// ./card_colors
#include <cstdio>
#include <iterator>
#include "constants/Cards.h"

using namespace sts;

int main() {
    struct Expected {
        CardId id;
        CardColor color;
    };
    // Check both affected ranges plus adjacent entries to catch an index shift.
    // Expected colors match the original game's card constructor CardColor values.
    constexpr Expected cases[] = {
        {CardId::BOWLING_BASH, CardColor::PURPLE},
        {CardId::BRILLIANCE, CardColor::PURPLE},
        {CardId::BRUTALITY, CardColor::RED},
        {CardId::BUFFER, CardColor::BLUE},
        {CardId::BULLET_TIME, CardColor::GREEN},
        {CardId::BULLSEYE, CardColor::BLUE},
        {CardId::COLD_SNAP, CardColor::BLUE},
        {CardId::COLLECT, CardColor::PURPLE},
        {CardId::COMBUST, CardColor::RED},
        {CardId::COMPILE_DRIVER, CardColor::BLUE},
        {CardId::CONCENTRATE, CardColor::GREEN},
        {CardId::CONCLUDE, CardColor::PURPLE},
    };
    static_assert(std::size(cardColors) == std::size(cardEnumStrings),
                  "card color table must align with card IDs");
    int failures = 0;
    for (const auto &test : cases) {
        const auto actual = getCardColor(test.id);
        if (actual != test.color) {
            std::fprintf(stderr, "%s: expected %s, got %s\n",
                         cardEnumStrings[static_cast<int>(test.id)],
                         cardColorStrings[static_cast<int>(test.color)],
                         cardColorStrings[static_cast<int>(actual)]);
            ++failures;
        }
    }
    if (failures != 0) {
        std::fprintf(stderr, "CARD_COLORS_FAILED (%d mismatches)\n", failures);
        return 1;
    }
    std::puts("CARD_COLORS_OK (8 corrected cards, 4 adjacent controls)");
    return 0;
}
