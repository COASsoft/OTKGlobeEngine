#pragma once

#include "ote/features.hpp"

#include <cstddef>
#include <cstdint>

namespace ote {

bool decodeImage(const std::uint8_t* data, std::size_t size, Image& out);
bool imageSize(const std::uint8_t* data, std::size_t size, int& width, int& height);
bool isWebp(const std::uint8_t* data, std::size_t size);
bool webpSupported();

}
