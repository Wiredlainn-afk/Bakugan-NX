/**
 * @file        core/system_posix.cpp
 * @brief       POSIX implementations of rex/system.h platform helpers.
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <cstdio>
#include <cstring>

#include <rex/system.h>

// The last error box text, for start-up failures the Switch entry point shows on a text screen
// (ui/switch_fatal_screen.cpp).
extern "C" {
char g_rex_last_error_message[512] = {};
}

namespace rex {

// TODO(tomc): add linux support for showing a native message box
void ShowSimpleMessageBox(SimpleMessageBoxType type, std::string_view message) {
  const char* level = "INFO";
  switch (type) {
    case SimpleMessageBoxType::Help:
      level = "INFO";
      break;
    case SimpleMessageBoxType::Warning:
      level = "WARNING";
      break;
    case SimpleMessageBoxType::Error:
      level = "ERROR";
      break;
  }
  if (type == SimpleMessageBoxType::Error) {
    std::snprintf(g_rex_last_error_message, sizeof(g_rex_last_error_message), "%.*s", static_cast<int>(message.size()),
                  message.data());
  }
  std::fprintf(stderr, "[%s] %.*s\n", level, static_cast<int>(message.size()), message.data());
  std::fflush(stderr);
}

}  // namespace rex
