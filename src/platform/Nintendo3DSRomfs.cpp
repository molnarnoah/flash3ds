// Nintendo3DSRomfs.cpp
//
// See Nintendo3DSRomfs.h for the full design rationale (why this exists
// instead of libctru's own romfsInit()).

#include "platform/Nintendo3DSRomfs.h"

#include <cstring>

#include "platform/Log.h"

namespace flash3ds::platform {

namespace {

// Mirrors the public, documented 3DSX file format -- cross-checked against
// BOTH this toolchain's own reader (libctru's romfs_dev.c, `_3DSX_Header`)
// and its own writer (3dstools' 3dsxtool.cpp, the `fout.WriteWord`/
// `WriteHword` sequence in `ElfConvert::Convert()`/`WriteExtHeader()`) --
// not derived from Shift-DX/gameswf in any way. All fields are u16/u32 at
// naturally-4-byte-aligned offsets (verified field-by-field against both
// sources above), so this struct's layout matches the on-disk format
// exactly with no compiler-inserted padding on any platform this project
// targets.
struct ThreeDsxHeader {
    uint32_t magic;
    uint16_t headerSize;
    uint16_t relocHdrSize;
    uint32_t formatVer;
    uint32_t flags;
    uint32_t codeSegSize, rodataSegSize, dataSegSize, bssSize;
    // Extended header -- only meaningful if headerSize indicates it was
    // actually written (see open()'s own check below). This project's
    // build always requests BOTH --smdh= and --romfs= together (see
    // CMakeLists.txt) specifically so fsOffset is always valid whenever
    // headerSize says the extended header is present at all.
    uint32_t smdhOffset, smdhSize;
    uint32_t fsOffset;
};

constexpr uint32_t k3dsxMagic = 0x58534433;  // '3DSX', little-endian on-disk
constexpr uint32_t kRomfsNone = 0xFFFFFFFFu;  // romfs_dir/romfs_file "no sibling/child" marker

}  // namespace

Nintendo3DSRomfs::~Nintendo3DSRomfs() {
    if (open_) {
        FSFILE_Close(fd_);
    }
}

bool Nintendo3DSRomfs::open(const char* argv0) {
    // Logged UNCONDITIONALLY, before any check below can bail out --
    // discovered while diagnosing a "loads then silently quits" report
    // (2026-08-19): every LOG_ERROR call after an early return was
    // previously invisible on target (see Log.cpp's svcOutputDebugString
    // addition, same date), so a failure here gave zero clue which of
    // these checks actually failed. Logging the raw inputs first means the
    // very next test run's Citra/Azahar Log Viewer output pinpoints it
    // exactly instead of requiring another guess-and-rebuild cycle.
    LOG_INFO("VC", "Nintendo3DSRomfs::open: envIsHomebrew=%d argv0=%s", envIsHomebrew() ? 1 : 0,
              argv0 ? argv0 : "(null)");

    if (!envIsHomebrew()) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: not a homebrew (3DSX) launch -- no embedded "
                        "RomFS section to read (see docs/3ds-toolchain.md)");
        return false;
    }
    if (!argv0) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: argv[0] is null -- can't locate this .3dsx's "
                        "own file on the SD card");
        return false;
    }

    // Mirrors romfsMountSelf()'s own two supported schemes exactly (see
    // libctru's romfs_dev.c): a plain SD-card launch (hbmenu, or an
    // emulator's "Load File") hands argv[0] as "sdmc:/path/to/app.3dsx";
    // launching via the `3dslink` dev-workflow tool (also usable against
    // Citra/Azahar, which both implement the same TCP handshake) instead
    // hands "3dslink:/path/to/app.3dsx", which resolves against the SD
    // card's "/3ds" directory by convention, NOT literally -- both are
    // legitimate, commonly-hit launch paths, not just the first one.
    std::string path = argv0;
    const std::string kSdmcPrefix = "sdmc:/";
    const std::string k3dslinkPrefix = "3dslink:/";
    if (path.rfind(kSdmcPrefix, 0) == 0) {
        path = path.substr(kSdmcPrefix.size() - 1);  // keep the leading '/'
    } else if (path.rfind(k3dslinkPrefix, 0) == 0) {
        path = "/3ds" + path.substr(k3dslinkPrefix.size() - 1);
    } else {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: argv[0] ('%s') isn't an sdmc:/ or 3dslink:/ "
                        "path -- this launch method isn't supported by this reader",
                  path.c_str());
        return false;
    }

    uint16_t utf16Path[512];
    ssize_t units = utf8_to_utf16(utf16Path, reinterpret_cast<const uint8_t*>(path.c_str()), 511);
    if (units < 0) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: failed to convert '%s' to UTF-16", path.c_str());
        return false;
    }
    utf16Path[units] = 0;

    FS_Path archPath = {PATH_EMPTY, 1, ""};
    FS_Path filePath = {PATH_UTF16, static_cast<uint32_t>((units + 1) * 2), utf16Path};

    Handle fd = 0;
    Result rc = FSUSER_OpenFileDirectly(&fd, ARCHIVE_SDMC, archPath, filePath, FS_OPEN_READ, 0);
    if (R_FAILED(rc)) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: FSUSER_OpenFileDirectly('%s') failed "
                        "(result=0x%08lx)",
                  path.c_str(), static_cast<unsigned long>(rc));
        return false;
    }

    ThreeDsxHeader header{};
    uint32_t bytesRead = 0;
    rc = FSFILE_Read(fd, &bytesRead, 0, &header, sizeof(header));
    if (R_FAILED(rc) || bytesRead != sizeof(header) || header.magic != k3dsxMagic) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: '%s' is not a readable/valid .3dsx file",
                  path.c_str());
        FSFILE_Close(fd);
        return false;
    }
    if (header.headerSize < sizeof(header)) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: '%s' has no embedded RomFS section -- it was "
                        "built without --romfs=... (see CMakeLists.txt's FLASH3DS_BUILD_3DS block)",
                  path.c_str());
        FSFILE_Close(fd);
        return false;
    }

    fd_ = fd;
    sectionOffset_ = header.fsOffset;
    open_ = true;

    if (!readAt(0, &header_, sizeof(header_)) || header_.headerSize < sizeof(header_)) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: RomFS section header in '%s' is malformed",
                  path.c_str());
        FSFILE_Close(fd_);
        open_ = false;
        return false;
    }

    dirTable_.resize(header_.dirTableSize);
    if (!dirTable_.empty() && !readAt(header_.dirTableOff, dirTable_.data(), header_.dirTableSize)) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: failed to read RomFS directory table");
        FSFILE_Close(fd_);
        open_ = false;
        return false;
    }

    fileTable_.resize(header_.fileTableSize);
    if (!fileTable_.empty() &&
        !readAt(header_.fileTableOff, fileTable_.data(), header_.fileTableSize)) {
        LOG_ERROR("VC", "Nintendo3DSRomfs::open: failed to read RomFS file table");
        FSFILE_Close(fd_);
        open_ = false;
        return false;
    }

    LOG_INFO("VC", "Nintendo3DSRomfs: opened embedded RomFS (dirTable=%lu bytes, fileTable=%lu "
                   "bytes)",
             static_cast<unsigned long>(header_.dirTableSize),
             static_cast<unsigned long>(header_.fileTableSize));
    return true;
}

