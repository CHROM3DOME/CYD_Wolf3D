#include <Arduino.h>
#include <SD.h>
#include <fcntl.h>
#include <stdarg.h>

#include <SDL.h>

// This file implements the redirected calls, so Arduino FS member functions
// must retain their original names.
#undef open
#undef close
#undef read
#undef write
#undef lseek
#undef stat
#undef unlink
#undef mkdir

namespace {
constexpr int maxFiles = 16;
File descriptors[maxFiles];

String gamePath(const char *path) {
  if (!path) return "/wolf3d/";
  if (path[0] == '/') return String(path);
  return String("/wolf3d/") + path;
}

String upperFilenamePath(String path) {
  int slash = path.lastIndexOf('/');
  for (int i = slash + 1; i < path.length(); ++i) {
    path.setCharAt(i, toupper(path.charAt(i)));
  }
  return path;
}

String upperPath(String path) {
  path.toUpperCase();
  return path;
}

String baseName(String path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

String dirName(String path) {
  int slash = path.lastIndexOf('/');
  if (slash <= 0) return "/";
  return path.substring(0, slash);
}

bool pathExistsByListing(const String &path) {
  String dirPath = dirName(path);
  String wanted = baseName(path);
  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String found = baseName(entry.name());
    bool match = found.equalsIgnoreCase(wanted);
    entry.close();
    if (match) {
      dir.close();
      return true;
    }
  }
  dir.close();
  return false;
}

File openExistingReadFile(const char *path) {
  String original = gamePath(path);
  File file = SD.open(original, FILE_READ);
  if (file) return file;

  String upperName = upperFilenamePath(original);
  if (upperName != original) {
    file = SD.open(upperName, FILE_READ);
    if (file) return file;
  }

  String allUpper = upperPath(original);
  if (allUpper != original && allUpper != upperName) {
    file = SD.open(allUpper, FILE_READ);
    if (file) return file;
  }

  return File();
}

String resolveExistingPath(const char *path) {
  String original = gamePath(path);
  if (pathExistsByListing(original)) return original;

  String upperName = upperFilenamePath(original);
  if (pathExistsByListing(upperName)) return upperName;

  String allUpper = upperPath(original);
  if (pathExistsByListing(allUpper)) return allUpper;

  return original;
}

SemaphoreHandle_t sdMutex = nullptr;

void lockSD() {
  if (!sdMutex) {
    sdMutex = xSemaphoreCreateMutex();
  }
  xSemaphoreTake(sdMutex, portMAX_DELAY);
}

void unlockSD() {
  if (sdMutex) {
    xSemaphoreGive(sdMutex);
  }
}

struct SDLock {
  SDLock() { lockSD(); }
  ~SDLock() { unlockSD(); }
};

int allocate(File file) {
  for (int i = 1; i < maxFiles; ++i) if (!descriptors[i]) { descriptors[i] = file; return i; }
  return -1;
}
}

extern "C" int wolf_open(const char *path, int flags, ...) {
  SDLock lock;
  const bool writing = flags & (O_WRONLY | O_RDWR | O_CREAT);
  File file = writing ? SD.open(gamePath(path), FILE_WRITE) : openExistingReadFile(path);
  return file ? allocate(file) : -1;
}
extern "C" int wolf_close(int fd) {
  SDLock lock;
  if (fd <= 0 || fd >= maxFiles || !descriptors[fd]) return -1;
  descriptors[fd].close();
  descriptors[fd] = File();
  return 0;
}
extern "C" int32_t wolf_read(int fd, void *buffer, size_t count) {
  SDLock lock;
  return fd > 0 && fd < maxFiles && descriptors[fd] ? descriptors[fd].read(static_cast<uint8_t *>(buffer), count) : -1;
}
extern "C" int32_t wolf_write(int fd, const void *buffer, size_t count) {
  SDLock lock;
  return fd > 0 && fd < maxFiles && descriptors[fd] ? descriptors[fd].write(static_cast<const uint8_t *>(buffer), count) : -1;
}
extern "C" int32_t wolf_lseek(int fd, int32_t offset, int whence) {
  SDLock lock;
  if (fd <= 0 || fd >= maxFiles || !descriptors[fd]) return -1;
  int32_t target = offset;
  if (whence == SEEK_CUR) target += descriptors[fd].position();
  else if (whence == SEEK_END) target += descriptors[fd].size();
  return descriptors[fd].seek(target, SeekSet) ? target : -1;
}
extern "C" int wolf_stat(const char *path, void *) {
  SDLock lock;
  return pathExistsByListing(resolveExistingPath(path)) ? 0 : -1;
}
extern "C" int wolf_unlink(const char *path) {
  SDLock lock;
  return SD.remove(gamePath(path)) ? 0 : -1;
}
extern "C" int wolf_mkdir(const char *path, int) {
  SDLock lock;
  String resolved = gamePath(path);
  if (SD.exists(resolved)) return 0;
  return SD.mkdir(resolved) ? 0 : -1;
}

struct WolfFile { File file; bool eof; };
extern "C" WolfFile *wolf_fopen(const char *path, const char *mode) {
  SDLock lock;
  WolfFile *result = new WolfFile;
  const bool writing = strchr(mode, 'w') || strchr(mode, 'a');
  result->file = writing ? SD.open(gamePath(path), FILE_WRITE) : openExistingReadFile(path);
  result->eof = false;
  if (!result->file) { delete result; return nullptr; }
  return result;
}
extern "C" int wolf_fclose(WolfFile *file) {
  SDLock lock;
  if (!file) return -1;
  file->file.close();
  delete file;
  return 0;
}
extern "C" size_t wolf_fread(void *buffer, size_t size, size_t count, WolfFile *file) {
  SDLock lock;
  if (!file || !size) return 0;
  size_t bytes = file->file.read(static_cast<uint8_t *>(buffer), size * count);
  file->eof = bytes < size * count;
  return bytes / size;
}
extern "C" size_t wolf_fwrite(const void *buffer, size_t size, size_t count, WolfFile *file) {
  SDLock lock;
  return file && size ? file->file.write(static_cast<const uint8_t *>(buffer), size * count) / size : 0;
}
extern "C" int wolf_fseek(WolfFile *file, long offset, int whence) {
  SDLock lock;
  if (!file) return -1;
  long target=offset;
  if(whence==SEEK_CUR) target+=file->file.position();
  else if(whence==SEEK_END) target+=file->file.size();
  file->eof=false;
  return file->file.seek(target, SeekSet) ? 0 : -1;
}
extern "C" long wolf_ftell(WolfFile *file) {
  SDLock lock;
  return file ? file->file.position() : -1;
}
extern "C" int wolf_feof(WolfFile *file) {
  return file && file->eof;
}
