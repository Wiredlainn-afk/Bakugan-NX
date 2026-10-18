#!/usr/bin/env python3
"""Makes installer/wasm/runtime_containers.json: the shader containers the game builds in memory while it runs.

The disc's media/shaders/*.par files hold most of the game's shaders (tools/par_extract_shaders.py). The rest are
written by the game's own code into the command stream as "immediate" shaders (a template with a few words changed)
and are not on the disc. The native renderer dumps them into <bakugan_shader_misses>/missing and /discovery while the
game runs (settings key bakugan_shader_misses); this tool keeps those that the disc does not have, as
{ "<name>.bin": "<base64 of the container>" }, which the installer adds to the disc's before translating.

  python tools/make_runtime_containers.py <library input folder> <disc extraction folder> <out json>

  <library input folder>   out/shaderlib/in (every container the library was built from)
  <disc extraction folder> the output of tools/par_extract_shaders.py for media/shaders/*.par
"""
import base64, json, os, sys

lib, disc, out = sys.argv[1:4]
on_disc = set(os.listdir(disc))
extra = sorted(f for f in os.listdir(lib) if f.endswith('.bin') and f not in on_disc)
data = {}
for f in extra:
    with open(os.path.join(lib, f), 'rb') as fh:
        data[f] = base64.b64encode(fh.read()).decode('ascii')
with open(out, 'w', encoding='utf-8', newline='\n') as fh:
    json.dump(data, fh, indent=0)
    fh.write('\n')
print('%d run-time containers, %d bytes of json -> %s' % (len(data), os.path.getsize(out), out))
