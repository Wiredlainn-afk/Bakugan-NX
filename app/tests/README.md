# Test settings

Settings files for test builds on the console. A TEST build is the same source and the same NRO as a release: only the
`bakugan.toml` next to it differs. Release builds ship **no** `bakugan.toml` (the NRO writes the player's default one and
everything else comes from its built-in defaults, `app/bakugan_switch_defaults.toml`).

```
tools\win\package_switch.ps1                 # test folder: out\nx\sdcard\switch\bakugan-nx (no pipeline cache)
tools\win\stage_toml.ps1 AK_profile_xfer     # puts app\tests\AK_profile_xfer.toml there as bakugan.toml
tools\win\package_release.ps1                # release folder: out\nx\sdcard_release\... (no toml)
```

Test files keep numbered logs (`log_single_file = false`) and turn on the profile report, which then accumulates in
`logs/rex/rex_profile.log` with one `#### launch bakugan_NNN.log` marker per start. The standard test for comparing
builds is **AK_profile_xfer** (release behaviour plus the profile, GPU categories and a serialized diagnostic frame
every 10 s); `Z_release_check` is the empty file that checks the built-in defaults. Analyse the logs with
`tools/analyze_runs.py` and `tools/analyze_categories.py`. The files from A to Z are in the order they were written;
the early ones are single experiments (a key or two against the baseline) and are kept as a record.

| File | What it tests |
|---|---|
| AA_ablation | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AB_hang_stacks | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AC_profile_450 | Profile run that starts exactly like the working tests (dynamic render scale on, 720p start): per-category GPU times, one frame's timeline every 20 s, per-shader costs once per 10 s, transfer pairs. |
| AD_min540 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AE_stacks_540 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AF_cas450 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AG_cas_strong | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| AH_profile_draws | Profile run that starts exactly like the working tests (dynamic render scale on, 720p start): per-category GPU times, one frame's timeline every 20 s, per-shader costs once per 10 s, transfer pairs. |
| AI_stall_stacks | Profile run that starts exactly like the working tests (dynamic render scale on, 720p start): per-category GPU times, one frame's timeline every 20 s, per-shader costs once per 10 s, transfer pairs. |
| AJ_clean_stacks | Clean CPU profile: the release configuration (built-in defaults: dynamic scale, CAS, native renderer) plus the stack sampler and numbered logs. No GPU marks, no pair report, no per-draw log, no render-height pinning, so the ring |
| AK_profile_xfer | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AL_worker_any | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AM_worker_core2 | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AN_sound_spin | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AO_xfer_log | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AP_bolder | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| AQ_late | Release behaviour (dynamic scale, CAS, native renderer) plus: the CPU stack sampler, GPU timestamps per pass category, and once every 10 s one serialized diagnostic frame (per-draw GPU time, the top individual draws, and the GPU time of |
| A_baseline | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| C_alias_mode3 | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| D_alias_mode1 | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| E_handheld_540 | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| F_zcull_off | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| G_stats | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| H_stacks | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| I_540p_stacks | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| J_audio_queue20 | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| K_sound_sleep_off | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| L_gpu_wait_off | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| M_both_off | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| N_limiter | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| O_audio_queue32 | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| P_clear_proof_on | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| Q_clear_proof_off | Read the plain game_root folder directly (no 3 GB asset pack build at startup). Face buttons by letter: the Switch's A acts as the Xbox A, so the letters in the on-screen prompts are the pad's |
| R_dynamic_scale | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| S_fixed_720 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| T_lod_bias_10 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| U_fixed_540 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| V_540_profile | Profile run: fixed 540p, per-category GPU times, one frame's timeline every 20 s, per-shader cost once per 10 s. development runs keep one numbered log per run (the release default is one replaced file) |
| W_profile_dynamic | Profile run that starts exactly like the working tests (dynamic render scale on, 720p start): per-category GPU times, one frame's timeline every 20 s, per-shader costs once per 10 s, transfer pairs. |
| X_wmv3_no_simd | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| Y_video_dump | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| Z1_movie_game_decoder | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| Z3_noncpu_enh_off | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| Z4_fixed_720 | Dynamic render scale (performance-fidelity branch): steps the render height down by 180 px while the GPU is the limit (handheld: 720 -> 540, docked: 1080 -> 720) and back up with headroom. Look for "[res] dynamic:" in the log. |
| Z_release_check | Release defaults check: no keys at all, everything comes from the NRO built-in defaults (logging on and replaced every start, dynamic render scale, CAS 0.7 below native size, native renderer, input by letter). |
