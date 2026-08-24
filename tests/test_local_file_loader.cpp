// test_local_file_loader.cpp
//
// Roadmap Phase 4 (2026-08-21, loadMovie — see docs/known-limitations.md
// L4): LocalFileLoader is the one genuinely OS-facing piece of this
// phase's work (real ifstream I/O against real temp files, not a
// synthetic in-memory fixture like every other test in this suite) — see
// its own doc comment in runtime/LocalFileLoader.h for why it exists.

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "TestFramework.h"
#include "runtime/LocalFileLoader.h"

using flash3ds::runtime::LocalFileLoader;

namespace {

// Writes `bytes` to a fresh, uniquely-named file under the system temp
// directory and returns its full path. Uses std::filesystem purely for
// path plumbing (temp_directory_path(), unique_path()) — the actual write
// is plain ofstream, matching LocalFileLoader's own plain-ifstream read
// side.
std::filesystem::path writeTempFile(const std::string& name, const std::vector<uint8_t>& bytes) {
    std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

}  // namespace

TEST_CASE(LocalFileLoader_AbsolutePath_ReadsFileContentsExactly) {
    std::vector<uint8_t> content = {0x46, 0x57, 0x53, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    auto path = writeTempFile("flash3ds_lfl_test_absolute.bin", content);

    LocalFileLoader loader;
    auto result = loader.loadFile(path.string());
    CHECK(result.has_value());
    CHECK_EQ(*result, content);

    std::filesystem::remove(path);
}

TEST_CASE(LocalFileLoader_MissingFile_ReturnsNullopt) {
    LocalFileLoader loader;
    auto result = loader.loadFile("/this/path/almost-certainly/does/not/exist.swf");
    CHECK(!result.has_value());
}

TEST_CASE(LocalFileLoader_EmptyFile_ReadsAsEmptyNotNullopt) {
    auto path = writeTempFile("flash3ds_lfl_test_empty.bin", {});

    LocalFileLoader loader;
    auto result = loader.loadFile(path.string());
    CHECK(result.has_value());
    CHECK_EQ(result->size(), static_cast<size_t>(0));

    std::filesystem::remove(path);
}

TEST_CASE(LocalFileLoader_BaseDirJoining_RelativeUrlResolvesUnderBaseDir) {
    std::vector<uint8_t> content = {0x01, 0x02, 0x03};
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    auto fullPath = writeTempFile("flash3ds_lfl_test_relative.bin", content);

    LocalFileLoader loader(dir.string());
    auto result = loader.loadFile("flash3ds_lfl_test_relative.bin");
    CHECK(result.has_value());
    CHECK_EQ(*result, content);

    std::filesystem::remove(fullPath);
}

TEST_CASE(LocalFileLoader_AbsoluteUrl_IgnoresBaseDir) {
    std::vector<uint8_t> content = {0x0A, 0x0B};
    auto fullPath = writeTempFile("flash3ds_lfl_test_absolute_ignores_base.bin", content);

    // A base dir that doesn't exist at all — proves the absolute `url`
    // never gets joined onto it.
    LocalFileLoader loader("/this/base/dir/does/not/exist");
    auto result = loader.loadFile(fullPath.string());
    CHECK(result.has_value());
    CHECK_EQ(*result, content);

    std::filesystem::remove(fullPath);
}
