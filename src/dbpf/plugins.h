// Plugin folders as the resource manager sees them: which files it loads, in
// what order, and which package serves each key when several have it
// (docs/resources.md, "Which file wins"). Also what the in-game mod manager
// needs: plugins turned off, a summary per file, the conflicts. Portable
// C++20, no game code.
#pragma once

#include "dbpf/dbpf.h"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace opensc4::dbpf {

// Sorts the files of a folder segment into the order the game lists them:
// top-level files first, then each subfolder in turn (each one ordered the
// same way), names compared the way NTFS does (case-insensitive, by
// uppercase). Paths are relative to the folder and use '/'. The game was
// probed one subfolder deep; deeper nesting and the exact collation are
// assumed, not measured.
void SortFolderOrder(std::vector<std::string>& relativePaths);

// Every regular file under `folder` (UTF-8), relative and in folder order;
// with `recursive` false only the folder's own files. False (with a reason
// in `error`) if the folder cannot be listed.
bool ListFolder(const std::string& folder, std::vector<std::string>& out, std::string* error = nullptr,
                bool recursive = true);

// Whether a file name ends in ".dat", without case. The game loads a plugin
// folder's .dat files as one folder segment and every other package file in
// it as a segment of its own, registered before the folder (so any .dat
// wins over them; docs/resources.md, "Which file wins").
bool IsDatFile(const std::string& path);

// A list of plugin paths, one per line, relative to the Plugins folder with
// '/', UTF-8. A path ending in '/' means everything under that folder. Blank
// lines and lines starting with '#' are ignored. This is how the turned-off
// plugins are kept. A missing file reads as an empty list.
bool ReadPathList(const std::string& file, std::vector<std::string>& out, std::string* error = nullptr);
bool WritePathList(const std::string& file, const std::vector<std::string>& paths, std::string* error = nullptr);
// Whether `path` (relative, '/') is named by `list` (ReadPathList's format),
// compared without case like Windows paths.
bool PathListed(const std::string& path, const std::vector<std::string>& list);

struct PluginFile {
    std::string path;                 // as given; relative to its folder for folder segments
    size_t segment = 0;               // index in the order segments were added
    std::unique_ptr<Package> package; // null if the file is not a package we read
    std::string error;                // why `package` is null
    bool enabled = true;              // off: listed, but serves nothing
};

// One key held by more than one enabled package.
struct Conflict {
    Key key;
    // Indices into Files(), the one that serves the key first, then the ones
    // it hides in the order they would take over.
    std::vector<size_t> files;
    // Every copy has the same contents (a shared dependency shipped twice),
    // so which one wins does not matter.
    bool identical = false;
};

// One file as the mod manager shows it. Counts are distinct keys, the
// directory record aside; zero for files that are off or not packages.
struct FileSummary {
    size_t keys = 0;               // served + hidden
    size_t served = 0;             // keys this file provides
    size_t overrides = 0;          // of those, keys another enabled file also has
    size_t hidden = 0;             // keys another file provides instead
    std::vector<size_t> beats;     // files it hides at least one key of
    std::vector<size_t> beatenBy;  // files that hide at least one of its keys
};

// The resource manager's segment list. Segments are asked in the order they
// are added and the first that has a key serves it; inside a folder segment
// the last file listed wins.
class LoadOrder {
public:
    // A plugin folder as the game loads it, as two segments: its .dat files
    // (the folder segment), then every other file under it (each its own
    // segment in the game, registered in folder order, so here too the last
    // one listed wins). Both in folder order. Files that are not packages are
    // kept with an error, as the game skips them.
    bool AddFolder(const std::string& folder, std::string* error = nullptr);
    // A single-package segment (the game's own .dat files).
    void AddFile(const std::string& path);
    // A folder segment from sources already in folder order (for tests and
    // packages that are not on disk).
    struct NamedSource {
        std::string path;
        std::unique_ptr<Source> source;
    };
    void AddSegment(std::vector<NamedSource> files);

    // In the order the game lists them, segment by segment.
    const std::vector<PluginFile>& Files() const { return files_; }
    size_t SegmentCount() const { return segments_; }

    // A file that is off stays in Files() but serves nothing, as if it were
    // not there.
    void SetEnabled(size_t file, bool enabled);
    // Turns off the files `paths` names (ReadPathList's format, compared
    // without case like Windows paths) and turns on every other. Returns how
    // many entries matched at least one file.
    size_t ApplyDisabled(const std::vector<std::string>& paths);
    // The paths of the files that are off, for WritePathList.
    std::vector<std::string> DisabledPaths() const;

    // The file that serves `key`, or null.
    const PluginFile* Winner(const Key& key) const;
    // The served record's contents, decompressed.
    bool Read(const Key& key, std::vector<uint8_t>& out, std::string* error = nullptr);
    // Distinct keys served by all enabled files together.
    size_t KeyCount() const;

    // One per file, in Files() order.
    std::vector<FileSummary> Summaries() const;

    // Every key (but the directory record) that more than one enabled file
    // has, grouped by the winning file (the highest-priority file first) and
    // then by key. With `compareContents`, reads each copy to fill
    // Conflict::identical.
    std::vector<Conflict> Conflicts(bool compareContents = true);

private:
    // File indices from the one that wins to the one asked last.
    std::vector<size_t> Priority() const;
    const std::unordered_map<Key, std::vector<uint32_t>, KeyHash>& Holders() const;
    void Add(PluginFile file);
    bool ReadEntry(size_t file, const Key& key, std::vector<uint8_t>& out, std::string* error);

    std::vector<PluginFile> files_;
    size_t segments_ = 0;
    // Every key and the enabled files that have it, in priority order. Built
    // on first use, dropped when files are added or turned on or off.
    mutable std::unordered_map<Key, std::vector<uint32_t>, KeyHash> holders_;
    mutable bool indexed_ = false;
    // Files with an open handle, least recently read first.
    std::vector<size_t> open_;
};

// A short name for the record types modders meet most, or null.
const char* TypeName(uint32_t type);

} // namespace opensc4::dbpf
