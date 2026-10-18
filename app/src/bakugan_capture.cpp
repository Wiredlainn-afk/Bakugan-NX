// Test captures of the game image (see bakugan_capture.h).

#include "bakugan_capture.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_INT32(bakugan_shot_s, 0, "Bakugan",
                     "First PNG snapshot of the game image this many seconds after start (0 = off)");
REXCVAR_DEFINE_INT32(bakugan_shot_count, 1, "Bakugan", "Number of PNG snapshots (bakugan_shot_s)");
REXCVAR_DEFINE_INT32(bakugan_shot_every_s, 5, "Bakugan", "Seconds between PNG snapshots");
REXCVAR_DEFINE_INT32(bakugan_video_s, 0, "Bakugan",
                     "Record a raw video of the game image from this many seconds after start (0 = off)");
REXCVAR_DEFINE_INT32(bakugan_video_frames, 300, "Bakugan", "Frames in the video (bakugan_video_s), about 30 a second");
REXCVAR_DEFINE_BOOL(bakugan_video_half, true, "Bakugan", "Video at half resolution (every other pixel)");

namespace bakugan::capture {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
bool g_stop = false;
std::thread g_thread;

bool Wait(std::chrono::milliseconds d) {
  std::unique_lock lock(g_mutex);
  return g_cv.wait_for(lock, d, [] { return g_stop; });
}

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void PutBe32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(uint8_t(x >> 24));
  v.push_back(uint8_t(x >> 16));
  v.push_back(uint8_t(x >> 8));
  v.push_back(uint8_t(x));
}

void Chunk(std::ofstream& f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> head;
  PutBe32(head, uint32_t(data.size()));
  f.write(reinterpret_cast<const char*>(head.data()), 4);
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  f.write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
  std::vector<uint8_t> crc;
  PutBe32(crc, Crc32(body.data(), body.size()));
  f.write(reinterpret_cast<const char*>(crc.data()), 4);
}

