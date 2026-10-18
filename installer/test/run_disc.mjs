// Runs the real installer pipeline in Node over your own extracted disc and writes the zip (update mode: the shader
// package and the build files, without the 3 GB of game files unless --full is given).
//
//   node test/run_disc.mjs <extracted disc folder> <out zip> [--full]
//
// Uses the real WebAssembly tools of installer/wasm (hlsl, pack, DXC). The "site" is installer/ itself: the build
// files are fake (a few bytes) unless installer/releases/ has a real bakugan.nro.
import { readFileSync, writeFileSync, existsSync, readdirSync, statSync, openAsBlob } from 'node:fs';
import { join, dirname, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { CONFIG } from '../config.js';
import { run } from '../js/pipeline.js';
import { MemorySink } from '../js/zip.js';
import { ShaderHandler, PackHandler } from '../js/workers/handlers.js';

const here = dirname(fileURLToPath(import.meta.url));
const site = join(here, '..');
const [discDir, outZip] = process.argv.slice(2);
const full = process.argv.includes('--full');
if (!discDir || !outZip) { console.error('usage: node test/run_disc.mjs <disc folder> <out zip> [--full]'); process.exit(1); }

function walk(dir, prefix = '') {
  const out = [];
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) out.push(...walk(p, `${prefix}${name}/`));
    else out.push({ path: `${prefix}${name}`, size: statSync(p).size, p });
  }
  return out;
}

const all = walk(discDir);
const keep = full ? all : all.filter((f) => /^media\/shaders\//i.test(f.path) || /^default\.xex$/i.test(f.path));
const files = [];
for (const f of keep) files.push({ path: f.path, size: f.size, blob: await openAsBlob(f.p) });

const fakeNro = Buffer.from('NRO0-test-build'.repeat(64));
const fetchImpl = async (url, init = {}) => {
  const u = new URL(url);
  const rel = decodeURIComponent(u.pathname).replace(/^\/+([A-Za-z]:)/, '$1');
  const path = rel.startsWith('/') || /^[A-Za-z]:/.test(rel) ? rel : join(site, rel);
  let body = null;
  if (/releases\/bakugan\.nro$/.test(path)) body = existsSync(path) ? readFileSync(path) : fakeNro;
  else if (/releases\//.test(path)) body = null;
  else if (existsSync(path)) body = readFileSync(path);
  const ok = body !== null && init.method !== undefined ? true : body !== null;
  return {
    ok, status: ok ? 200 : 404,
    headers: { get: (k) => (k.toLowerCase() === 'content-length' && body ? String(body.length) : null) },
    json: async () => JSON.parse(body.toString()),
    arrayBuffer: async () => body.buffer.slice(body.byteOffset, body.byteOffset + body.length),
    body: null,
  };
};

function inProcessWorkers() {
  const kinds = { shader: ShaderHandler, pack: PackHandler };
  return (kind) => {
    const handler = new kinds[kind]();
    let chain = Promise.resolve();
    let dead = false;
    const w = {
      onmessage: null, onerror: null,
      postMessage(msg) {
        chain = chain.then(() => (dead ? null : handler.handle(msg, (m) => setImmediate(() => !dead && w.onmessage?.({ data: m })))));
      },
      terminate() { dead = true; },
    };
    return w;
  };
}

const sink = new MemorySink();
const t0 = Date.now();
let last = '';
const result = await run({
  config: CONFIG, edition: CONFIG.editions[0], files, mode: full ? 'full' : 'update', sink,
  createWorker: inProcessWorkers(), baseUrl: pathToFileURL(site + '/').href, fetchImpl,
  onProgress: (id, p) => { const l = `${id}: ${p.label ?? ''}`; if (l !== last && (!/Translating/.test(l) || /0 of|\b\d+00 of/.test(l))) { console.log(l); last = l; } },
  onLog: (t, level) => console.log(`[${level}] ${t}`),
});
writeFileSync(outZip, sink.concat());
console.log(`done in ${((Date.now() - t0) / 1000).toFixed(0)} s: ${result.zipBytes} bytes, ${result.shaders.ok} shaders`);
