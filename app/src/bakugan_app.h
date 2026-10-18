// Bakugan: Battle Brawlers: the ReXGlue application object (paths, GPU plugin choice, analysis aids).

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/ui/presenter.h>

#include "bakugan_capture.h"
#include "bakugan_pgo.h"
#include "cpu/bakugan_enhancements.h"
#include "me/me_native_system.h"
#include "me/bakugan_render_mode.h"
#include "profiler/bakugan_cpu_profile_pc.h"

#if defined(BAKUGAN_BUILTIN_CONFIG)
#include "bakugan_builtin_config.h"  // generated from app/bakugan_switch_defaults.toml (CMake)
#endif

namespace bakugan_game {

// The retail disc (Xbox 360, title 41560857) this port was recompiled from: the sizes of its executable and of the two
// archives in media/. A different release or a partial copy fails here with a message the Switch shows on screen,
// instead of starting and misbehaving.
constexpr std::uintmax_t kXexSize = 11055104;
constexpr std::uintmax_t kArcSize = 2510774272;
constexpr std::uintmax_t kBinSize = 387296;

inline std::string Problem(const std::filesystem::path& root) {
  std::error_code ec;
  const auto size_of = [&](const std::filesystem::path& file) -> std::uintmax_t {
    if (!std::filesystem::is_regular_file(file, ec)) return UINTMAX_MAX;
    const auto size = std::filesystem::file_size(file, ec);
    return ec ? UINTMAX_MAX : size;
  };
  const auto xex = size_of(root / "default.xex");
  if (xex != kXexSize) {
    return "Wrong game files: default.xex is not the Bakugan: Battle Brawlers (Xbox 360) executable this port needs ("
           + (xex == UINTMAX_MAX ? std::string("missing") : std::to_string(xex) + " bytes") + ", expected "
           + std::to_string(kXexSize) + ").";
  }
  // With the asset cache (<root>.nxpack, see pack_device.h) the archives are inside the pack, not in media/.
  if (std::filesystem::is_regular_file(std::filesystem::path(root.string() + ".nxpack"), ec)) return {};
  const auto arc = size_of(root / "media" / "d00.arc");
  if (arc != kArcSize) {
    return "Wrong or incomplete game files: media/d00.arc is "
           + (arc == UINTMAX_MAX ? std::string("missing") : std::to_string(arc) + " bytes") + ", expected "
           + std::to_string(kArcSize) + ".";
  }
  const auto bin = size_of(root / "media" / "d00.bin");
  if (bin != kBinSize) {
    return "Wrong or incomplete game files: media/d00.bin is "
           + (bin == UINTMAX_MAX ? std::string("missing") : std::to_string(bin) + " bytes") + ", expected "
           + std::to_string(kBinSize) + ".";
  }
  return {};
}

}  // namespace bakugan_game

class BakuganApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BakuganApp>(new BakuganApp(ctx, "bakugan", PPCImageConfig));
  }

  // The Switch build's fixed settings (app/bakugan_switch_defaults.toml, compiled in): applied before bakugan.toml,
  // which holds only the player's options (and may still override any of these).
  std::string_view BuiltInConfig() const override {
#if defined(BAKUGAN_BUILTIN_CONFIG)
    return kBakuganBuiltInConfig;
#else
    return {};
#endif
  }
  // Without a bakugan.toml next to the NRO, the player's default one is written there (app/bakugan_switch.toml).
  std::string_view DefaultConfigFile() const override {
#if defined(BAKUGAN_BUILTIN_CONFIG)
    return kBakuganPlayerConfig;
#else
    return {};
#endif
  }

  void OnPostInitLogging() override {
    // The runtime defaults to no GPU plugin: select ReXGlue's Xenos implementation, keeping an explicit
    // command-line or config-file choice.
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
    // Which enhancements run (own key + group master), in one line; before any guest thread exists.
    bakugan::enh::Resolve();
  }

  // Stage 4: with bakugan_renderer_native = true the graphics system is the app's own native Vulkan renderer
  // (src/me), and the emulated Xenos GPU plugin is not loaded (ReXApp only loads it when config.graphics is empty).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    bakugan::enh::ApplyGpuBoost();
    if (me::native::Enabled()) {
      config.graphics = me::native::CreateGraphicsSystem();
      REXLOG_INFO("graphics: native Vulkan renderer (PM4 ring)");
    }
  }

  // Without --game_data_root, use the game_root/ folder next to the executable or, on a PC build tree, the
  // project's assets/game_root. An explicit path always wins.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      return;
    }
    std::error_code ec;
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (folder.empty()) {
      return;
    }
    for (auto candidate = folder; !candidate.empty(); candidate = candidate.parent_path()) {
      for (const auto& sub : {std::filesystem::path("game_root"),
                              std::filesystem::path("assets") / "game_root"}) {
        if (std::filesystem::is_regular_file(candidate / sub / "default.xex", ec)) {
          // A folder with the wrong or incomplete files is handed over as a path that does not exist, carrying the
          // reason: the start then fails with it, and the Switch shows it on the text screen.
          const std::string problem = bakugan_game::Problem(candidate / sub);
          paths.game_data_root = problem.empty() ? (candidate / sub) : std::filesystem::path(problem);
          return;
        }
      }
      if (candidate == candidate.parent_path()) {
        break;
      }
    }
  }

  // Test captures (PNG snapshots / raw video) from the presenter's guest output, so they work minimized.
  void OnPostSetup() override {
    // PC only: the sampling CPU profiler and the stack dump.
    nfsmw::perfil_pc::Arrancar();
    // The key that simulates docking / undocking (render resolution), PC only.
    bakugan::render::RegisterKeys();
    bakugan::capture::Start([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphics = rt ? rt->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
    // The PGO-instrumented build (BAKUGAN_PGO=generate) saves its counts to pgo/ every 2 minutes (nothing otherwise).
    bakugan::pgo::Start();
  }

  // The PGO-instrumented build saves its counts when the game loses focus (HOME menu): it may be closed from there.
  void OnWindowFocusChanged(bool focused) override {
    if (!focused) bakugan::pgo::DumpNow();
  }

  void OnShutdown() override {
    nfsmw::perfil_pc::Parar();
    bakugan::capture::Stop();
    bakugan::pgo::Stop();
  }

  // Analysis aid: with BAKUGAN_DUMP_IMAGE=<file>, write the loaded guest image (what the running game sees, after
  // the loader patched the imports).
  void OnPostLoadXexImage() override {
    if (!runtime() || !runtime()->memory()) {
      return;
    }
    const char* path = std::getenv("BAKUGAN_DUMP_IMAGE");
    if (!path) {
      return;
    }
    const uint8_t* image =
        runtime()->memory()->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base);
    if (std::FILE* f = std::fopen(path, "wb")) {
      std::fwrite(image, 1, PPCImageConfig.image_size, f);
      std::fclose(f);
      REXLOG_INFO("guest image written to {}", path);
    }
  }
};
