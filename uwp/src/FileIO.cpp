#include "FileIO.h"

#include <algorithm>
#include <cstring>
#include <cwctype>

#if defined(_WIN32)
#include <windows.h>
#include <fileapifromapp.h>
#else
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fsx {

// ---- string helpers -------------------------------------------------------

std::wstring Join(const std::wstring &a, const std::wstring &b)
{
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring Parent(const std::wstring &path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring FileName(const std::wstring &path)
{
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring Lower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

bool EndsWithI(const std::wstring &s, const std::wstring &suffix)
{
    if (suffix.size() > s.size()) return false;
    return Lower(s.substr(s.size() - suffix.size())) == Lower(suffix);
}

std::string Narrow(const std::wstring &w)
{
    std::string out;
    for (size_t i = 0; i < w.size(); i++) {
        uint32_t c = (uint32_t)w[i];
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            uint32_t lo = (uint32_t)w[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i++; }
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::wstring Widen(const std::string &s)
{
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += n;
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += (wchar_t)(0xD800 + (cp >> 10));
            out += (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out += (wchar_t)cp;
        }
    }
    return out;
}

// ---- File -------------------------------------------------------------------

File::~File() { Close(); }
File::File(File &&o) noexcept : h_(o.h_) { o.h_ = nullptr; }
File &File::operator=(File &&o) noexcept
{
    if (this != &o) { Close(); h_ = o.h_; o.h_ = nullptr; }
    return *this;
}
bool File::IsOpen() const { return h_ != nullptr; }

#if defined(_WIN32)

static bool find_data(const std::wstring &path, WIN32_FILE_ATTRIBUTE_DATA &d)
{
    return GetFileAttributesExFromAppW(path.c_str(), GetFileExInfoStandard, &d) != FALSE;
}

bool File::OpenRead(const std::wstring &path)
{
    Close();
    CREATEFILE2_EXTENDED_PARAMETERS p = { sizeof(p) };
    p.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    p.dwFileFlags      = FILE_FLAG_SEQUENTIAL_SCAN;
    HANDLE h = CreateFile2FromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &p);
    if (h == INVALID_HANDLE_VALUE) return false;
    h_ = h;
    return true;
}

bool File::OpenWrite(const std::wstring &path)
{
    Close();
    CREATEFILE2_EXTENDED_PARAMETERS p = { sizeof(p) };
    p.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    HANDLE h = CreateFile2FromAppW(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, &p);
    if (h == INVALID_HANDLE_VALUE) return false;
    h_ = h;
    return true;
}

void File::Close()
{
    if (h_) { CloseHandle((HANDLE)h_); h_ = nullptr; }
}

bool File::ReadAt(uint64_t offset, void *dst, size_t len)
{
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx((HANDLE)h_, li, nullptr, FILE_BEGIN)) return false;
    size_t got = 0;
    return Read(dst, len, got) && got == len;
}

bool File::Read(void *dst, size_t len, size_t &got)
{
    got = 0;
    while (got < len) {
        DWORD chunk = (DWORD)std::min<size_t>(len - got, 1u << 30), n = 0;
        if (!ReadFile((HANDLE)h_, (uint8_t *)dst + got, chunk, &n, nullptr)) return false;
        if (n == 0) break;
        got += n;
    }
    return true;
}

bool File::Write(const void *src, size_t len)
{
    size_t done = 0;
    while (done < len) {
        DWORD chunk = (DWORD)std::min<size_t>(len - done, 1u << 30), n = 0;
        if (!WriteFile((HANDLE)h_, (const uint8_t *)src + done, chunk, &n, nullptr) || n == 0) return false;
        done += n;
    }
    return true;
}

uint64_t File::Size()
{
    LARGE_INTEGER li;
    return GetFileSizeEx((HANDLE)h_, &li) ? (uint64_t)li.QuadPart : 0;
}

bool Exists(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    return find_data(path, d);
}

bool IsDir(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    return find_data(path, d) && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileSize(const std::wstring &path, uint64_t &size)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!find_data(path, d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return true;
}

bool MakeDirs(const std::wstring &path)
{
    if (path.empty() || IsDir(path)) return true;
    std::wstring parent = Parent(path);
    if (!parent.empty() && parent != path && !(parent.size() == 2 && parent[1] == L':'))
        if (!MakeDirs(parent)) return false;
    return CreateDirectoryFromAppW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<Entry> List(const std::wstring &dir)
{
    std::vector<Entry> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExFromAppW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd,
                                       FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        Entry e;
        e.name   = fd.cFileName;
        e.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size   = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool RemoveTree(const std::wstring &path)
{
    if (!Exists(path)) return true;
    if (!IsDir(path)) return DeleteFileFromAppW(path.c_str()) != FALSE;
    bool ok = true;
    for (const Entry &e : List(path))
        ok = RemoveTree(Join(path, e.name)) && ok;
    return RemoveDirectoryFromAppW(path.c_str()) && ok;
}

bool Rename(const std::wstring &from, const std::wstring &to)
{
    return MoveFileFromAppW(from.c_str(), to.c_str()) != FALSE;
}

#else // ---- POSIX (tests only) -------------------------------------------

static std::string P(const std::wstring &w)
{
    std::string s = Narrow(w);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool File::OpenRead(const std::wstring &path)
{
    Close();
    h_ = std::fopen(P(path).c_str(), "rb");
    return h_ != nullptr;
}

bool File::OpenWrite(const std::wstring &path)
{
    Close();
    h_ = std::fopen(P(path).c_str(), "wb");
    return h_ != nullptr;
}

void File::Close()
{
    if (h_) { std::fclose((FILE *)h_); h_ = nullptr; }
}

bool File::ReadAt(uint64_t offset, void *dst, size_t len)
{
    if (fseeko((FILE *)h_, (off_t)offset, SEEK_SET) != 0) return false;
    return std::fread(dst, 1, len, (FILE *)h_) == len;
}

bool File::Read(void *dst, size_t len, size_t &got)
{
    got = std::fread(dst, 1, len, (FILE *)h_);
    return !std::ferror((FILE *)h_);
}

bool File::Write(const void *src, size_t len)
{
    return std::fwrite(src, 1, len, (FILE *)h_) == len;
}

uint64_t File::Size()
{
    struct stat st;
    return fstat(fileno((FILE *)h_), &st) == 0 ? (uint64_t)st.st_size : 0;
}

bool Exists(const std::wstring &path) { struct stat st; return stat(P(path).c_str(), &st) == 0; }
bool IsDir(const std::wstring &path) { struct stat st; return stat(P(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

bool FileSize(const std::wstring &path, uint64_t &size)
{
    struct stat st;
    if (stat(P(path).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
    size = (uint64_t)st.st_size;
    return true;
}

bool MakeDirs(const std::wstring &path)
{
    if (path.empty() || IsDir(path)) return true;
    std::wstring parent = Parent(path);
    if (!parent.empty() && parent != path && !MakeDirs(parent)) return false;
    return mkdir(P(path).c_str(), 0755) == 0 || errno == EEXIST;
}

std::vector<Entry> List(const std::wstring &dir)
{
    std::vector<Entry> out;
    DIR *d = opendir(P(dir).c_str());
    if (!d) return out;
    while (dirent *de = readdir(d)) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        Entry e;
        e.name = Widen(de->d_name);
        std::wstring full = Join(dir, e.name);
        e.is_dir = IsDir(full);
        if (!e.is_dir) FileSize(full, e.size);
        out.push_back(std::move(e));
    }
    closedir(d);
    return out;
}

bool RemoveTree(const std::wstring &path)
{
    if (!Exists(path)) return true;
    if (!IsDir(path)) return unlink(P(path).c_str()) == 0;
    bool ok = true;
    for (const Entry &e : List(path))
        ok = RemoveTree(Join(path, e.name)) && ok;
    return rmdir(P(path).c_str()) == 0 && ok;
}

bool Rename(const std::wstring &from, const std::wstring &to)
{
    return std::rename(P(from).c_str(), P(to).c_str()) == 0;
}

#endif

} // namespace fsx
