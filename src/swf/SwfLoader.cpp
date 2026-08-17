#include "swf/SwfLoader.h"

#include <zlib.h>

#include <cstdio>
#include <fstream>

#include "platform/Log.h"
#include "swf/SwfReader.h"
#include "swf/TagDispatcher.h"

namespace flash3ds::swf {

namespace {

// Defensive cap on decompressed CWS size, to avoid unbounded memory growth
// on a malicious/corrupt "zip-bomb" style SWF. 128 MiB is generous for any
// real SWF6-8 asset.
constexpr size_t kMaxDecompressedSize = 128u * 1024 * 1024;

// Inflates `compressed` (raw zlib stream, as used by CWS-signature SWFs)
// into `out`. Returns false on any zlib error; `out` may contain partial
// data in that case (caller should treat it as failure regardless).
bool inflateZlib(const uint8_t* compressed, size_t compressedSize,
                  std::vector<uint8_t>& out) {
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) {
        return false;
    }

    strm.next_in = const_cast<Bytef*>(compressed);
    strm.avail_in = static_cast<uInt>(compressedSize);

    constexpr size_t kChunk = 64 * 1024;
    std::vector<uint8_t> chunk(kChunk);

    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        strm.next_out = chunk.data();
        strm.avail_out = static_cast<uInt>(chunk.size());

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return false;
        }

        size_t produced = kChunk - strm.avail_out;
        if (produced > 0) {
            if (out.size() + produced > kMaxDecompressedSize) {
                LOG_ERROR("SWF", "CWS decompressed size exceeds safety cap (%zu bytes)",
                           kMaxDecompressedSize);
                inflateEnd(&strm);
                return false;
            }
            out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
        }

        if (ret == Z_BUF_ERROR && strm.avail_in == 0) {
            // No more input, no more output produced: stream is truncated.
            break;
        }
        if (ret == Z_STREAM_END) {
            break;
        }
    }

    inflateEnd(&strm);
    return true;  // Even a truncated-but-partial inflate is handed back;
                  // the header parser downstream will fail gracefully if
                  // the recovered bytes aren't enough.
}

void fillError(runtime::Movie& movie, std::string message) {
    movie.valid = false;
    movie.errorMessage = std::move(message);
    LOG_ERROR("SWF", "%s", movie.errorMessage.c_str());
}

}  // namespace

std::unique_ptr<runtime::Movie> SwfLoader::loadSwf(const uint8_t* data, size_t size) {
    auto movie = std::make_unique<runtime::Movie>();

    if (data == nullptr || size < 8) {
        fillError(*movie, "SWF too small to contain a valid header (need >= 8 bytes)");
        return movie;
    }

    char sig[3] = {static_cast<char>(data[0]), static_cast<char>(data[1]),
                    static_cast<char>(data[2])};
    uint8_t version = data[3];
    uint32_t declaredLength =
        static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
        (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);

    runtime::SwfCompression compression;
    if (sig[0] == 'F' && sig[1] == 'W' && sig[2] == 'S') {
        compression = runtime::SwfCompression::kNone;
    } else if (sig[0] == 'C' && sig[1] == 'W' && sig[2] == 'S') {
        compression = runtime::SwfCompression::kZlib;
    } else if (sig[0] == 'Z' && sig[1] == 'W' && sig[2] == 'S') {
        compression = runtime::SwfCompression::kLzma;
    } else {
        fillError(*movie, "Invalid SWF signature (expected 'FWS', 'CWS', or 'ZWS')");
        return movie;
    }

    movie->version = version;
    movie->compression = compression;
    movie->declaredFileLength = declaredLength;

    LOG_INFO("SWF", "Signature=%.3s Version=%u DeclaredLength=%u", sig, version, declaredLength);

    if (compression == runtime::SwfCompression::kLzma) {
        fillError(*movie, "ZWS (LZMA-compressed) SWF is recognized but not supported in Phase 1");
        return movie;
    }

    // Everything after the 8-byte header (RECT, frame rate, frame count,
    // tags) is optionally zlib-compressed.
    std::vector<uint8_t> restStorage;
    const uint8_t* restData = nullptr;
    size_t restSize = 0;

    if (compression == runtime::SwfCompression::kZlib) {
        if (!inflateZlib(data + 8, size - 8, restStorage)) {
            fillError(*movie, "Failed to zlib-decompress CWS body");
            return movie;
        }
        restData = restStorage.data();
        restSize = restStorage.size();
        LOG_INFO("SWF", "Decompressed CWS body: %zu bytes", restSize);
    } else {
        restData = data + 8;
        restSize = size - 8;
    }

    SwfReader reader(restData, restSize);

    movie->frameSize = reader.readRect();
    movie->frameRateFixed8 = reader.readFixed8();
    movie->frameCount = reader.readU16();

    if (reader.failed()) {
        fillError(*movie, "SWF header truncated while reading FrameSize/FrameRate/FrameCount");
        return movie;
    }

    LOG_INFO("SWF", "FrameSize=%.1fx%.1f px FrameRate=%.2f fps FrameCount=%u",
             movie->frameSize.widthPixels(), movie->frameSize.heightPixels(),
             movie->frameRateFps(), movie->frameCount);

    // Generic tag scan (Phase 1: names + offsets only, no body parsing).
    while (!reader.atEnd() && !reader.failed()) {
        TagRecord tag;
        if (!TagDispatcher::readTagHeader(reader, tag)) {
            break;
        }

        if (std::string(tag.name) == "Unknown") {
            LOG_WARN("SWF", "Unknown tag id=%u offset=%zu length=%u", tag.code, tag.bodyOffset,
                      tag.bodyLength);
        } else {
            LOG_DEBUG("SWF", "Tag: %s (id=%u offset=%zu length=%u)", tag.name.c_str(), tag.code,
                       tag.bodyOffset, tag.bodyLength);
        }

        if (TagDispatcher::isActionScriptTag(tag.code)) {
            movie->hasActionScript = true;
        }

        movie->tags.push_back(tag);

        if (tag.code == 0 /* End */) {
            break;
        }

        reader.skip(tag.bodyLength);
        if (reader.failed()) {
            LOG_WARN("SWF", "Tag stream truncated after tag '%s' (offset=%zu) — stopping scan",
                      tag.name.c_str(), tag.bodyOffset);
            break;
        }
    }

    movie->valid = true;
    LOG_INFO("SWF", "Loaded movie: %zu tags, ActionScript=%s", movie->tags.size(),
             movie->hasActionScript ? "yes" : "no");
    return movie;
}

std::unique_ptr<runtime::Movie> SwfLoader::loadSwfFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        auto movie = std::make_unique<runtime::Movie>();
        fillError(*movie, "Could not open file: " + filename);
        return movie;
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        auto movie = std::make_unique<runtime::Movie>();
        fillError(*movie, "Could not determine file size: " + filename);
        return movie;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        auto movie = std::make_unique<runtime::Movie>();
        fillError(*movie, "Failed to read file: " + filename);
        return movie;
    }

    LOG_INFO("SWF", "Loading movie from '%s' (%zu bytes)", filename.c_str(), buffer.size());
    return loadSwf(buffer.data(), buffer.size());
}

}  // namespace flash3ds::swf
