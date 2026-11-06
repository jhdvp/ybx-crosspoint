#include "NotesStorage.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace nativeapps::notes {
namespace {

constexpr const char* kTag = "NOTES";
bool readValid(const std::string& path, Document& target) {
  std::string raw;
  if (!Storage.readFileToString(kTag, path, kMaxFileBytes, raw)) return false;
  return decode(raw, target);
}

bool checkedWriteTemp(const std::string& path, const std::string& bytes) {
  HalFile file;
  if (!Storage.openFileForWrite(kTag, path, file)) return false;
  const size_t count = file.write(bytes.data(), bytes.size());
  file.flush();
  if (!file.close() || count != bytes.size()) return false;

  // Verify a fully closed file before moving or deleting the previous revision.
  Document roundTrip;
  if (!readValid(path, roundTrip)) return false;
  std::string verified;
  return encode(roundTrip, verified) && verified == bytes;
}

bool recoverBackupIfMissing(const std::string& path) {
  if (Storage.exists(path.c_str())) return true;
  const std::string backup = path + ".bak";
  if (!Storage.exists(backup.c_str())) return false;
  Document old;
  if (!readValid(backup, old)) return false;
  if (!Storage.rename(backup.c_str(), path.c_str())) {
    LOG_ERR(kTag, "Backup restore failed (path withheld)");
    return false;
  }
  return true;
}

bool ensureReady() { return Storage.ready() && Storage.ensureDirectoryExists(kDirectory); }

}  // namespace

bool load(const std::string_view fileName, Document& out) {
  const std::string path = pathFor(fileName);
  if (path.empty() || !Storage.ready()) return false;
  if (!recoverBackupIfMissing(path)) return false;
  return readValid(path, out);
}

bool save(const std::string_view fileName, const Document& value) {
  const std::string path = pathFor(fileName);
  std::string bytes;
  if (path.empty() || !encode(value, bytes) || !ensureReady()) return false;

  const std::string temp = path + ".tmp";
  const std::string backup = path + ".bak";
  if (!checkedWriteTemp(temp, bytes)) {
    LOG_ERR(kTag, "Temp write/verify failed (path withheld)");
    return false;  // Do not touch the previously committed note.
  }

  const bool exists = Storage.exists(path.c_str());
  if (exists) {
    // Never rotate or delete the previous valid backup if a canonical file
    // has become corrupt/unreadable since the user opened it.
    Document previous;
    if (!readValid(path, previous)) {
      Storage.remove(temp.c_str());
      return false;
    }
    // Old backup is redundant while the current canonical file is intact.
    // On failure, leave canonical untouched and remove temp.
    if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) {
      Storage.remove(temp.c_str());
      return false;
    }
    if (!Storage.rename(path.c_str(), backup.c_str())) {
      Storage.remove(temp.c_str());
      return false;
    }
  } else if (Storage.exists(backup.c_str())) {
    // An interrupted older commit is awaiting recovery. Do not let a new
    // creation overwrite its only still-valid revision.
    Storage.remove(temp.c_str());
    return false;
  }

  if (!Storage.rename(temp.c_str(), path.c_str())) {
    if (exists && !Storage.rename(backup.c_str(), path.c_str())) {
      LOG_ERR(kTag, "Restore after failed commit failed (path withheld)");
    }
    Storage.remove(temp.c_str());
    return false;
  }
  return true;
}

bool create(const Document& value, std::string& newFileName) {
  if (!validDocument(value) || !ensureReady()) return false;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const std::string name = fileName(esp_random(), esp_random());
    const std::string path = pathFor(name);
    if (Storage.exists(path.c_str()) || Storage.exists((path + ".bak").c_str()) ||
        Storage.exists((path + ".tmp").c_str()))
      continue;
    if (!save(name, value)) return false;
    newFileName = name;
    return true;
  }
  LOG_ERR(kTag, "Could not allocate unique note id");
  return false;
}

bool erase(const std::string_view fileName) {
  const std::string path = pathFor(fileName);
  if (path.empty() || !Storage.ready() || !Storage.exists(path.c_str())) return false;
  // Remove recovery candidates first so a reboot cannot resurrect a note
  // after its final delete. Canonical is removed last and remains on errors.
  const std::string backup = path + ".bak";
  const std::string temp = path + ".tmp";
  if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
  if (Storage.exists(temp.c_str()) && !Storage.remove(temp.c_str())) return false;
  return Storage.remove(path.c_str());
}

List list() {
  List result;
  if (!ensureReady()) {
    result.failed = true;
    return result;
  }
  HalFile dir = Storage.open(kDirectory);
  if (!dir || !dir.isDirectory()) {
    result.failed = true;
    return result;
  }

  // Collect names before performing recovery: renaming entries while holding
  // an open directory iterator is unsafe on some FAT implementations.
  std::vector<std::string> candidates;
  for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    char name[64] = {};
    if (!file.getName(name, sizeof(name))) continue;
    const std::string fullName(name);
    std::string noteName;
    if (validFileName(fullName)) {
      noteName = fullName;
    } else if (fullName.size() > 4 && fullName.substr(fullName.size() - 4) == ".bak" &&
               validFileName(std::string_view(fullName).substr(0, fullName.size() - 4))) {
      noteName = fullName.substr(0, fullName.size() - 4);
    }
    if (noteName.empty()) continue;
    // A large/copied SD directory must never force an unbounded heap
    // allocation simply from listing names. Keep an extra slot to signal
    // truncation without materializing the remainder.
    if (candidates.size() < kMaxNotes + 1)
      candidates.push_back(std::move(noteName));
    else
      result.truncated = true;
  }
  dir.close();
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

  if (candidates.size() > kMaxNotes) {
    result.truncated = true;
    candidates.resize(kMaxNotes);
  }
  result.rows.reserve(candidates.size());
  for (const auto& name : candidates) {
    Summary summary;
    summary.fileName = name;
    Document document;
    summary.readable = load(name, document);
    summary.title = summary.readable ? std::move(document.title) : std::string{};
    result.rows.push_back(std::move(summary));
  }
  return result;
}

}  // namespace nativeapps::notes
