# The graphics driver (Mesa and NVK)

This port does not change the driver. The NRO links the same Vulkan driver as nfsmw-nx: **NVK** (Mesa) for Horizon
from [danfromtico/mesa-switch](https://github.com/danfromtico/mesa-switch) at commit `1a8c1a66d6f`, with the
changes of [StevensND/nfsmw-nx](https://github.com/StevensND/nfsmw-nx) (`mesa/mesa-switch-nfsmw.patch`, 35 files).

1. `git clone https://github.com/danfromtico/mesa-switch.git && cd mesa-switch && git checkout 1a8c1a66d6f`
2. `git apply <nfsmw-nx>/mesa/mesa-switch-nfsmw.patch`
3. Build it with mesa-switch's `build-unified.sh` (see its README; `docs/switch-build-notes.md` lists what the build
   needed on Windows). The result is an SDK folder with `libvulkan.a`, `libnvk.a` and `libnak_rs.a`.
4. Point the Switch build at it: `NVK_SDK=<that folder>` (see `tools/win/build_switch.ps1`).
