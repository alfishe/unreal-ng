#pragma once

/// @file zifigit.h
/// @brief What the ZiFi S3 firmware's Wild Commander updater needs from git and the GitHub API: SHA-1, the git
/// blob hash ("blob <size>\0" + data), hex digests and the two JSON answers it reads (a branch ref and a recursive
/// tree). Ported from ZiFi-ESP32-S3-Zero 2e5ba83
/// (https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/git_sha1.cpp,
/// https://github.com/andrewinsidelazarev/ZiFi-ESP32-S3-Zero/blob/main/src/github_tree.cpp): the same strict JSON
/// cursor (an answer it does not understand is refused whole), the same path checks.

#include <cstddef>
#include <cstdint>
#include <string>

class ZiFiStateWriter;
class ZiFiStateReader;

namespace zifigit
{

class Sha1
{
public:
    static constexpr size_t kDigestSize = 20;
    Sha1() { Reset(); }
    void Reset();
    void Update(const uint8_t* data, size_t length);
    void Finish(uint8_t digest[kDigestSize]);
    /// TTD: the running state
    void Save(ZiFiStateWriter& w) const;
    void Load(ZiFiStateReader& r);

private:
    void Block(const uint8_t* data);
    uint32_t _state[5] = {};
    uint64_t _length = 0;
    uint8_t _buffer[64] = {};
    size_t _used = 0;
};

/// "blob <size>\0": a git object's header before its bytes
void GitBlobBegin(Sha1& sha, uint32_t size);
bool ParseSha1Hex(const char* text, uint8_t digest[Sha1::kDigestSize]);
std::string FormatSha1Hex(const uint8_t digest[Sha1::kDigestSize]);

struct TreeEntry
{
    static constexpr size_t kPathSize = 128;
    char path[kPathSize] = {};
    uint8_t sha[Sha1::kDigestSize] = {};
    uint32_t size = 0;
    bool directory = false;
};

/// GET /repos/{repo}/git/ref/heads/{branch}: the commit's 40 hex digits
bool ParseRefCommit(const char* json, size_t length, char commit[41]);
/// GET /repos/{repo}/git/trees/{commit}:{dir}?recursive=1: blobs and trees (submodules skipped)
bool ParseTree(const char* json, size_t length, TreeEntry* entries, size_t capacity, size_t& count, bool& truncated);

}  // namespace zifigit