bool Nintendo3DSRomfs::readAt(uint64_t offsetInSection, void* buffer, uint32_t size) const {
    uint32_t got = 0;
    Result rc = FSFILE_Read(fd_, &got, sectionOffset_ + offsetInSection, buffer, size);
    return R_SUCCEEDED(rc) && got == size;
}

bool Nintendo3DSRomfs::readFile(const std::string& name, std::vector<uint8_t>& outBytes) const {
    if (!open_ || dirTable_.empty() || fileTable_.empty()) return false;

    uint16_t targetUtf16[512];
    ssize_t targetUnits =
        utf8_to_utf16(targetUtf16, reinterpret_cast<const uint8_t*>(name.c_str()), 511);
    if (targetUnits < 0) return false;
    uint32_t targetNameLenBytes = static_cast<uint32_t>(targetUnits) * 2;

    // Root directory is always at offset 0 of the directory table -- see
    // <3ds/romfs.h>'s romfs_dir layout / romfs_dev.c's own romFS_root().
    const auto* root = reinterpret_cast<const romfs_dir*>(dirTable_.data());
    uint32_t fileOff = root->childFile;

    while (fileOff != kRomfsNone) {
        if (static_cast<uint64_t>(fileOff) + sizeof(romfs_file) > fileTable_.size()) {
            break;  // corrupt/unexpected table layout -- bail out rather than read out of bounds
        }
        const auto* file = reinterpret_cast<const romfs_file*>(fileTable_.data() + fileOff);

        if (file->nameLen == targetNameLenBytes &&
            std::memcmp(file->name, targetUtf16, file->nameLen) == 0) {
            outBytes.resize(static_cast<size_t>(file->dataSize));
            if (file->dataSize == 0) return true;
            return readAt(header_.fileDataOff + file->dataOff, outBytes.data(),
                           static_cast<uint32_t>(file->dataSize));
        }

        fileOff = file->sibling;
    }

    return false;
}

}  // namespace flash3ds::platform
