# SD card layout (sdmc:/switch/bakugan-nx/)

This folder is the layout of a release. The binaries are NOT in the repository (they contain the game's recompiled code and shaders): build them (docs/building.md) and tools/win/package_release.ps1 puts them here-equivalent in out/nx/sdcard_release.

    bakugan.nro                 built from this source
    bakugan_shaders.mesp(.idx)  made from your disc
    bakugan.toml                the player's settings (this file; written at the first start when missing)
    game_root/                  default.xex and media/ of your disc
    cache/bakugan_native_pipelines.bin   optional, made by the console while playing
