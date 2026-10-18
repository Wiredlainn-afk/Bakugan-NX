// bakugan-nx - flight recorder: one line per second in logs/flight.txt, written and fsync'd as it happens.
//
// Why: the console sometimes becomes unresponsive (a hang the game log cannot explain: its last lines are ordinary
// reports, and the SD card keeps the old data of the unwritten sectors). A line per second that is synced to the
// card says what the app was doing in its last second: frames presented, the GPU wait it was inside (and for how
// long), the last file the game opened and the memory of the application pool.
#pragma once

namespace bakugan::flight {

// The renderer presented a frame (ring thread, once per Swap).
void NotePresented();

// A blocking GPU wait (vkWaitForFences with no timeout, queue drains): while in scope the recorder names the site.
struct WaitScope {
  explicit WaitScope(const char* site);
  ~WaitScope();
  WaitScope(const WaitScope&) = delete;
  WaitScope& operator=(const WaitScope&) = delete;

 private:
  const char* previous_;
  long long previous_since_;
};

}  // namespace bakugan::flight
