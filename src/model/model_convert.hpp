#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ote {

using AssetReader = std::function<std::vector<std::uint8_t>(const std::string&)>;

std::vector<std::uint8_t> objToGlb(const std::string& text, const AssetReader& read);
std::vector<std::uint8_t> colladaToGlb(const std::string& text, const AssetReader& read);
std::vector<std::uint8_t> modelToGlb(const std::string& name, const std::vector<std::uint8_t>& bytes, const AssetReader& read);

}