// PNG without compression (zlib stored blocks): big, but simple and exact.
bool WritePng(const std::filesystem::path& path, const rex::ui::RawImage& image) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  f.write(reinterpret_cast<const char*>(sig), 8);
  std::vector<uint8_t> ihdr;
  PutBe32(ihdr, image.width);
  PutBe32(ihdr, image.height);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bit, RGB
  Chunk(f, "IHDR", ihdr);
  std::vector<uint8_t> raw;
  raw.reserve(size_t(image.width) * image.height * 3 + image.height);
  for (uint32_t y = 0; y < image.height; ++y) {
    raw.push_back(0);  // filter: none
    const uint8_t* row = image.data.data() + size_t(y) * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      raw.push_back(row[4 * x]);
      raw.push_back(row[4 * x + 1]);
      raw.push_back(row[4 * x + 2]);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t pos = 0; pos < raw.size();) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n));
    z.push_back(uint8_t((~n) >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  PutBe32(z, (b << 16) | a);
  Chunk(f, "IDAT", z);
  Chunk(f, "IEND", {});
  return true;
}

// Fraction of pixels that are not black, and the distinct colours among a sparse sample: tells "a picture" from
// "a black or flat frame" in a log line.
void Stats(const rex::ui::RawImage& image, double& non_black, size_t& colours) {
  size_t nb = 0, total = 0;
  std::vector<uint32_t> seen;
  for (uint32_t y = 0; y < image.height; y += 4) {
    const uint8_t* row = image.data.data() + size_t(y) * image.stride;
    for (uint32_t x = 0; x < image.width; x += 4) {
      const uint32_t c = (uint32_t(row[4 * x]) << 16) | (uint32_t(row[4 * x + 1]) << 8) | row[4 * x + 2];
      ++total;
      if (c) ++nb;
      if (seen.size() < 4096) seen.push_back(c);
    }
  }
  std::sort(seen.begin(), seen.end());
  colours = size_t(std::unique(seen.begin(), seen.end()) - seen.begin());
  non_black = total ? double(nb) / double(total) : 0.0;
}

void Run(std::function<rex::ui::Presenter*()> get_presenter) {
  const auto folder = rex::filesystem::GetExecutableFolder() / "captures";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto started = std::chrono::steady_clock::now();
  const int shot_s = REXCVAR_GET(bakugan_shot_s);
  const int shots = std::max(1, REXCVAR_GET(bakugan_shot_count));
  const int every = std::max(1, REXCVAR_GET(bakugan_shot_every_s));
  const int video_s = REXCVAR_GET(bakugan_video_s);
  const int video_frames = REXCVAR_GET(bakugan_video_frames);

  auto grab = [&](rex::ui::RawImage& image) {
    rex::ui::Presenter* presenter = get_presenter ? get_presenter() : nullptr;
    return presenter && presenter->CaptureGuestOutput(image) && image.width && image.height;
  };

  // Snapshots.
  if (shot_s > 0) {
    for (int i = 0; i < shots; ++i) {
      const auto at = started + std::chrono::seconds(shot_s + i * every);
      const auto now = std::chrono::steady_clock::now();
      if (at > now && Wait(std::chrono::duration_cast<std::chrono::milliseconds>(at - now))) return;
      rex::ui::RawImage image;
      if (!grab(image)) {
        REXLOG_WARN("[capture] t={} s: no guest output yet", shot_s + i * every);
        continue;
      }
      double non_black = 0;
      size_t colours = 0;
      Stats(image, non_black, colours);
      const auto path = folder / ("shot_" + std::to_string(shot_s + i * every) + "s.png");
      const bool ok = WritePng(path, image);
      REXLOG_INFO("[capture] t={} s: {}x{}, {:.1f}% non-black, {} colours -> {}{}", shot_s + i * every, image.width,
                  image.height, non_black * 100.0, colours, rex::path_to_utf8(path), ok ? "" : " (write failed)");
    }
  }

  // Video.
  if (video_s > 0) {
    const auto at = started + std::chrono::seconds(video_s);
    const auto now = std::chrono::steady_clock::now();
    if (at > now && Wait(std::chrono::duration_cast<std::chrono::milliseconds>(at - now))) return;
    const uint32_t step = REXCVAR_GET(bakugan_video_half) ? 2 : 1;
    const std::string name = "video_" + std::to_string(video_s) + "s";
    std::ofstream raw(folder / (name + ".rgba"), std::ios::binary | std::ios::trunc);
    int written = 0;
    uint32_t w = 0, h = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> frame;
    while (written < video_frames) {
      if (Wait(std::chrono::milliseconds(33))) break;
      rex::ui::RawImage image;
      if (!grab(image)) continue;
      if (!w) {
        w = image.width / step;
        h = image.height / step;
      }
      if (image.width / step != w || image.height / step != h) continue;
      frame.resize(size_t(w) * h * 4);
      for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = image.data.data() + size_t(y) * step * image.stride;
        for (uint32_t x = 0; x < w; ++x) std::memcpy(&frame[(size_t(y) * w + x) * 4], row + size_t(x) * step * 4, 4);
      }
      raw.write(reinterpret_cast<const char*>(frame.data()), std::streamsize(frame.size()));
      ++written;
    }
    raw.close();
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (written) {
      std::ofstream(folder / (name + ".txt"), std::ios::binary)
          << w << ' ' << h << ' ' << (s > 0 ? written / s : 30.0) << ' ' << written << '\n';
    }
    REXLOG_INFO("[capture] video {}: {} frames of {}x{} in {:.1f} s", name, written, w, h, s);
  }
}

}  // namespace

void Start(std::function<rex::ui::Presenter*()> get_presenter) {
  if ((REXCVAR_GET(bakugan_shot_s) <= 0 && REXCVAR_GET(bakugan_video_s) <= 0) || g_thread.joinable()) {
    return;
  }
  {
    std::lock_guard lock(g_mutex);
    g_stop = false;
  }
  g_thread = std::thread(Run, std::move(get_presenter));
}

void Stop() {
  {
    std::lock_guard lock(g_mutex);
    g_stop = true;
  }
  g_cv.notify_all();
  if (g_thread.joinable()) {
    g_thread.join();
  }
}

}  // namespace bakugan::capture
