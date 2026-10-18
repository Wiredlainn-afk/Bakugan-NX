/**
 * @file        ui/switch_fatal_screen.cpp
 * @brief       A text screen for start-up failures on Nintendo Switch (missing game files and the like).
 *
 * Before this, a start that failed (no game_root next to the NRO, for instance) ended in a black screen and a console
 * that looked frozen until the system closed the process. RexSwitchShowFatalScreen prints the reason on the libnx text
 * console and returns when the player presses + or B (or after a minute), so the process can exit normally.
 *
 * @license     BSD 3-Clause License
 */

#include <cstdio>
#include <cstring>

#include <switch.h>

extern "C" void RexSwitchShowFatalScreen(const char* reason) {
  consoleInit(nullptr);
  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeAny(&pad);

  std::printf("\n  Bakugan: Battle Brawlers could not start.\n\n");
  if (reason && reason[0]) {
    // "--game_data_root does not exist: <the reason>" carries the app's own text after the colon.
    const char* tail = std::strstr(reason, "does not exist: ");
    std::printf("  %s\n\n", tail ? tail + 16 : reason);
  }
  std::printf("  The game's own files must be in a folder named game_root next to bakugan.nro:\n\n");
  std::printf("    sdmc:/switch/bakugan-nx/game_root/default.xex\n");
  std::printf("    sdmc:/switch/bakugan-nx/game_root/media/...\n\n");
  std::printf("  Copy your extracted disc there (the release package does not include the game).\n");
  std::printf("  Also needed next to the NRO: bakugan_shaders.mesp and bakugan_shaders.mesp.idx.\n\n");
  std::printf("  Press + or B to exit.\n");
  consoleUpdate(nullptr);

  // At most a minute: the screen never keeps the console waiting for good.
  for (int frame = 0; frame < 60 * 60 && appletMainLoop(); ++frame) {
    padUpdate(&pad);
    const u64 down = padGetButtonsDown(&pad);
    if (down & (HidNpadButton_Plus | HidNpadButton_B)) {
      break;
    }
    consoleUpdate(nullptr);
  }
  consoleExit(nullptr);
}
