#include "runtime/LocalFileLoader.h"

#include <fstream>

#include "platform/Log.h"

namespace flash3ds::runtime {

LocalFileLoader::LocalFileLoader(std::string baseDir) : baseDir_(std::move(baseDir)) {}

std::optional<std::vector<uint8_t>> LocalFileLoader::loadFile(const std::string& url) {
    std::string path;
    if (!url.empty() && url.front() == '/') {
        path = url;
    } else if (baseDir_.empty()) {
        path = url;
    } else {
        path = baseDir_;
        if (path.back() != '/') path += '/';
        path += url;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOG_WARN("FILELOADER", "LocalFileLoader: failed to open '%s' (resolved from '%s')",
                  path.c_str(), url.c_str());
        return std::nullopt;
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        LOG_WARN("FILELOADER", "LocalFileLoader: could not determine size of '%s'", path.c_str());
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        LOG_WARN("FILELOADER", "LocalFileLoader: short read on '%s'", path.c_str());
        return std::nullopt;
    }
    return bytes;
}

}  // namespace flash3ds::runtime
