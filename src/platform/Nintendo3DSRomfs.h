// Nintendo3DSRomfs.h
//
// Minimal, self-contained reader for THIS project's own embedded RomFS
// section (the flat romfs/ directory 3dsxtool's --romfs= flag packs into
// flash3ds_3ds.3dsx -- see CMakeLists.txt's FLASH3DS_BUILD_3DS block and
// docs/virtual-console.md).
//
// Deliberately NOT libctru's own romfsInit()/romfsMountSelf(): those
// (romfs_dev.c) need the sys/iosupport.h device-table framework this
// from-source toolchain build excludes for the same reason archive_dev.c/
// console.c are excluded (see docs/3ds-toolchain.md's Step 4 writeup) --
// so calling romfsInit() would fail to LINK, not just misbehave at
// runtime. This class instead reimplements only the narrow subset of
// romfsMountSelf()'s own logic that does NOT need iosupport: locate this
// running .3dsx's own file on the SD card (from argv[0], exactly the value
// romfsMountSelf() itself reads from __system_argv[0]), open it directly
// via FSUSER_OpenFileDirectly (an ordinary FS-service call, unaffected by
// the exclusion), read its 3DSX header to find the embedded RomFS
// section's byte offset, then read that section's own header/directory-
// table/file-table (the public, documented RomFS format -- reusing
// <3ds/romfs.h>'s own romfs_header/romfs_dir/romfs_file structs, which are
// ordinary header-only type definitions unaffected by which .c files got
// excluded) directly via FSFILE_Read -- entirely without installing any
// devoptab/newlib virtual-filesystem device. Every offset/struct-layout
// decision here is cross-checked against libctru's own (excluded, but
// still present in this toolchain checkout as source) romfs_dev.c and
// 3dstools' own 3dsxtool.cpp -- both public, documented parts of the
// homebrew ecosystem, not derived from Shift-DX/gameswf in any way.
//
// Deliberately scoped to a FLAT root directory only (no subdirectory
// support) -- exactly matches this project's own romfs/ layout
// (`romfs/game.swf`, `romfs/config.ini`, nothing nested). readFile() walks
// the root directory's file-sibling chain directly (a simple linear scan
// -- there are only ever a handful of files) rather than reimplementing
// romfs_dev.c's hash-table lookup; simpler, and just as correct for this
// specific layout.
//
// This file is only compiled for the 3DS target (guarded by __3DS__).

#pragma once

#ifndef __3DS__
#error "Nintendo3DSRomfs.h is only valid in a 3DS cross-compile (__3DS__ not defined)"
#endif

#include <3ds.h>
#include <3ds/romfs.h>

#include <cstdint>
#include <string>
#include <vector>

namespace flash3ds::platform {

class Nintendo3DSRomfs {
public:
    Nintendo3DSRomfs() = default;
    ~Nintendo3DSRomfs();

    Nintendo3DSRomfs(const Nintendo3DSRomfs&) = delete;
    Nintendo3DSRomfs& operator=(const Nintendo3DSRomfs&) = delete;

    // Numbered failure reasons for open() below, in the exact order those
    // checks run -- kept in the header (not just as LOG_ERROR text)
    // because a platform's own diagnostic tooling isn't always reachable
    // from wherever it's actually being tested (e.g. iOS emulators expose
    // no LOG_ERROR/svcOutputDebugString viewer at all -- see
    // nintendo3ds_main.cpp's showFatalErrorScreen(), which draws this
    // count as on-screen squares for exactly that reason).
    enum class OpenFailure {
        kNone = 0,
        // NOT emitted anymore as of 2026-08-19 -- envIsHomebrew()==false
        // now takes the ARCHIVE_ROMFS fallback branch instead of failing
        // outright (see open()'s .cpp comment; confirmed necessary on
        // Manic EMU, an iOS 3DS emulator that doesn't implement the
        // homebrew argv/service-handle-override ABI). Value kept
        // reserved/unused rather than renumbered so any earlier on-screen
        // square-count report stays meaningful.
        kNotHomebrew = 1,
        kNullArgv0 = 2,
        kUnrecognizedArgv0Scheme = 3,  // argv0 isn't "sdmc:/" or "3dslink:/"
        kUtf16ConversionFailed = 4,
        kFileOpenFailed = 5,    // FSUSER_OpenFileDirectly failed
        kInvalid3dsxHeader = 6, // bad magic / short read
        kNoRomfsSection = 7,    // headerSize says no extended header
        kRomfsHeaderMalformed = 8,
        kDirTableReadFailed = 9,
        kFileTableReadFailed = 10,
    };

    // Opens this running .3dsx's own embedded RomFS section (see class
    // comment). `argv0` is main()'s own argv[0] -- the same value
    // romfsMountSelf() itself would read from __system_argv[0], no need to
    // reference that libctru internal directly. Returns false (with a
    // LOG_ERROR'd reason, AND -- if `outFailure` is non-null -- a specific
    // OpenFailure code) if: this isn't a homebrew (3DSX) launch, the
    // .3dsx couldn't be reopened from the SD card, its 3DSX header is
    // malformed, or it has no embedded RomFS section at all (e.g. built
    // without --romfs=... -- see CMakeLists.txt).
    bool open(const char* argv0, OpenFailure* outFailure = nullptr);

    // Reads a ROOT-level file's full contents into `outBytes`. Returns
    // false (outBytes left unchanged) if open() wasn't called/didn't
    // succeed, or `name` doesn't exist at RomFS root. `name` is matched
    // exactly (case-sensitive); no subdirectory paths supported (see class
    // comment).
    bool readFile(const std::string& name, std::vector<uint8_t>& outBytes) const;

private:
    bool readAt(uint64_t offsetInSection, void* buffer, uint32_t size) const;

    Handle fd_ = 0;
    bool open_ = false;
    uint32_t sectionOffset_ = 0;  // byte offset of the RomFS section within fd_
    romfs_header header_{};
    std::vector<uint8_t> dirTable_;
    std::vector<uint8_t> fileTable_;
};

}  // namespace flash3ds::platform
