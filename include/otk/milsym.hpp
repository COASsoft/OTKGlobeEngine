#pragma once

#include "otk/features.hpp"

#include <memory>
#include <string>

namespace otk {

enum class Affiliation { Pending, Unknown, AssumedFriend, Friend, Neutral, Suspect, Hostile, Joker, Faker, NoAffiliation };
enum class Dimension { Space, Air, Ground, SeaSurface, Subsurface, Sof, Other };

struct SymbolCode {
    Affiliation affiliation = Affiliation::Unknown;
    Dimension dimension = Dimension::Ground;
    bool planned = false;
    bool exercise = false;
    std::string function;
    char modifier = '-';
    char echelon = '-';
    bool valid = false;
};

SymbolCode parseSidc(const std::string& sidc);
std::string sidcFromCotType(const std::string& cotType);
std::string cotTypeFromSidc(const std::string& sidc);
Image renderSymbolImage(const std::string& sidc, int sizePx = 96);
std::shared_ptr<const Image> symbolIcon(const std::string& sidc, int sizePx = 96);
void applySymbol(Feature& feature);

}
