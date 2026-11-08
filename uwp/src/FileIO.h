// Small file-system layer for the Xbox launcher.
//
// On Windows (UWP) every call goes through the *FromApp Win32 functions
// (fileapifromapp.h). Those honour the broadFileSystemAccess capability, so
// the launcher can read D:\DevelopmentFiles and USB drives on an Xbox in
// Developer Mode, and write into its own LocalState folder.
//
// The POSIX build exists only so the importer can be unit-tested on Linux
// (uwp/tests). Paths are std::wstring with '\\' separators on both.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fsx {

struct Entry {
    std::wstring name;
    bool         is_dir = false;
    uint64_t     size   = 0;
};

class File {
public:
    File() = default;
    ~File();
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    File(File &&o) noexcept;
    File &operator=(File &&o) noexcept;

    bool OpenRead(const std::wstring &path);
    bool OpenWrite(const std::wstring &path);   // create / truncate
    void Close();
    bool IsOpen() const;

    bool     ReadAt(uint64_t offset, void *dst, size_t len);
    bool     Read(void *dst, size_t len, size_t &got);
    bool     Write(const void *src, size_t len);
    uint64_t Size();

private:
    void *h_ = nullptr;   // HANDLE on Windows, FILE* on POSIX
};

std::wstring Join(const std::wstring &a, const std::wstring &b);
std::wstring Parent(const std::wstring &path);
std::wstring FileName(const std::wstring &path);
std::wstring Lower(std::wstring s);
bool         EndsWithI(const std::wstring &s, const std::wstring &suffix);

bool Exists(const std::wstring &path);
bool IsDir(const std::wstring &path);
bool FileSize(const std::wstring &path, uint64_t &size);
bool MakeDirs(const std::wstring &path);
bool RemoveTree(const std::wstring &path);
bool Rename(const std::wstring &from, const std::wstring &to);
std::vector<Entry> List(const std::wstring &dir);

// UTF-16 <-> UTF-8 (for logs and the POSIX build).
std::string  Narrow(const std::wstring &w);
std::wstring Widen(const std::string &s);

} // namespace fsx
