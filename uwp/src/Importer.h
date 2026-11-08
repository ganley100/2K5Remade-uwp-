// Finds the player's own copy of NFL 2K5 and imports it into the app's
// storage, where the game reads it from:
//
//     <LocalState>\NFL2K5\original\disc\default.xbe   (+ the rest of the disc)
//
// A source is either an extracted disc folder (contains default.xbe) or an
// Xbox disc image (.iso / .xiso, redump or extract-xiso layout). Nothing is
// downloaded and nothing from the game ships with the app.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace importer {

// SHA-256 of the retail NTSC default.xbe that the recompiled code targets
// (same value as NFL2K5_RETAIL_XBE_SHA256 in src/main.c).
extern const char *const kRetailXbeSha256;

struct Source {
    enum Kind { Folder, Image } kind = Folder;
    std::wstring path;     // folder or image file
    std::wstring label;    // what the launcher shows
    uint64_t     bytes = 0;
};

// Partition offsets an Xbox image can start at (XISO, XGD1/2/3 redump).
bool FindXdvdfs(const std::wstring &image, uint64_t &partition_offset);

// Folders worth scanning on this device, in order. On Xbox this is the
// Device Portal "DevelopmentFiles" share, USB drives, and LocalState\Import.
std::vector<std::wstring> DefaultScanRoots(const std::wstring &local_state);

std::vector<Source> FindSources(const std::vector<std::wstring> &roots);

// Inspect a source path the user picked by hand (folder or image).
bool DescribeSource(const std::wstring &path, Source &out);

struct InstallInfo {
    bool        installed = false;   // default.xbe present
    bool        complete  = false;   // default.xbe and vc_53450030 present
    bool        retail    = false;   // XBE hash matches kRetailXbeSha256
    std::string xbe_sha256;
    std::wstring disc_dir;
};

// <root>\original\disc
std::wstring DiscDir(const std::wstring &game_root);
InstallInfo  Inspect(const std::wstring &game_root);
bool         Remove(const std::wstring &game_root);

struct Progress {
    std::atomic<uint64_t> done_bytes{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<bool>     running{false};
    std::atomic<bool>     cancel{false};
    std::atomic<bool>     succeeded{false};

    void         SetStatus(const std::wstring &s) { std::lock_guard<std::mutex> l(m_); status_ = s; }
    std::wstring Status() { std::lock_guard<std::mutex> l(m_); return status_; }

private:
    std::mutex   m_;
    std::wstring status_;
};

// Synchronous import (runs on the caller's thread). Copies/extracts into
// <game_root>\original\disc.importing and only replaces the existing disc
// folder once everything was written.
bool Import(const Source &src, const std::wstring &game_root, Progress &progress);

// Runs Import() on a worker thread.
class Job {
public:
    ~Job();
    void      Start(const Source &src, const std::wstring &game_root);
    Progress &State() { return progress_; }
    void      Join();

private:
    std::thread worker_;
    Progress    progress_;
};

} // namespace importer
