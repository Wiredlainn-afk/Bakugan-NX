// bakugan - WMV movies decoded natively with FFmpeg
//
// The game's videos (Movies/*.wmv) are WMV3 main profile, 1280x720 at 30 fps and without B frames. On
// the Switch the XDK's recompiled decoder runs at ~98 % of a core and produces ~22 frames per second:
// the picture falls behind and the audio ends first. Videos re-encoded by a translation can have B frames and
// the loop filter.
//  - Wmv3Decoder: decodes with FFmpeg's WMV3 the compressed frames as stored in the container, without
//    reordering: each frame comes out of the call that decodes it, also with B frames.
//    bakugan_video_nativo.cpp passes it the same bytes the game hands to its own decoder.
//  - ReadWmvInfo: reads the size and the 4 WMV3 sequence bytes from the movie's ASF header.
//  - WmvMovie: full ASF reader (diagnostic: decodes the file on its own).

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bakugan::video_wmv3 {

// YUV 4:2:0 planes of the last decoded frame; valid until the next call.
struct Frame {
  const uint8_t* planes[3] = {nullptr, nullptr, nullptr};
  int strides[3] = {0, 0, 0};
  int width = 0;
  int height = 0;
  bool key = false;
  uint32_t bytes = 0;  // compressed frame size
};

struct WmvInfo {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> sequence;  // extra data of the WMV3 stream (STRUCT_C, 4 bytes)
};

// Reads the movie's ASF header using the guest path (the same one NtCreateFile uses).
bool ReadWmvInfo(const std::string& ruta, WmvInfo& info);

class Wmv3Decoder {
 public:
  Wmv3Decoder();
  ~Wmv3Decoder();
  Wmv3Decoder(const Wmv3Decoder&) = delete;
  Wmv3Decoder& operator=(const Wmv3Decoder&) = delete;

  bool Open(const WmvInfo& info);
  // Decodes one complete compressed frame. false if FFmpeg fails or returns no picture.
  bool Decode(const uint8_t* datos, size_t bytes, bool key, Frame& salida);

  int width() const { return width_; }
  int height() const { return height_; }
  int64_t fotogramas() const { return fotogramas_; }

 private:
  struct Estado;
  std::unique_ptr<Estado> e_;
  int width_ = 0;
  int height_ = 0;
  int64_t fotogramas_ = 0;
};

class WmvMovie {
 public:
  WmvMovie();
  ~WmvMovie();
  WmvMovie(const WmvMovie&) = delete;
  WmvMovie& operator=(const WmvMovie&) = delete;

  // Opens the movie with the guest path and prepares the decoder.
  bool Open(const std::string& ruta);
  // Reads the next video frame from the container and decodes it. false at the end or on failure.
  bool Next(Frame& salida);
  // The next compressed video frame as stored in the container (no decoding), and the decode of a frame given to it.
  bool NextRaw(std::vector<uint8_t>& data, bool& key, uint32_t& number);
  bool DecodeRaw(const std::vector<uint8_t>& data, bool key, Frame& salida);

  const std::string& ruta() const { return ruta_; }
  int width() const { return info_.width; }
  int height() const { return info_.height; }
  int64_t fotogramas() const { return fotogramas_; }

 private:
  struct Estado;
  std::unique_ptr<Estado> e_;
  std::string ruta_;
  WmvInfo info_;
  int64_t fotogramas_ = 0;
};

}  // namespace bakugan::video_wmv3
