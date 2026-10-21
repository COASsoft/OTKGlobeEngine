#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "otk/formats.hpp"

namespace otk::adapters {

std::string xmlEscape(const std::string& s);
std::string colorHex(std::uint32_t argb);
std::string uniqueId(const Feature& f, size_t index);

std::vector<Feature> parseCot(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings);
std::string writeCotEvent(const Feature& f, size_t index, ExportResult& r);
std::string writeCot(const std::vector<Feature>& fs, ExportResult& r);

std::vector<Feature> parseCsv(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings);
std::string writeCsv(const std::vector<Feature>& fs, ExportResult& r);

bool parseWkt(const std::string& text, std::vector<Geometry>& out);
std::string writeWkt(const std::vector<Geometry>& geometries);
std::vector<Feature> parseWktText(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings);
std::string writeWktText(const std::vector<Feature>& fs, ExportResult& r);

std::vector<Feature> parseGml(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings);
std::string writeGml(const std::vector<Feature>& fs, ExportResult& r);

std::string writeGeoJson(const std::vector<Feature>& fs, ExportResult& r);
std::string writeKml(const std::vector<Feature>& fs, ExportResult& r);
std::string writeGpx(const std::vector<Feature>& fs, ExportResult& r);

ImportResult importPackage(const std::vector<std::uint8_t>& bytes, const ImportLimits& limits, int depth);
std::vector<std::uint8_t> writePackage(const std::vector<Feature>& fs, ExportResult& r);

}
