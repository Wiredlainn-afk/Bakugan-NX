# Bakugan: Battle Brawlers (Xbox 360): static recompilation host application.
#
# The game code is not in this repository: run tools/codegen.ps1 first, which writes generated/ from your own
# default.xex together with the CMake glue (generated/rexglue.cmake).
#
# Stage 3, Windows PC (inside tools\win\env.ps1):
#   cmake --preset win-pc -S app && cmake --build out/pc

cmake_minimum_required(VERSION 3.25)
project(bakugan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The SDK is built from source in this repository unless another tree is given.
set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sdk" CACHE PATH "Path to the ReXGlue SDK source tree")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    message(FATAL_ERROR "app/generated/rexglue.cmake is missing: run tools/codegen.ps1 first")
endif()
include(generated/rexglue.cmake)

set(BAKUGAN_SOURCES
    src/main.cpp
    # Windows: logs fatal host exceptions the SDK does not report (illegal instruction, ...) with module + offset.
    src/bakugan_crash_log.cpp
    # Test captures from the presenter (PNG snapshots, raw video): cvars bakugan_shot_s, bakugan_video_s.
    src/bakugan_capture.cpp
    # Temporary hooks that log the callers of functions a boot crashed in.
    src/bakugan_debug_hooks.cpp
    src/bakugan_trace.cpp
    # PGO profile collection (the Switch build with BAKUGAN_PGO=generate; stubs otherwise).
    src/bakugan_pgo.cpp
    # Stage 4: the native renderer (src/me, masseffect-nx's PM4-ring Vulkan renderer as adapted by banjo-nx). It
    # replaces the emulated Xenos GPU with bakugan_renderer_native = true.
    src/me/me_native_system.cpp
    src/me/me_ring_wait.cpp
    src/me/me_renderdoc.cpp
    src/me/native/bakugan_native_draws.cpp
    src/me/native/bakugan_deferred_recording.cpp
    src/me/native/bakugan_native_targets.cpp
    src/me/native/bakugan_native_shaders.cpp
    src/me/native/bakugan_native_textures_pool.cpp
    src/me/native/bakugan_shader_library.cpp
    src/me/native/bakugan_native_glue.cpp
    # Bakugan's D3D addresses (draw-time shader load, Draw*).
    src/me/bakugan_me_hooks.cpp
    # Render resolution per console mode (docked / handheld) and MSAA: bakugan_render_height_*, bakugan_msaa.
    src/me/bakugan_render_mode.cpp
    # Switchable enhancements: masters bakugan_cpu_enhancements / bakugan_noncpu_enhancements.
    src/cpu/bakugan_enhancements.cpp
    src/cpu/bakugan_gpu_boost.cpp
    src/cpu/bakugan_flight.cpp
    # The game's Direct3D GPU wait (sub_8265A180) sleeps instead of spinning: frees a core of the three.
    src/cpu/bakugan_gpu_wait.cpp
    # The sound thread sleeps between mixer updates (it spun a core with Sleep(0)).
    src/cpu/bakugan_sound_sleep.cpp
    # Lock-free AddRef/Release (sub_8249F830/F858): the global-lock bracket was ~45 % of the game thread in movies.
    src/cpu/bakugan_refcount.cpp
    # WMV3 movies decoded by FFmpeg (the XDK decoder runs at 20-23 fps on the Switch).
    src/video/bakugan_video_wmv3.cpp
    src/video/bakugan_video_native.cpp
)
if(NOT REXGLUE_PLATFORM_SWITCH)
    # Switch profiler counters the native renderer calls (no-ops on PC).
    list(APPEND BAKUGAN_SOURCES src/me/me_pc_stubs.cpp)
    # Sampling CPU profiler for PC tests (from nfsmw-nx via banjo-nx).
    list(APPEND BAKUGAN_SOURCES src/profiler/bakugan_cpu_profile_pc.cpp)
endif()

# Diagnostic builds only (e.g. the Direct3D census: out/census/bakugan_d3d_census.cpp): extra sources, empty by default.
set(BAKUGAN_EXTRA_SOURCES "" CACHE STRING "Extra source files for diagnostic builds (empty for normal builds)")
if(BAKUGAN_EXTRA_SOURCES)
    list(APPEND BAKUGAN_SOURCES ${BAKUGAN_EXTRA_SOURCES})
endif()

if(WIN32)
    add_executable(bakugan WIN32 ${BAKUGAN_SOURCES})
else()
    add_executable(bakugan ${BAKUGAN_SOURCES})
endif()

# Link the SDK. Stage 3 draws with ReXGlue's Xenos GPU emulation; the native renderer replaces it later (stage 4).
rexglue_setup_target(bakugan GPU_PLUGINS xenos)

# The SDK's FFmpeg (WMV3 decoder) for the movies: src/video/bakugan_video_wmv3.cpp.
target_link_libraries(bakugan PRIVATE libavcodec)

# SPIR-V initializers of the native renderer's EDRAM transfer / utility shaders (src/me/native/shaders), compiled on
# the host with glslangValidator (tools/win/build_glslang.ps1 builds it from the SDK's glslang).
set(BAKUGAN_GLSLANG_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/../out/glslang/StandAlone/glslangValidator${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "Host glslangValidator")
set(BAKUGAN_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(BAKUGAN_EDRAM_SHADER_INCLUDES)
foreach(_shader bakugan_edram_7e3_to_rgba8 bakugan_edram_rgba8_to_7e3 bakugan_edram_16f_to_16f me_resolve_exp_bias me_edram_depth_to_rgba8 me_edram_depth_to_16f me_edram_import me_edram_color_to_depth me_edram_depth_to_depth me_edram_depth_to_stencil me_edram_color_to_stencil me_edram_depth_to_depth_msaa2 me_edram_depth_msaa2_to_depth_1x me_edram_depth_to_stencil_msaa2 me_edram_depth_msaa2_to_stencil_1x me_edram_raw64_to_rgba8 me_edram_rgba8_to_raw64 me_edram_raw64_to_16f me_edram_16f_to_raw64 me_edram_raw64_to_raw64 me_edram_depth_to_raw64 me_edram_raw64_to_depth me_depth_resolve_guestspace me_depth_resolve_guestspace_msaa2 me_edram_stencil_to_buffer me_edram_stencil_msaa2_to_buffer me_edram_color_stencil_to_buffer me_edram_color_to_color me_edram_r64_to_r64 bakugan_resolve_copy bakugan_depth_resolve_ms)
    set(_extension comp)
    if(_shader STREQUAL "me_edram_import")
        set(_extension vert)
    elseif(_shader MATCHES "^me_edram_(color_to_depth|depth_to_depth|depth_to_stencil|color_to_stencil|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x|raw64_to_depth|color_to_color|r64_to_r64)$" OR _shader STREQUAL "bakugan_resolve_copy" OR
           _shader STREQUAL "bakugan_depth_resolve_ms")
        set(_extension frag)
    endif()
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/me/native/shaders/${_shader}.${_extension}")
    set(_output "${BAKUGAN_GENERATED_SHADER_DIR}/${_shader}.inc")
    set(_target_flags)
    if(_shader MATCHES "^me_edram_(stencil_to_buffer|color_stencil_to_buffer|stencil_msaa2_to_buffer|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x)$" OR _shader STREQUAL "me_depth_resolve_guestspace_msaa2")
        set(_target_flags --target-env vulkan1.2)
        set(_output "${BAKUGAN_GENERATED_SHADER_DIR}/${_shader}_generated.inc")
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BAKUGAN_GENERATED_SHADER_DIR}"
        COMMAND "${BAKUGAN_GLSLANG_VALIDATOR}" -V ${_target_flags} -x -o "${_output}" "${_source}"
        DEPENDS "${_source}" VERBATIM)
    list(APPEND BAKUGAN_EDRAM_SHADER_INCLUDES "${_output}")
endforeach()
add_custom_target(bakugan_edram_shaders DEPENDS ${BAKUGAN_EDRAM_SHADER_INCLUDES})
add_dependencies(bakugan bakugan_edram_shaders)
target_include_directories(bakugan PRIVATE
    "${BAKUGAN_GENERATED_SHADER_DIR}"
    src
    src/me
    src/me/native
    src/me/native/shaders
    "${REXSDK_DIR}/src/core"
    # Precompiled SPIR-V of the SDK presenter (vulkan_spirv/guest_output_*): the renderer draws into its output.
    "${REXSDK_DIR}/src/ui/shaders")

# CPU-only SDK sources the native renderer reuses (draw-extent estimation, shader microcode interpretation): the GPU
# plugin's private symbols cannot satisfy application-side use.
target_sources(bakugan PRIVATE
    "${REXSDK_DIR}/src/graphics/register_file.cpp"
    "${REXSDK_DIR}/src/graphics/format/ucode.cpp"
    "${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp")

# The renderer includes xxhash.h and renderdoc_app.h directly; when the SDK is built from source they are private to
# its libraries.
foreach(_dep xxhash renderdoc)
    if(TARGET ${_dep})
        target_include_directories(bakugan PRIVATE $<TARGET_PROPERTY:${_dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endforeach()

# Settings template next to the executable, only when there is none yet (the copy there is the user's to edit).
if(NOT REXGLUE_PLATFORM_SWITCH)
    add_custom_command(TARGET bakugan POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/bakugan.toml
                -DDST=$<TARGET_FILE_DIR:bakugan>/bakugan.toml
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_if_missing.cmake"
        VERBATIM)
endif()

# Debug: -DBAKUGAN_TRACE_FUNCS=ON records the last 256 guest functions of each thread (src/bakugan_trace.cpp, PC; needs the
# pch patch of tools/win/trace_patch.py after every codegen).
option(BAKUGAN_TRACE_FUNCS "Record the guest functions entered (debug)" OFF)
if(BAKUGAN_TRACE_FUNCS)
    foreach(target bakugan bakugan_recomp)
        if(TARGET ${target})
            target_compile_definitions(${target} PRIVATE BAKUGAN_TRACE_FUNCS=1)
        endif()
    endforeach()
endif()

# The SDK compiles itself with these (directory scope), but they do not reach the generated game code, which is
# built in this directory: no FMA contraction (the PC rounds like the console) and no strict aliasing.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target bakugan bakugan_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off -fno-strict-aliasing)
        endif()
    endforeach()
endif()

# sa2-style trick: bakugan_register.cpp is one generated function of ~46,000 SetFunction calls that runs once at
# startup; GCC -O3 spends minutes on it. -O0 compiles it in seconds and changes nothing at run time but a few
# microseconds of startup.
if(REXGLUE_PLATFORM_SWITCH)
    foreach(register_file generated/default/bakugan_register.cpp)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${register_file}")
            set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${register_file}" APPEND PROPERTY COMPILE_OPTIONS -O0)
        endif()
    endforeach()

    # The Switch build's fixed settings, compiled in (src/bakugan_app.h BuiltInConfig): everything bakugan.toml no
    # longer lists. The SD card's bakugan.toml holds the player's options and overrides these key by key.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/bakugan_switch_defaults.toml" BAKUGAN_BUILTIN_CONFIG)
    # And the player's bakugan.toml, written next to the NRO when it is missing (src/bakugan_app.h DefaultConfigFile).
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/bakugan_switch.toml" BAKUGAN_PLAYER_CONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/bakugan_switch.toml" "${CMAKE_CURRENT_SOURCE_DIR}/bakugan_switch_defaults.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/bakugan_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/bakugan_builtin_config.h" @ONLY)
    target_include_directories(bakugan PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(bakugan PRIVATE BAKUGAN_BUILTIN_CONFIG=1 XXH_FORCE_MEMORY_ACCESS=0)
    # The homebrew menu's entry: name, author, version and icon (app/icon.jpg, 256x256 JPEG).
    rexglue_switch_add_nro(bakugan
        NAME "Bakugan Battle Brawlers"
        AUTHOR "Wiredlainn"
        VERSION "0.1.0"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
endif()

# Compiler-level CPU optimisations of the Switch build (ported from sa2-nx; docs: sa2-nx-main/docs/porting-guide.md ch. 9.2):
#   - LTO: the game code and the app are optimised as one program at the link (-flto, -fipa-pta); the link repeats
#     -ffp-contract=off -fno-strict-aliasing (the LTRANS stage must not fuse what the compile stage kept apart). Off by
#     default: slow link; -DBAKUGAN_SWITCH_LTO=ON.
#   - Hot functions first in .text (app/hot_functions.ld from tools/hot_order.py over console stack samples): the NRO has ~55 MB
#     of code and the A57 a 48 KB instruction cache; placement only, KEEP(*(.crt0)) stays first.
if(REXGLUE_PLATFORM_SWITCH)
    option(BAKUGAN_SWITCH_LTO "Link-time optimisation of the game code and the app (Switch)" OFF)
    set(BAKUGAN_SWITCH_LTO_JOBS "8" CACHE STRING "Parallel LTRANS jobs of the Switch LTO link")
    option(BAKUGAN_SWITCH_IPA_PTA "-fipa-pta at the Switch LTO link" ON)
    option(BAKUGAN_SWITCH_HOT_ORDER "Hot functions first in .text (app/hot_functions.ld)" ON)
    if(BAKUGAN_SWITCH_LTO)
        set(_bk_lto_compile -flto -fno-fat-lto-objects)
        set(_bk_lto_link -flto=${BAKUGAN_SWITCH_LTO_JOBS} -flto-partition=balanced -ffp-contract=off -fno-strict-aliasing)
        if(BAKUGAN_SWITCH_IPA_PTA)
            list(APPEND _bk_lto_compile -fipa-pta)
            list(APPEND _bk_lto_link -fipa-pta)
        endif()
        foreach(target bakugan_recomp bakugan)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_bk_lto_compile})
            endif()
        endforeach()
        target_link_options(bakugan PRIVATE ${_bk_lto_link})
        message(STATUS "bakugan: Switch LTO on (${BAKUGAN_SWITCH_LTO_JOBS} LTRANS jobs, -fipa-pta ${BAKUGAN_SWITCH_IPA_PTA})")
    endif()
    # Profile-guided optimisation (sa2-nx's scheme, porting-guide ch. 9.2), for the game code and the app like LTO:
    #   BAKUGAN_PGO=generate  an instrumented NRO that streams its branch counts to pgo/ next to the NRO on the SD card
    #                         (src/bakugan_pgo.cpp: every 2 minutes, on HOME and at exit; value profiling is off).
    #   BAKUGAN_PGO=use       the counts merged on the PC (tools/win/pgo_merge.ps1 -> the repository's pgo/ folder). Build both in the
    #                         same build folder (tools/win/build_switch_pgo.ps1 uses out/nx_pgo): the .gcda names carry its path.
    set(BAKUGAN_PGO "" CACHE STRING "Profile-guided optimisation of the Switch build: empty, generate or use")
    set_property(CACHE BAKUGAN_PGO PROPERTY STRINGS "" generate use)
    set(BAKUGAN_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
    get_filename_component(_bk_pgo_dir "${BAKUGAN_PGO_DIR}" ABSOLUTE)
    file(TO_NATIVE_PATH "${_bk_pgo_dir}" _bk_pgo_dir)
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" _bk_pgo_prefix)
    if(BAKUGAN_PGO STREQUAL "generate")
        set(_bk_pgo "-fprofile-generate=${_bk_pgo_dir}" "-fprofile-prefix-path=${_bk_pgo_prefix}"
                    -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(bakugan PRIVATE BAKUGAN_PGO_GENERATE=1)
    elseif(BAKUGAN_PGO STREQUAL "use")
        if(NOT EXISTS "${BAKUGAN_PGO_DIR}")
            message(FATAL_ERROR "BAKUGAN_PGO=use: no profile in ${BAKUGAN_PGO_DIR}")
        endif()
        set(_bk_pgo "-fprofile-use=${_bk_pgo_dir}" "-fprofile-prefix-path=${_bk_pgo_prefix}" -fprofile-correction
                    -fno-profile-values -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch)
    elseif(BAKUGAN_PGO)
        message(FATAL_ERROR "BAKUGAN_PGO must be empty, generate or use (got ${BAKUGAN_PGO})")
    endif()
    if(BAKUGAN_PGO)
        foreach(target bakugan_recomp bakugan)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_bk_pgo})
            endif()
        endforeach()
        target_link_options(bakugan PRIVATE ${_bk_pgo})
        message(STATUS "bakugan: PGO ${BAKUGAN_PGO}")
    endif()
    if(BAKUGAN_SWITCH_HOT_ORDER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/hot_functions.ld")
        target_link_options(bakugan PRIVATE "-Wl,--section-ordering-file,${CMAKE_CURRENT_SOURCE_DIR}/hot_functions.ld")
        set_property(TARGET bakugan APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/hot_functions.ld")
    endif()
endif()
