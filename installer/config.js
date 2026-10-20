// Everything the installer page needs to know about the project lives here. Change a name, a URL, an edition or a
// wasm file name in this file only; nothing else in the page hard-codes them.

export const CONFIG = {
  project: {
    title: 'Bakugan: Battle Brawlers for Nintendo Switch',
    author: 'Bakugan-NX',
    repoUrl: 'https://github.com/Wiredlainn-afk/Bakugan-NX',
    releasesUrl: 'https://github.com/Wiredlainn-afk/Bakugan-NX/releases',
    issuesUrl: 'https://github.com/Wiredlainn-afk/Bakugan-NX/issues',
  },

  // Names inside the zip. The zip is extracted into sdmc:/switch/, so `root` becomes sdmc:/switch/bakugan-nx/.
  zip: {
    root: 'bakugan-nx',
    fullName: 'bakugan-nx.zip',
    updateName: 'bakugan-nx-update.zip',
    gameRootDir: 'game_root',
  },

  // 'embedded' (0.2.0 and later, Renut on deko3d): the game's shaders are compiled into the NRO, so the page makes no shader
  // package (no disc scan, no translation, no packing) and the WebAssembly tools are not needed. 'disc' (0.1.x, the native
  // renderer): the page makes bakugan_shaders.mesp from the player's disc.
  shaderMode: 'embedded',

  // Files of older builds that the current one does not read and that the player should delete from the SD card (shown on the page).
  obsoleteFiles: [
    'bakugan_shaders.mesp',
    'bakugan_shaders.mesp.idx',
    'cache/bakugan_native_pipelines.bin',
    'bakugan.toml (an old one can select the old renderer; the 0.2.0 defaults are compiled in)',
    'every other file in cache/ (old pipeline and shader caches)',
  ],

  // File names inside <zip.root>/. The shader package names are fixed by the game (shaders/README.md); used in 'disc' mode.
  files: {
    nro: 'bakugan.nro',
    // The player's settings are written by the NRO at the first start when missing; the release has no toml.
    toml: null,
    shaders: 'bakugan_shaders.mesp',
    shadersIndex: 'bakugan_shaders.mesp.idx',
    // Optional: the warmed pipeline cache of the release (the console keeps growing its own).
    pipelineCache: 'cache/bakugan_native_pipelines.bin',
  },

  // Where the page gets the build. GitHub release assets cannot be fetched by a browser from another origin, so the
  // deploy workflow copies the latest release's assets into the site, next to the page, and writes `manifest.json`.
  build: {
    siteDir: 'releases/',
    manifest: 'releases/manifest.json',
  },

  // Known game editions, recognised by the SHA-256 of default.xex. The NRO is the recompiled program of one exact
  // default.xex, so only editions listed here can be installed.
  //   nro / pipelineCache: the asset names in the release (and in siteDir).
  //   cacheFiles (shaderMode 'embedded'): release assets that go into the zip: [{ asset, zipPath (relative to zip.root) }].
  editions: [
    {
      id: 'ntsc-u-41560857',
      name: 'Bakugan: Battle Brawlers (Xbox 360, title 41560857)',
      xexSha256: '7f47ff62b744239fb3c1b98bb9ca26ab38b3d33cd148bba6d14c03739f7ab29b',
      header: {
        titleId: 0x41560857,
        mediaId: 0x221503EA,
        version: 0x00000001,
        imageSize: 0x00AB0000,
        entryPoint: 0x823BCB28,
      },
      nro: 'bakugan.nro',
      pipelineCache: 'bakugan_native_pipelines.bin',
      cacheFiles: [
        { asset: 'deko3d_shaders.bin', zipPath: 'cache/deko3d_shaders.bin' },
        { asset: 'pipeline_list.bin', zipPath: 'cache/pipeline_list.bin' },
      ],
    },
  ],

  // Which disc files go to <root>/game_root/. Paths are relative to the disc root, matched case-insensitively.
  // The default is to include a file; only what is listed here is left out.
  disc: {
    xex: 'default.xex',
    skip: [
      { prefix: '$SystemUpdate/', why: 'Xbox 360 console system update, installed by the console, never read by the game' },
      { prefix: 'nxeart', why: 'dashboard artwork of the disc for the Xbox 360 home screen' },
    ],
    // The shader containers of the game are in these files (the scanner decides by content).
    shaderFolder: 'media/shaders/',
    shaderExtension: '.par',
  },

  // The voice and intro-movie languages of the game (media/sound/<name>/ and media/movie/intro_4_3_<code>.pam). The player
  // can leave some out of the zip: the NRO (vfs_interchangeable in app/bakugan_switch_defaults.toml) then serves the same
  // file of an installed language, so the game still runs. The text of the game lives in d00.arc and is not affected.
  // `minBuild`: the oldest build whose NRO knows that fallback; with an older release the page offers no choice.
  languages: {
    minBuild: '0.1.1',
    list: [
      { id: 'english', name: 'English', soundDir: 'media/sound/english/', introMovie: 'media/movie/intro_4_3_eng.pam' },
      { id: 'french', name: 'French', soundDir: 'media/sound/french/', introMovie: 'media/movie/intro_4_3_fre.pam' },
      { id: 'german', name: 'German', soundDir: 'media/sound/german/', introMovie: 'media/movie/intro_4_3_ger.pam' },
      { id: 'italian', name: 'Italian', soundDir: 'media/sound/italian/', introMovie: 'media/movie/intro_4_3_ita.pam' },
      { id: 'spanish', name: 'Spanish', soundDir: 'media/sound/spanish/', introMovie: 'media/movie/intro_4_3_spa.pam' },
      { id: 'dutch', name: 'Dutch', soundDir: 'media/sound/dutch/', introMovie: 'media/movie/intro_4_3_dut.pam' },
      { id: 'swedish', name: 'Swedish', soundDir: 'media/sound/swedish/', introMovie: 'media/movie/intro_4_3_swe.pam' },
    ],
  },

  // The WebAssembly tools (built by shaders/wasm/*.sh and published by the workflow into installer/wasm/).
  wasm: {
    dir: 'wasm/',
    // Cache tag of the WebAssembly tools (added as ?v= to their URLs): change it whenever shaders/wasm/, the
    // translator or the packer change, so browsers drop their cached copies.
    version: '2026-10-18a',
    hlsl: 'hlsl.mjs',
    dxc: 'dxc_web.mjs',
    pack: 'pack.mjs',
    // shader_common.h of the translator (shaders/XenosRecomp/shader_common.h), copied next to the tools.
    shaderCommon: 'shader_common.h',
    // Shaders the game builds in memory while it runs (not on the disc): 48 containers, a few KB in all.
    runtimeContainers: 'runtime_containers.json',
    // Each .mjs loads its own .wasm from the same folder.
    extraFiles: ['hlsl.wasm', 'dxc_web.wasm', 'pack.wasm'],
  },

  // Shader containers the native toolchain cannot build either: the page leaves them out instead of refusing to make
  // the package. Any other container that fails to translate or compile stops the run (no package with holes).
  // Key: the container's name ({vs,ps}_<FNV-1a 64 of the container>.bin); value: why (for the log). These four vertex
  // shaders fail in the native toolchain too (DXC: "partial explicit stage input location assignment via
  // vk::location(X) unsupported"); the shader package of the native build lacks them as well.
  knownShaderFailures: {
    'vs_7c0c96117edd8e12.bin': 'DXC: partial explicit stage input location assignment via vk::location(X) unsupported',
    'vs_5a1c5229b9e7daf0.bin': 'DXC: partial explicit stage input location assignment via vk::location(X) unsupported',
    'vs_2a9009df842d42de.bin': 'DXC: partial explicit stage input location assignment via vk::location(X) unsupported',
    'vs_215a758cd1a242e1.bin': 'DXC: partial explicit stage input location assignment via vk::location(X) unsupported',
  },

  limits: {
    // Parallel translate+compile workers: min(hardwareConcurrency - 1, this). Each holds a DXC instance (~0.5 GB).
    maxShaderWorkers: 6,
    // A container whose job produces no answer in this time is skipped and its worker restarted.
    jobTimeoutMs: 180000,
    // Read size when copying disc files into the zip.
    copyChunkBytes: 4 * 1024 * 1024,
    // The result of a complete run: 1858 containers on the disc + 48 built at run time.
    expectedContainers: 1906,
    // Rough sizes for the up-front estimate (the shader package is about 85 MB).
    expectedShaderBytes: 90 * 1000 * 1000,
    expectedNroBytes: 60 * 1000 * 1000,
    // shaderMode 'embedded': the NRO with the shaders inside, plus the two cache files.
    expectedEmbeddedBytes: 70 * 1000 * 1000,
    // The runtime and the packer refuse a shader package above 1 GiB (kMaxFile in bakugan_shader_library.cpp).
    maxShaderPackageBytes: 1073741824,
    // Without a disk-backed save target the whole zip is built in memory: warn above this size.
    blobWarnBytes: 1.5 * 1024 * 1024 * 1024,
  },
};

export default CONFIG;
