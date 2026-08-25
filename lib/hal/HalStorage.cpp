#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>

#include <cassert>

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

size_t HalStorage::maxReadFileBytes() { return kMaxReadFileBytes; }

// Whole-file read, implemented here rather than forwarded to the SDK.
//
// The SDK's own readFile silently truncates at its own 50 KB ceiling and cannot
// report which of "over the cap" or "out of heap" happened. Both matter: a file
// cut mid-token reaches a parser as a syntax error at an arbitrary line, which
// sends the author hunting a bug that is not there. Rather than patch the
// vendored submodule (an edit the parent repo cannot even record, so fresh
// clones would not build), the cap and the truncation contract live on our side
// of the HAL boundary, built from primitives the SDK already exposes.
String HalStorage::readFile(const char* path, bool* outTruncated, size_t* outFileSize) {
  // Cleared first: callers declare these once and do not re-initialize them
  // after the call, so every return path below must leave them meaningful.
  if (outTruncated) *outTruncated = false;
  if (outFileSize) *outFileSize = 0;

  // One lock for the whole open→read→close, so no other task interleaves inside
  // a single file's read. Re-entrant by construction: openFileForRead and each
  // HalFile call take it again, and the mutex is recursive (see the ctor).
  StorageLock lock;

  if (!ready()) {
    LOG_ERR("SD", "not initialized; cannot read %s", path);
    return String();
  }

  HalFile f;
  // Logs whether it was missing or unopenable, tagged "SD" — same message the
  // SDK's own readFile produced, since this is the primitive it used too.
  if (!openFileForRead("SD", path, f)) return String();

  const size_t fileSize = f.fileSize();
  if (outFileSize) *outFileSize = fileSize;
  const size_t wantSize = fileSize < kMaxReadFileBytes ? fileSize : kMaxReadFileBytes;

  // Reserve up front. Growing an Arduino String by appending reallocs roughly
  // every 16 bytes, so a 25 KB file would cost ~1600 grow-copy-free cycles — on
  // a heap this size with no PSRAM that is a fragmentation source, not merely
  // slow. One allocation of the known size instead.
  //
  // A failed reserve is not cosmetic: concat swallows a failed grow, so reading
  // on regardless would hand back a short string that looks like a whole file.
  // Stop here and report the short read.
  String content;
  if (wantSize > 0 && !content.reserve(wantSize)) {
    LOG_ERR("SD", "%s: no heap for a %u-byte read", path, static_cast<unsigned>(wantSize));
    if (outTruncated) *outTruncated = true;
    return String();
  }

  // Block reads rather than byte-at-a-time: fewer per-byte calls into SdFat, and
  // one memcpy per chunk. concat(ptr, len) is the length-taking overload, so
  // embedded NULs survive — .luac bytecode is read through here.
  constexpr size_t kReadChunkBytes = 256;  // matches readFileToStream's default
  char buf[kReadChunkBytes];
  size_t readSize = 0;
  while (readSize < wantSize) {
    const size_t remaining = wantSize - readSize;
    const size_t chunk = remaining < kReadChunkBytes ? remaining : kReadChunkBytes;
    const int got = f.read(buf, chunk);
    if (got <= 0) break;  // EOF or read error; the truncation check below reports it
    if (!content.concat(buf, static_cast<unsigned>(got))) break;  // cannot happen after reserve
    readSize += static_cast<size_t>(got);
  }
  // Not closed explicitly: HalFile's destructor closes under the lock
  // (DESTRUCTOR_CLOSES_FILE), which is the convention for a local handle.

  // Comparing the string against the file's true size covers every cause at
  // once — over the cap, a short read, a failed append — so no shortfall can
  // escape as a whole-looking file.
  const bool truncated = content.length() < fileSize;
  if (truncated) {
    LOG_ERR("SD", "%s TRUNCATED: read %u of %u bytes (cap %u)", path, static_cast<unsigned>(content.length()),
            static_cast<unsigned>(fileSize), static_cast<unsigned>(kMaxReadFileBytes));
  }
  if (outTruncated) *outTruncated = truncated;
  return content;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock;
    file.close();
  }
  FsFile file;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  return HalFile(std::make_unique<HalFile::Impl>(SDCard.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
