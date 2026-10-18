/**
 * The Switch's own software keyboard for the games' XamShowKeyboardUI (name entries and the like) (2026-10-12).
 *
 * The desktop path draws an ImGui dialog, which a Switch build does not have: the game got its default text back
 * at once and its own text box could not be typed into (Bakugan's story-mode character name). Here the keyboard
 * applet (libnx swkbd) is shown instead. Kept in its own file because switch.h brings u8/Result into the global
 * namespace.
 */

#if defined(__SWITCH__)

#include <switch.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace rex::kernel::xam {

// header / guide / initial are UTF-8; the entered text (UTF-8) goes to *out. false: cancelled or the applet failed.
bool ShowSwitchKeyboard(const std::string& header, const std::string& guide, const std::string& initial,
                        size_t max_chars, std::string* out) {
  SwkbdConfig kbd;
  if (R_FAILED(swkbdCreate(&kbd, 0))) {
    return false;
  }
  swkbdConfigMakePresetDefault(&kbd);
  if (!header.empty()) swkbdConfigSetHeaderText(&kbd, header.c_str());
  if (!guide.empty()) swkbdConfigSetGuideText(&kbd, guide.c_str());
  if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());
  if (max_chars) swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(std::min<size_t>(max_chars, 500)));
  char text[2048];
  std::memset(text, 0, sizeof(text));
  const Result rc = swkbdShow(&kbd, text, sizeof(text));
  swkbdClose(&kbd);
  if (R_FAILED(rc)) {
    return false;
  }
  *out = text;
  return true;
}

}  // namespace rex::kernel::xam

#endif  // __SWITCH__
