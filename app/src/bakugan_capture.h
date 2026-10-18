// Test captures of the game image, taken from the presenter's guest output (what the game presents), so they work
// with the window minimized and nothing can cover them:
//   bakugan_shot_s / bakugan_shot_count / bakugan_shot_every_s   PNG snapshots (captures/shot_<n>.png)
//   bakugan_video_s / bakugan_video_frames                       raw R8G8B8X8 video (captures/video_<s>s.rgba + .txt)

#pragma once

#include <functional>

namespace rex::ui {
class Presenter;
}

namespace bakugan::capture {

// Starts the capture thread when a snapshot or a video was asked for.
void Start(std::function<rex::ui::Presenter*()> get_presenter);

// Stops it (writes what it has, if anything).
void Stop();

}  // namespace bakugan::capture
