#pragma once

// An episode file, read with POSIX read() on the card's VFS. Arduino's fs::File goes through
// stdio, 4 KB at a time through a buffer in PSRAM; read() hands FatFs the whole request. While the
// TV plays: 1.3 -> 1.0 ms per KB (2026-10-01). The rest of the wait is hidden by SdPrefetch.

#include <Stream.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

class SdFile : public Stream {
 public:
  SdFile() = default;
  SdFile(const SdFile&) = delete;
  SdFile& operator=(const SdFile&) = delete;
  ~SdFile() override { close(); }

  // fullPath includes the VFS mount point.
  bool open(const char* fullPath) {
    close();
    fd_ = ::open(fullPath, O_RDONLY);
    struct stat st;
    size_ = fd_ >= 0 && fstat(fd_, &st) == 0 ? static_cast<uint32_t>(st.st_size) : 0;
    pos_ = 0;
    return fd_ >= 0;
  }
  void close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }
  explicit operator bool() const { return fd_ >= 0; }

  bool seek(uint32_t pos) {
    if (fd_ < 0 || lseek(fd_, static_cast<off_t>(pos), SEEK_SET) < 0) return false;
    pos_ = pos;
    return true;
  }
  uint32_t position() const { return pos_; }
  uint32_t size() const { return size_; }

  size_t read(uint8_t* buf, size_t len) {
    const ssize_t n = fd_ >= 0 ? ::read(fd_, buf, len) : -1;
    if (n <= 0) return 0;  // end of file, or the card failed: the caller compares position() and size()
    pos_ += static_cast<uint32_t>(n);
    return static_cast<size_t>(n);
  }
  size_t readBytes(char* buf, size_t len) override { return read(reinterpret_cast<uint8_t*>(buf), len); }

  // Stream and Print want these; the player only reads in blocks.
  int available() override { return static_cast<int>(size_ - pos_); }
  int read() override {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 0; }

 private:
  int fd_ = -1;
  uint32_t pos_ = 0;
  uint32_t size_ = 0;
};
