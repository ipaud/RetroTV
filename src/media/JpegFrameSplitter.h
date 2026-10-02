#pragma once

// Cuts a raw .mjpeg stream (JPEGs back to back) into single frames, FFD8 ... FFD9.
// Pure C++: tested on the host. Inside JPEG entropy data every 0xFF is followed by 0x00 or
// a restart marker, so FF D9 can only be the real end of image.
// Frames larger than the buffer are never written past it: the rest is skipped up to FFD9,
// the frame is dropped and counted in overflows().

#include <stddef.h>
#include <stdint.h>

class JpegFrameSplitter {
 public:
  enum class Result : uint8_t { Frame, EndOfStream };

  // frame: where one whole JPEG is assembled. chunk: scratch for reads from the source.
  void attach(uint8_t* frame, size_t frameCap, uint8_t* chunk, size_t chunkCap) {
    frame_ = frame;
    frameCap_ = frameCap;
    chunk_ = chunk;
    chunkCap_ = chunkCap;
    reset();
  }

  // Forget any partial frame and buffered bytes (new source).
  void reset() {
    chunkLen_ = chunkPos_ = frameLen_ = 0;
    inFrame_ = discarding_ = prevFF_ = false;
  }

  uint32_t overflows() const { return overflows_; }

  // read(dst, max) -> bytes read, 0 at end of stream. On Frame, frame[0 .. len) holds the JPEG.
  template <typename ReadFn>
  Result next(ReadFn&& read, size_t& len) {
    for (;;) {
      if (chunkPos_ == chunkLen_) {
        chunkLen_ = read(chunk_, chunkCap_);
        chunkPos_ = 0;
        if (chunkLen_ == 0) {
          reset();  // a frame cut short by the end of the file is dropped
          return Result::EndOfStream;
        }
      }
      while (chunkPos_ < chunkLen_) {
        const uint8_t b = chunk_[chunkPos_++];
        if (!inFrame_) {
          if (prevFF_ && b == 0xD8) startFrame();
          else prevFF_ = b == 0xFF;
          continue;
        }
        if (frameLen_ < frameCap_) frame_[frameLen_++] = b;
        else discarding_ = true;

        if (prevFF_ && b == 0xD9) {
          inFrame_ = prevFF_ = false;
          if (discarding_) {
            ++overflows_;
            continue;
          }
          len = frameLen_;
          return Result::Frame;
        }
        prevFF_ = b == 0xFF;
      }
    }
  }

 private:
  void startFrame() {
    inFrame_ = true;
    discarding_ = prevFF_ = false;
    frame_[0] = 0xFF;
    frame_[1] = 0xD8;
    frameLen_ = 2;
  }

  uint8_t* frame_ = nullptr;
  size_t frameCap_ = 0;
  uint8_t* chunk_ = nullptr;
  size_t chunkCap_ = 0;
  size_t chunkLen_ = 0;
  size_t chunkPos_ = 0;
  size_t frameLen_ = 0;
  bool inFrame_ = false;
  bool discarding_ = false;
  bool prevFF_ = false;
  uint32_t overflows_ = 0;
};
