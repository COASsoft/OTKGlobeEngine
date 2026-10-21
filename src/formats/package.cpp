#include "formats/adapters.hpp"

#include <miniz.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>

namespace otk::adapters {

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string extensionOf(const std::string& name) {
    size_t dot = name.find_last_of('.');
    size_t slash = name.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return lowerCopy(name.substr(dot));
}

bool safeEntryName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) return false;
    return name.find("..") == std::string::npos;
}

std::string randomToken() {
    std::random_device rd;
    std::uniform_int_distribution<unsigned long long> d;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", d(rd));
    return buf;
}

}

ImportResult importPackage(const std::vector<std::uint8_t>& bytes, const ImportLimits& limits, int depth) {
    ImportResult res;
    res.format = "tak-package";
    if (depth > limits.maxArchiveDepth) {
        res.error = "archive nesting deeper than " + std::to_string(limits.maxArchiveDepth);
        return res;
    }
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        res.error = "not a zip archive";
        return res;
    }
    mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count > limits.maxArchiveEntries) {
        mz_zip_reader_end(&zip);
        res.error = "archive has " + std::to_string(count) + " entries, over the limit";
        return res;
    }
    std::uint64_t total = 0;
    std::vector<std::string> names(count);
    int manifest = -1;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        total += st.m_uncomp_size;
        names[i] = st.m_filename;
        std::string l = lowerCopy(names[i]);
        if (l == "manifest/manifest.xml" || l == "manifest.xml") manifest = static_cast<int>(i);
    }
    if (total > limits.maxArchiveBytes) {
        mz_zip_reader_end(&zip);
        res.error = "archive expands to " + std::to_string(total) + " bytes, over the limit";
        return res;
    }
    auto extract = [&](mz_uint index) {
        std::vector<std::uint8_t> out;
        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);
        if (p) {
            out.assign(static_cast<std::uint8_t*>(p), static_cast<std::uint8_t*>(p) + size);
            mz_free(p);
        }
        return out;
    };
    std::vector<mz_uint> selected;
    std::string packageName;
    if (manifest >= 0) {
        std::vector<std::uint8_t> m = extract(static_cast<mz_uint>(manifest));
        pugi::xml_document doc;
        if (doc.load_buffer(m.data(), m.size())) {
            pugi::xml_node root = doc.document_element();
            for (auto p : root.child("Configuration").children("Parameter"))
                if (std::string(p.attribute("name").as_string()) == "name") packageName = p.attribute("value").as_string();
            for (auto c : root.child("Contents").children("Content")) {
                if (c.attribute("ignore").as_bool(false)) continue;
                std::string entry = c.attribute("zipEntry").as_string();
                for (mz_uint i = 0; i < count; ++i)
                    if (names[i] == entry) selected.push_back(i);
            }
        } else {
            res.warnings.push_back("manifest is not well-formed; importing every entry");
        }
    }
    if (selected.empty())
        for (mz_uint i = 0; i < count; ++i)
            if (static_cast<int>(i) != manifest) selected.push_back(i);
    std::filesystem::path temp;
    for (mz_uint i : selected) {
        const std::string& name = names[i];
        if (!safeEntryName(name)) {
            res.warnings.push_back("skipped unsafe entry name " + name);
            continue;
        }
        std::string ext = extensionOf(name);
        if (ext.empty() || mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        if (ext == ".shp") {
            if (temp.empty()) {
                temp = std::filesystem::temp_directory_path() / ("otk_pkg_" + randomToken());
                std::filesystem::create_directories(temp);
            }
            std::string stem = name.substr(0, name.size() - 4);
            for (const char* side : {".shp", ".dbf", ".prj", ".shx"})
                for (mz_uint k = 0; k < count; ++k)
                    if (lowerCopy(names[k]) == lowerCopy(stem + side)) {
                        std::vector<std::uint8_t> b = extract(k);
                        std::ofstream(temp / (std::string("layer") + std::to_string(i) + side), std::ios::binary)
                            .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
                    }
            std::u8string shp = (temp / ("layer" + std::to_string(i) + ".shp")).u8string();
            ImportResult sub = importData(std::string(shp.begin(), shp.end()), "shp", limits);
            res.data.features.insert(res.data.features.end(), sub.data.features.begin(), sub.data.features.end());
            continue;
        }
        std::vector<std::uint8_t> b = extract(i);
        std::string format = detectFormat(b, name);
        const FormatInfo* info = findFormat(format);
        if (!info || !info->canImport || info->kind != DataKind::Vector) {
            if (!format.empty() && info && info->kind != DataKind::Vector) res.warnings.push_back(name + " is " + info->name + "; load it as a layer");
            continue;
        }
        ImportResult sub = format == "tak-package" ? importPackage(b, limits, depth + 1) : importBytes(b, format, limits);
        for (Feature& f : sub.data.features) {
            if (!packageName.empty() && !findAttribute(f, "package")) setAttribute(f, "package", packageName);
            res.data.features.push_back(std::move(f));
        }
        res.data.overlays.insert(res.data.overlays.end(), sub.data.overlays.begin(), sub.data.overlays.end());
        for (auto& w : sub.warnings) res.warnings.push_back(name + ": " + w);
        if (res.data.features.size() > limits.maxFeatures) {
            res.warnings.push_back("feature limit reached");
            break;
        }
    }
    mz_zip_reader_end(&zip);
    if (!temp.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
    }
    res.ok = true;
    return res;
}

std::vector<std::uint8_t> writePackage(const std::vector<Feature>& fs, ExportResult& r) {
    mz_zip_archive zip{};
    std::vector<std::uint8_t> out;
    if (!mz_zip_writer_init_heap(&zip, 0, 1 << 16)) {
        r.error = "zip writer failed";
        return out;
    }
    std::string packageUid = "OTK-PKG-" + randomToken();
    std::string manifest = "<MissionPackageManifest version=\"2\"><Configuration><Parameter name=\"uid\" value=\"" + packageUid +
                           "\"/><Parameter name=\"name\" value=\"OTKGlobeEngine export\"/></Configuration><Contents>";
    ExportResult inner;
    for (size_t i = 0; i < fs.size(); ++i) {
        std::string event = writeCotEvent(fs[i], i, inner);
        if (event.empty()) continue;
        std::string uid = uniqueId(fs[i], i);
        std::string safe;
        for (char c : uid) safe += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' ? c : '_';
        std::string entry = safe + "/" + safe + ".cot";
        std::string doc = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n" + event + "\n";
        mz_zip_writer_add_mem(&zip, entry.c_str(), doc.data(), doc.size(), MZ_DEFAULT_COMPRESSION);
        manifest += "<Content ignore=\"false\" zipEntry=\"" + xmlEscape(entry) + "\"><Parameter name=\"uid\" value=\"" + xmlEscape(uid) + "\"/></Content>";
    }
    manifest += "</Contents></MissionPackageManifest>\n";
    mz_zip_writer_add_mem(&zip, "MANIFEST/manifest.xml", manifest.data(), manifest.size(), MZ_DEFAULT_COMPRESSION);
    void* buf = nullptr;
    size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&zip, &buf, &size) && buf) {
        out.assign(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + size);
        mz_free(buf);
    }
    mz_zip_writer_end(&zip);
    for (auto& l : inner.lossy)
        if (std::find(r.lossy.begin(), r.lossy.end(), l) == r.lossy.end()) r.lossy.push_back(l);
    return out;
}

}
