/**
 * Reads of loose files at offsets of 2 GiB and above on Horizon (2026-10-12).
 *
 * Bakugan's media\d00.arc is 2.5 GB and the game reads it from offset 2,488,502,272: through newlib's lseek + read
 * (switch_libc_supplement.c pread over fsdev) that failed with EIO on the console, so the game never got past its
 * first load. fsFileRead takes a 64-bit offset on every call, as the asset pack's storage does
 * (filesystem/devices/pack_storage_switch.cpp, whose reads at up to 3.2 GB work). Kept in its own file because
 * switch.h brings u8/Result into the global namespace.
 */

#if defined(__SWITCH__)

#include <switch.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include <rex/logging.h>

namespace rex::filesystem::switch_io {

void* Open(const char* utf8_path) {
  FsFileSystem* fs = nullptr;
  char path_fs[FS_MAX_PATH];
  std::memset(path_fs, 0, sizeof(path_fs));
  if (fsdevTranslatePath(utf8_path, &fs, path_fs) == -1 || !fs) {
    return nullptr;
  }
  auto* file = new FsFile;
  if (R_FAILED(fsFsOpenFile(fs, path_fs, FsOpenMode_Read, file))) {
    delete file;
    return nullptr;
  }
  s64 size = -1;
  const Result src = fsFileGetSize(file, &size);
  REXLOG_INFO("[io] fs open '{}' (fs path '{}'): size {} (result {:08X})", utf8_path, path_fs, size, src);
  return file;
}

void Close(void* handle) {
  if (!handle) return;
  auto* file = static_cast<FsFile*>(handle);
  fsFileClose(file);
  delete file;
}

bool ReadAt(void* handle, uint64_t offset, void* buffer, size_t size, size_t* out_bytes_read) {
  auto* file = static_cast<FsFile*>(handle);
  auto* out = static_cast<uint8_t*>(buffer);
  size_t total = 0;
  // bakugan-nx: never ask the file system for bytes past the end of the file. A voice file read stayed inside
  // fsFileRead for minutes on the console (offset 2048, 128 KB, of an 18 KB file) and the file system service
  // took the whole console with it; a read clamped to the file size cannot straddle the end.
  {
    s64 file_size = -1;
    if (R_SUCCEEDED(fsFileGetSize(file, &file_size)) && file_size >= 0) {
      if (offset >= uint64_t(file_size)) {
        *out_bytes_read = 0;
        return true;
      }
      if (uint64_t(size) > uint64_t(file_size) - offset) size = size_t(uint64_t(file_size) - offset);
    }
  }
  while (total < size) {
    const size_t piece = std::min<size_t>(size - total, 1 << 20);
    u64 read = 0;
    // bakugan-nx: always through a host buffer (see PosixFileHandle::Read): the guest heap is never given to fs.
    static thread_local std::unique_ptr<uint8_t[]> bounce;
    if (!bounce) bounce = std::make_unique<uint8_t[]>(1 << 20);
    Result rc = fsFileRead(file, s64(offset + total), bounce.get(), piece, FsReadOption_None, &read);
    if (R_SUCCEEDED(rc)) std::memcpy(out + total, bounce.get(), size_t(read));
    if (R_FAILED(rc)) {
      static int failed = 0;
      if (failed++ < 8) {
        REXLOG_WARN("[io] fsFileRead of {} bytes at {} failed (result {:08X})", piece, offset + total, rc);
      }
      static bool probed = false;
      if (!probed) {
        probed = true;
        // Diagnostic: which offsets does this file system accept?
        s64 size = -1;
        fsFileGetSize(file, &size);
        static uint8_t tmp[4096];
        const s64 probes[] = {0, 1 << 20, 0x7FFFF000LL, 0x7FFFFFFFLL, 0x80000000LL, 0x80001000LL, 0x90000000LL,
                              0xA0000000LL, s64(offset + total) & ~0xFFFLL, s64(offset + total)};
        for (s64 at : probes) {
          u64 got = 0;
          const Result prc = fsFileRead(file, at, tmp, 4096, FsReadOption_None, &got);
          REXLOG_WARN("[io] probe: size {} read 4096 at {} -> result {:08X}, {} bytes", size, at, prc, got);
        }
      }
      if (total == 0) {
        *out_bytes_read = 0;
        return false;
      }
      break;
    }
    if (read == 0) break;  // end of file
    total += size_t(read);
  }
  *out_bytes_read = total;
  return true;
}

}  // namespace rex::filesystem::switch_io

#endif  // __SWITCH__
