// bakugan - WMV movies decoded natively with FFmpeg (see bakugan_video_wmv3.h)
//
// ASF container (the minimum for these files)
//  - Header: child objects with GUID and size. File properties (packet size and packet count), the
//    properties of each stream (the video one carries a BITMAPINFOHEADER with the width, the height
//    and the 4 WMV3 sequence bytes after it) and the data object, whose packets start 50 bytes later.
//  - Fixed-size packets with one or more payloads. Each payload is a piece of a media object (a
//    frame) with its number, its offset within the object and, in the replicated data, the total size
//    and the time. A frame is complete once all its bytes have been received.
//  - 1-byte replicated data = compressed payloads: several small objects in a row, each with its size
//    in one byte.
// Without B frames, each compressed frame yields one output frame in the same order.

#include "bakugan_video_wmv3.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <span>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

extern "C" {
#include "libavcodec/avcodec.h"
}

REXCVAR_DEFINE_BOOL(bakugan_video_wmv3_simd, true, "BAKUGAN enhancements",
                    "FFmpeg WMV3 decoder uses its SIMD (NEON/SSE) routines; false = plain C (diagnostic)");

namespace bakugan::video_wmv3 {
namespace {

constexpr uint8_t kGuidCabecera[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                       0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
constexpr uint8_t kGuidFichero[16] = {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                                      0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
constexpr uint8_t kGuidFlujo[16] = {0x91, 0x07, 0xDC, 0xB7, 0xB7, 0xA9, 0xCF, 0x11,
                                    0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
constexpr uint8_t kGuidVideo[16] = {0xC0, 0xEF, 0x19, 0xBC, 0x4D, 0x5B, 0xCF, 0x11,
                                    0xA8, 0xFD, 0x00, 0x80, 0x5F, 0x5C, 0x44, 0x2B};
constexpr uint8_t kGuidDatos[16] = {0x36, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                    0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
constexpr size_t kMaxCabecera = 512 * 1024;

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }
uint64_t Le64(const uint8_t* p) { return uint64_t(Le32(p)) | (uint64_t(Le32(p + 4)) << 32); }

// ASF variable-length field: 0, 1, 2 or 4 bytes depending on the type (0-3).
bool LeerVariable(const std::vector<uint8_t>& b, size_t& p, int tipo, uint32_t& valor) {
  static constexpr int kBytes[4] = {0, 1, 2, 4};
  const int n = kBytes[tipo & 3];
  if (p + size_t(n) > b.size()) {
    return false;
  }
  valor = n == 0 ? 0 : n == 1 ? b[p] : n == 2 ? Le16(&b[p]) : Le32(&b[p]);
  p += size_t(n);
  return true;
}

struct Objeto {
  std::vector<uint8_t> datos;
  uint32_t recibidos = 0;
  bool key = false;
  uint32_t number = 0;  // media object number (ASF)
};

// File opened through the SDK's VFS (folder or ISO).
struct FicheroVfs {
  rex::filesystem::File* f = nullptr;
  uint64_t tamano = 0;

  ~FicheroVfs() {
    if (f) {
      f->Destroy();
    }
  }

  bool Open(const std::string& ruta) {
    rex::filesystem::FileAction accion;
    const auto estado = REX_KERNEL_FS()->OpenFile(nullptr, ruta, rex::filesystem::FileDisposition::kOpen,
                                                  rex::filesystem::FileAccess::kGenericRead, false, true, &f,
                                                  &accion);
    if (estado != 0 || !f) {
      REXLOG_WARN("[video] native WMV3: no se puede abrir '{}' ({:08X})", ruta, uint32_t(estado));
      f = nullptr;
      return false;
    }
    tamano = f->entry()->size();
    return true;
  }

  bool Leer(uint64_t desplazamiento, uint8_t* destino, size_t bytes) {
    size_t leidos = 0;
    const auto estado = f->ReadSync(std::span<uint8_t>(destino, bytes), size_t(desplazamiento), &leidos);
    return estado == 0 && leidos == bytes;
  }
};

struct Contenedor {
  WmvInfo info;
  uint64_t inicio_datos = 0;
  uint64_t paquetes = 0;
  uint32_t tamano_paquete = 0;
  uint32_t flujo_video = 0;
};

bool LeerCabecera(FicheroVfs& fichero, const std::string& ruta, Contenedor& c) {
  std::vector<uint8_t> cabecera(size_t(std::min<uint64_t>(fichero.tamano, kMaxCabecera)));
  if (cabecera.size() < 128 || !fichero.Leer(0, cabecera.data(), cabecera.size()) ||
      std::memcmp(cabecera.data(), kGuidCabecera, 16) != 0) {
    REXLOG_WARN("[video] native WMV3: '{}' no empieza por una cabecera ASF", ruta);
    return false;
  }
  const uint64_t tam_cabecera = Le64(&cabecera[16]);
  size_t p = 30;
  bool video = false;
  while (p + 24 <= cabecera.size() && p < tam_cabecera) {
    const uint8_t* o = &cabecera[p];
    const uint64_t tam = Le64(o + 16);
    if (tam < 24 || p + tam > cabecera.size()) {
      break;
    }
    if (std::memcmp(o, kGuidFichero, 16) == 0 && tam >= 104) {
      c.paquetes = Le64(o + 56);
      c.tamano_paquete = Le32(o + 92);
    } else if (std::memcmp(o, kGuidFlujo, 16) == 0 && tam >= 78 + 11 + 40 && !video &&
               std::memcmp(o + 24, kGuidVideo, 16) == 0) {
      c.flujo_video = Le16(o + 72) & 0x7F;
      const uint8_t* bmi = o + 78 + 11;
      const uint32_t tam_bmi = Le32(bmi);
      c.info.width = int(Le32(bmi + 4));
      c.info.height = std::abs(int(Le32(bmi + 8)));
      if (tam_bmi > 40 && 78 + 11 + uint64_t(tam_bmi) <= tam) {
        c.info.sequence.assign(bmi + 40, bmi + tam_bmi);
      }
      video = std::memcmp(bmi + 16, "WMV3", 4) == 0;
    }
    p += size_t(tam);
  }
  if (!video || !c.tamano_paquete || !c.info.width || !c.info.height) {
    REXLOG_WARN("[video] native WMV3: '{}' sin flujo WMV3 utilizable (paquete {}, {}x{})", ruta, c.tamano_paquete,
                c.info.width, c.info.height);
    return false;
  }
  // The data object comes right after the header; its packets start 50 bytes later.
  std::vector<uint8_t> datos(50);
  if (!fichero.Leer(tam_cabecera, datos.data(), datos.size()) || std::memcmp(datos.data(), kGuidDatos, 16) != 0) {
    REXLOG_WARN("[video] native WMV3: '{}' sin objeto de datos detras de la cabecera", ruta);
    return false;
  }
  c.inicio_datos = tam_cabecera + 50;
  return true;
}

}  // namespace

bool ReadWmvInfo(const std::string& ruta, WmvInfo& info) {
  FicheroVfs fichero;
  Contenedor c;
  if (!fichero.Open(ruta) || !LeerCabecera(fichero, ruta, c)) {
    return false;
  }
  info = std::move(c.info);
  return true;
}

// --- Wmv3Decoder ------------------------------------------------------------------------------------

struct Wmv3Decoder::Estado {
  AVCodecContext* codec = nullptr;
  AVPacket* pkt = nullptr;
  AVFrame* frame = nullptr;

  ~Estado() {
    if (frame) {
      av_frame_free(&frame);
    }
    if (pkt) {
      av_packet_free(&pkt);
    }
    if (codec) {
      avcodec_free_context(&codec);
    }
  }
};

Wmv3Decoder::Wmv3Decoder() : e_(std::make_unique<Estado>()) {}
Wmv3Decoder::~Wmv3Decoder() = default;

bool Wmv3Decoder::Open(const WmvInfo& info) {
  const AVCodec* wmv3 = avcodec_find_decoder(AV_CODEC_ID_WMV3);
  if (!wmv3) {
    REXLOG_ERROR("[video] native WMV3: FFmpeg sin descodificador WMV3");
    return false;
  }
  e_ = std::make_unique<Estado>();
  e_->codec = avcodec_alloc_context3(wmv3);
  e_->codec->width = e_->codec->coded_width = info.width;
  e_->codec->height = e_->codec->coded_height = info.height;
  e_->codec->thread_count = 1;
  // Each frame comes out of the call that decodes it, without reordering. With B frames FFmpeg would otherwise
  // return the previous widthr (one frame late), and the game expects the picture of what it just asked for.
  // Without B frames nothing changes: FFmpeg's VC-1 decoder already has no delay (vc1dec.c, low_delay).
  e_->codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  if (!info.sequence.empty()) {
    e_->codec->extradata =
        static_cast<uint8_t*>(av_mallocz(info.sequence.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    std::memcpy(e_->codec->extradata, info.sequence.data(), info.sequence.size());
    e_->codec->extradata_size = int(info.sequence.size());
  }
  // Diagnostic: without SIMD the decoder initialises its plain C routines (the DSP tables are chosen in open).
  const bool simd = REXCVAR_GET(bakugan_video_wmv3_simd);
  if (!simd) av_force_cpu_flags(0);
  const int open_result = avcodec_open2(e_->codec, wmv3, nullptr);
  if (!simd) av_force_cpu_flags(-1);
  if (open_result < 0) {
    REXLOG_ERROR("[video] native WMV3: avcodec_open2 fallo ({}x{})", info.width, info.height);
    e_ = std::make_unique<Estado>();
    return false;
  }
  // With B frames, vc1dec.c computes low_delay again at the first frame from has_b_frames (1 when the sequence
  // allows B frames), and the first I frame did not come out (EAGAIN): the video had no picture until the next I
  // frame, 4 s later. With has_b_frames at 0, low_delay stays at 1 from the first frame.
  e_->codec->has_b_frames = 0;
  e_->pkt = av_packet_alloc();
  e_->frame = av_frame_alloc();
  width_ = info.width;
  height_ = info.height;
  fotogramas_ = 0;
  return true;
}

bool Wmv3Decoder::Decode(const uint8_t* datos, size_t bytes, bool key, Frame& salida) {
  if (!e_->codec || !bytes) {
    return false;
  }
  av_packet_unref(e_->pkt);
  if (av_new_packet(e_->pkt, int(bytes)) < 0) {
    return false;
  }
  std::memcpy(e_->pkt->data, datos, bytes);
  e_->pkt->flags = key ? AV_PKT_FLAG_KEY : 0;
  int r = avcodec_send_packet(e_->codec, e_->pkt);
  if (r < 0) {
    REXLOG_WARN("[video] native WMV3: avcodec_send_packet {} en el fotograma {} ({} bytes)", r, fotogramas_, bytes);
    return false;
  }
  r = avcodec_receive_frame(e_->codec, e_->frame);
  if (r < 0) {
    REXLOG_WARN("[video] native WMV3: avcodec_receive_frame {} en el fotograma {} ({} bytes)", r, fotogramas_,
                bytes);
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    salida.planes[i] = e_->frame->data[i];
    salida.strides[i] = e_->frame->linesize[i];
  }
  salida.width = e_->frame->width;
  salida.height = e_->frame->height;
  salida.key = key;
  salida.bytes = uint32_t(bytes);
  ++fotogramas_;
  return true;
}

// --- WmvMovie (diagnostico) ------------------------------------------------------------------------------

struct WmvMovie::Estado {
  FicheroVfs fichero;
  Contenedor c;
  uint64_t siguiente_paquete = 0;
  std::vector<uint8_t> paquete;
  std::map<uint32_t, Objeto> en_curso;
  std::deque<Objeto> listos;
  Wmv3Decoder descodificador;

  // Adds a payload to its media object; once complete, the object moves to the ready queue.
  void Carga(uint32_t objeto, uint32_t desplazamiento, uint32_t tamano_objeto, bool key, const uint8_t* datos,
             uint32_t bytes) {
    auto& o = en_curso[objeto];
    if (o.datos.empty()) {
      o.datos.assign(tamano_objeto, 0);
      o.key = key;
      o.number = objeto;
    }
    if (uint64_t(desplazamiento) + bytes > o.datos.size()) {
      en_curso.erase(objeto);  // inconsistent piece: the object is dropped
      return;
    }
    std::memcpy(o.datos.data() + desplazamiento, datos, bytes);
    o.recibidos += bytes;
    if (o.recibidos >= o.datos.size()) {
      listos.push_back(std::move(o));
      en_curso.erase(objeto);
    }
  }

  bool LeerPaquete() {
    if (siguiente_paquete >= c.paquetes) {
      return false;
    }
    paquete.resize(c.tamano_paquete);
    if (!fichero.Leer(c.inicio_datos + siguiente_paquete * uint64_t(c.tamano_paquete), paquete.data(),
                      c.tamano_paquete)) {
      return false;
    }
    ++siguiente_paquete;
    const auto& b = paquete;
    size_t p = 0;
    const uint8_t ec = b[p];
    if (ec & 0x80) {
      p += 1 + (ec & 0x0F);
    }
    if (p + 2 > b.size()) {
      return false;
    }
    const uint8_t tipos = b[p];
    const uint8_t propiedades = b[p + 1];
    p += 2;
    uint32_t longitud_paquete = 0, sequence = 0, relleno = 0;
    if (!LeerVariable(b, p, (tipos >> 5) & 3, longitud_paquete) || !LeerVariable(b, p, (tipos >> 1) & 3, sequence) ||
        !LeerVariable(b, p, (tipos >> 3) & 3, relleno)) {
      return false;
    }
    p += 6;  // send time and duration
    const bool multiples = tipos & 1;
    int n_cargas = 1;
    int tipo_longitud_carga = 0;
    if (multiples) {
      if (p >= b.size()) {
        return false;
      }
      n_cargas = b[p] & 0x3F;
      tipo_longitud_carga = (b[p] >> 6) & 3;
      ++p;
    }
    const size_t fin = std::min<size_t>(b.size(), (longitud_paquete ? longitud_paquete : c.tamano_paquete)) -
                       std::min<size_t>(relleno, b.size());
    for (int i = 0; i < n_cargas && p < fin; ++i) {
      const uint8_t numero = b[p++];
      const uint32_t flujo = numero & 0x7F;
      const bool key = numero & 0x80;
      uint32_t objeto = 0, desplazamiento = 0, replicados = 0;
      if (!LeerVariable(b, p, (propiedades >> 4) & 3, objeto) ||
          !LeerVariable(b, p, (propiedades >> 2) & 3, desplazamiento) ||
          !LeerVariable(b, p, propiedades & 3, replicados) || p + replicados > b.size()) {
        return false;
      }
      const size_t pos_replicados = p;
      p += replicados;
      uint32_t bytes = 0;
      if (multiples) {
        if (!LeerVariable(b, p, tipo_longitud_carga, bytes)) {
          return false;
        }
      } else {
        bytes = uint32_t(fin > p ? fin - p : 0);
      }
      if (p + bytes > b.size()) {
        return false;
      }
      if (flujo == c.flujo_video) {
        if (replicados >= 8) {
          Carga(objeto, desplazamiento, Le32(&b[pos_replicados]), key, &b[p], bytes);
        } else if (replicados == 1) {
          // Compressed payloads: whole objects in a row, each preceded by its size in one byte.
          size_t q = p;
          uint32_t sub = objeto;
          while (q < p + bytes) {
            const uint32_t n = b[q++];
            if (q + n > p + bytes) {
              break;
            }
            Carga(sub++, 0, n, key, &b[q], n);
            q += n;
          }
        }
      }
      p += bytes;
    }
    return true;
  }
};

WmvMovie::WmvMovie() : e_(std::make_unique<Estado>()) {}
WmvMovie::~WmvMovie() = default;

bool WmvMovie::Open(const std::string& ruta) {
  ruta_ = ruta;
  if (!e_->fichero.Open(ruta) || !LeerCabecera(e_->fichero, ruta, e_->c)) {
    return false;
  }
  info_ = e_->c.info;
  if (!e_->descodificador.Open(info_)) {
    return false;
  }
  const auto& s = info_.sequence;
  REXLOG_INFO("[video] native WMV3: '{}' {}x{}, {} paquetes de {} bytes, flujo {}, sequence {:02X}{:02X}{:02X}{:02X}",
              ruta, info_.width, info_.height, e_->c.paquetes, e_->c.tamano_paquete, e_->c.flujo_video,
              s.size() > 0 ? s[0] : 0, s.size() > 1 ? s[1] : 0, s.size() > 2 ? s[2] : 0, s.size() > 3 ? s[3] : 0);
  return true;
}

bool WmvMovie::Next(Frame& salida) {
  while (e_->listos.empty()) {
    if (!e_->LeerPaquete()) {
      return false;
    }
  }
  Objeto o = std::move(e_->listos.front());
  e_->listos.pop_front();
  if (!e_->descodificador.Decode(o.datos.data(), o.datos.size(), o.key, salida)) {
    return false;
  }
  ++fotogramas_;
  return true;
}

bool WmvMovie::NextRaw(std::vector<uint8_t>& data, bool& key, uint32_t& number) {
  while (e_->listos.empty()) {
    if (!e_->LeerPaquete()) {
      return false;
    }
  }
  Objeto o = std::move(e_->listos.front());
  e_->listos.pop_front();
  data = std::move(o.datos);
  key = o.key;
  number = o.number;
  return true;
}

bool WmvMovie::DecodeRaw(const std::vector<uint8_t>& data, bool key, Frame& salida) {
  if (!e_->descodificador.Decode(data.data(), data.size(), key, salida)) {
    return false;
  }
  ++fotogramas_;
  return true;
}

}  // namespace bakugan::video_wmv3
