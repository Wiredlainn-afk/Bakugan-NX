// BAKUGAN - native renderer: access to the shader library the native graphics system has loaded.

#pragma once

namespace bakugan::native {

class ShadersNative;

// The library the native graphics system loaded (nullptr until then), from any thread. Used for pipeline
// prewarming (bakugan_native_draws.cpp).
const ShadersNative* ActiveLibrary();

}  // namespace bakugan::native
