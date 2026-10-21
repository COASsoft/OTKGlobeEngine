#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ote/features.hpp"

namespace ote {

enum class DataKind { Vector, Raster, Elevation, Model, PointCloud, Tiles, Package, Stream };

struct FormatInfo {
    std::string id;
    std::string name;
    std::vector<std::string> extensions;
    DataKind kind = DataKind::Vector;
    bool canImport = false;
    bool canExport = false;
    bool requiresGdal = false;
    bool available = true;
    std::string loadWith;
    std::string notes;
};

struct ImportLimits {
    std::uint64_t maxFileBytes = 1ull << 30;
    std::uint64_t maxArchiveBytes = 2ull << 30;
    std::uint32_t maxArchiveEntries = 20000;
    int maxArchiveDepth = 3;
    std::uint64_t maxFeatures = 5000000;
};

struct ImportResult {
    bool ok = false;
    std::string format;
    ParsedFile data;
    std::vector<std::string> warnings;
    std::string error;
};

struct ExportResult {
    bool ok = false;
    std::string format;
    std::vector<std::string> lossy;
    std::string error;
};

const std::vector<FormatInfo>& formats();
const FormatInfo* findFormat(const std::string& id);
std::string detectFormat(const std::string& path);
std::string detectFormat(const std::vector<std::uint8_t>& bytes, const std::string& nameHint = {});

ImportResult importData(const std::string& path, const std::string& format = {}, const ImportLimits& limits = {});
ImportResult importBytes(const std::vector<std::uint8_t>& bytes, const std::string& format, const ImportLimits& limits = {});

ExportResult exportFeatures(const std::vector<Feature>& features, const std::string& path, const std::string& format = {});
std::vector<std::uint8_t> exportBytes(const std::vector<Feature>& features, const std::string& format, ExportResult* result = nullptr);

std::string formatsMarkdown();

std::string registerIconset(const std::string& zipOrDirectory);
std::shared_ptr<const Image> iconsetImage(const std::string& iconsetPath);
void clearIconsets();

}
