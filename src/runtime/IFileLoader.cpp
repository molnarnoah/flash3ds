#include "runtime/IFileLoader.h"

#include "platform/Log.h"

namespace flash3ds::runtime {

std::optional<std::vector<uint8_t>> NullFileLoader::loadFile(const std::string& url) {
    LOG_WARN("FILELOADER", "loadFile('%s'): no IFileLoader wired up — see IFileLoader.h",
              url.c_str());
    return std::nullopt;
}

}  // namespace flash3ds::runtime
