// Plugin folders as the resource manager sees them: which files it loads, in
// what order, and which package serves each key when several have it
// (docs/resources.md, "Which file wins"). The base for conflict and override
// diagnostics and for a mod manager. Portable C++20, no game code.
#pragma once

#include "dbpf/dbpf.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace opensc4::dbpf {

// Sorts the files of a folder segment into the order the game lists them:
// top-level files first, then each subfolder in turn (each one ordered the
// same way), names compared the way NTFS does (case-insensitive, by
// uppercase). Paths are relative to the folder and use '/'. The game was
// probed one subfolder deep; deeper nesting and the exact collation are
// assumed, not measured.
void SortFolderOrder(std::vector<std::string>& relativePaths);

// Every regular file under `folder` (UTF-8), relative and in folder order.
// False (with a reason in `error`) if the folder cannot be listed.
bool ListFolder(const std::string& folder, std::vector<std::string>& out, std::string* error = nullptr);

struct PluginFile {
    std::string path;                 // as given; relative to its folder for folder segments
    size_t segment = 0;               // index in the order segments were added
    std::unique_ptr<Package> package; // null if the file is not a package we read
    std::string error;                // why `package` is null
};

// One key held by more than one package.
struct Conflict {
    Key key;
    // Indices into Files(), the one that serves the key first, then the ones
    // it hides in the order they would take over.
    std::vector<size_t> files;
    // Every copy has the same contents (a shared dependency shipped twice),
    // so which one wins does not matter.
    bool identical = false;
};

// The resource manager's segment list. Segments are asked in the order they
// are added and the first that has a key serves it; inside a folder segment
// the last file listed wins.
class LoadOrder {
public:
    // A folder segment: every file under `folder`, in folder order. Files
    // that are not packages are kept with an error, as the game skips them.
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

    // The file that serves `key`, or null.
    const PluginFile* Winner(const Key& key) const;

    // Every key (but the directory record) that more than one file has,
    // grouped by the winning file (the highest-priority file first) and then
    // by key. With `compareContents`, reads each copy to fill
    // Conflict::identical.
    std::vector<Conflict> Conflicts(bool compareContents = true);

private:
    // File indices from the one that wins to the one asked last.
    std::vector<size_t> Priority() const;

    std::vector<PluginFile> files_;
    size_t segments_ = 0;
};

// A short name for the record types modders meet most, or null.
const char* TypeName(uint32_t type);

} // namespace opensc4::dbpf
