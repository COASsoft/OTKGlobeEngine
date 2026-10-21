#include "otk/formats.hpp"
#include "otk/image.hpp"

#include <miniz.h>
#include <pugixml.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace otk {

namespace {

struct Iconset {
    std::string name;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::map<std::string, std::shared_ptr<const Image>> decoded;
};

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::map<std::string, Iconset>& registry() {
    static std::map<std::string, Iconset> r;
    return r;
}

std::string normalized(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

bool unsafe(const std::string& p) {
    if (p.empty() || p.find(':') != std::string::npos) return true;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t e = p.find('/', pos);
        if (e == std::string::npos) e = p.size();
        if (p.compare(pos, e - pos, "..") == 0 && e - pos == 2) return true;
        pos = e + 1;
    }
    return false;
}

const std::vector<std::uint8_t>* lookup(const Iconset& set, const std::string& group, const std::string& file) {
    for (const std::string& key : {group + "/" + file, file}) {
        auto it = set.files.find(key);
        if (it != set.files.end()) return &it->second;
    }
    for (const auto& [path, bytes] : set.files)
        if (path.size() > file.size() && path.compare(path.size() - file.size(), file.size(), file) == 0 && path[path.size() - file.size() - 1] == '/')
            return &bytes;
    return nullptr;
}

}

std::string registerIconset(const std::string& zipOrDirectory) {
    Iconset set;
    std::filesystem::path root = std::filesystem::u8path(zipOrDirectory);
    std::error_code ec;
    if (std::filesystem::is_directory(root, ec)) {
        size_t count = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || ++count > 20000) break;
            if (!it->is_regular_file(ec) || it->file_size(ec) > (std::uintmax_t(16) << 20)) continue;
            std::ifstream in(it->path(), std::ios::binary);
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::u8string rel = std::filesystem::relative(it->path(), root, ec).generic_u8string();
            set.files[normalized(std::string(rel.begin(), rel.end()))] = std::move(bytes);
        }
    } else {
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, zipOrDirectory.c_str(), 0)) return {};
        mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n && i < 20000; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory || st.m_uncomp_size > (std::uint64_t(16) << 20)) continue;
            std::string path = normalized(st.m_filename);
            if (unsafe(path)) continue;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) continue;
            const auto* b = static_cast<const std::uint8_t*>(data);
            set.files[path].assign(b, b + size);
            mz_free(data);
        }
        mz_zip_reader_end(&zip);
    }
    const std::vector<std::uint8_t>* xml = nullptr;
    for (const auto& [path, bytes] : set.files) {
        std::string tail = path.size() >= 11 ? path.substr(path.size() - 11) : path;
        if (tail == "iconset.xml") {
            xml = &bytes;
            break;
        }
    }
    if (!xml) return {};
    pugi::xml_document doc;
    if (!doc.load_buffer(xml->data(), xml->size())) return {};
    pugi::xml_node node = doc.child("iconset");
    std::string uid = node.attribute("uid").as_string();
    if (uid.empty()) return {};
    set.name = node.attribute("name").as_string();
    std::lock_guard lock(registryMutex());
    registry()[uid] = std::move(set);
    return uid;
}

std::shared_ptr<const Image> iconsetImage(const std::string& iconsetPath) {
    std::string path = normalized(iconsetPath);
    size_t a = path.find('/');
    if (a == std::string::npos) return nullptr;
    size_t b = path.rfind('/');
    std::string uid = path.substr(0, a);
    std::string file = path.substr(b + 1);
    std::string group = b > a ? path.substr(a + 1, b - a - 1) : std::string();
    std::lock_guard lock(registryMutex());
    auto it = registry().find(uid);
    if (it == registry().end()) return nullptr;
    Iconset& set = it->second;
    std::string key = group + "/" + file;
    auto cached = set.decoded.find(key);
    if (cached != set.decoded.end()) return cached->second;
    std::shared_ptr<const Image> image;
    if (const std::vector<std::uint8_t>* bytes = lookup(set, group, file)) {
        auto img = std::make_shared<Image>();
        if (decodeImage(bytes->data(), bytes->size(), *img)) image = img;
    }
    set.decoded[key] = image;
    return image;
}

void clearIconsets() {
    std::lock_guard lock(registryMutex());
    registry().clear();
}

}
