// The installer's work, start to finish. Runs in the page's main thread and drives Web Workers; it never touches the
// DOM, so the Node tests run it with in-process workers.
//
//   download  the build (NRO) and the release's pipeline cache (optional) from the site, checked against manifest.json
//   scan      read the disc's media/shaders/*.par files and find the shader containers         (js/par_scan.js)
//   translate every container: Xbox 360 microcode -> HLSL -> SPIR-V                           (workers: hlsl.mjs + dxc_web.mjs)
//   pack      make bakugan_shaders.mesp and its .idx                                          (worker: pack.mjs)
//   zip       stream the zip: nro, pipeline cache, shader package, then the disc files (full mode)
//
// Based on the installer of masseffect-nx (NebadasSwifty), without its NSP, DLC and Unreal-package parts.
import { ZipWriter } from './zip.js';
import { planGameFiles, shaderFiles, sha256Hex } from './plan.js';
import { scanContainers } from './par_scan.js';

export class Cancelled extends Error {
  constructor() { super('Cancelled'); this.name = 'Cancelled'; }
}

export class UserError extends Error {
  constructor(message, code = 'error') { super(message); this.name = 'UserError'; this.code = code; }
}

export const stageIds = ['download', 'scan', 'translate', 'pack', 'zip'];

function abs(rel, baseUrl) {
  return new URL(rel, baseUrl).href;
}

export function wasmUrls(config, baseUrl) {
  const w = config.wasm;
  // ?v=<config.wasm.version>: a browser that cached an older tool must not keep using it (the workers pass the same
  // query on to each module's .wasm, see handlers.js createModule).
  const url = (name) => abs(w.dir + name, baseUrl) + (w.version ? `?v=${encodeURIComponent(w.version)}` : '');
  return {
    hlsl: url(w.hlsl),
    dxc: url(w.dxc),
    pack: url(w.pack),
    shaderCommon: url(w.shaderCommon),
    runtimeContainers: w.runtimeContainers ? url(w.runtimeContainers) : null,
  };
}

/** HEAD-checks every file of the shader toolchain. Returns the list of missing URLs (empty = all present). */
export async function checkToolchain(config, baseUrl, fetchImpl = fetch) {
  const w = config.wasm;
  const names = [w.hlsl, w.dxc, w.pack, w.shaderCommon, ...w.extraFiles];
  if (w.runtimeContainers) names.push(w.runtimeContainers);
  const missing = [];
  await Promise.all(names.map(async (n) => {
    const url = abs(w.dir + n, baseUrl);
    try {
      const r = await fetchImpl(url, { method: 'HEAD' });
      if (!r.ok) missing.push(w.dir + n);
    } catch {
      missing.push(w.dir + n);
    }
  }));
  return missing.sort();
}

async function fetchWithProgress(url, fetchImpl, signal, onBytes) {
  let r;
  try {
    r = await fetchImpl(url, { signal, cache: 'no-cache' });
  } catch (e) {
    if (signal?.aborted) throw new Cancelled();
    throw new UserError(`Could not download ${url}: ${e.message}. Check your connection and try again.`, 'network');
  }
  if (!r.ok) {
    throw new UserError(r.status === 404
      ? `${url} was not found on the site (HTTP 404). The build has not been published yet.`
      : `Could not download ${url} (HTTP ${r.status}).`, 'download');
  }
  const total = Number(r.headers.get('content-length')) || 0;
  const parts = [];
  let got = 0;
  if (r.body?.getReader) {
    const reader = r.body.getReader();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value);
      got += value.length;
      onBytes?.(got, total);
    }
  } else {
    const b = new Uint8Array(await r.arrayBuffer());
    parts.push(b);
    got = b.length;
    onBytes?.(got, total);
  }
  const out = new Uint8Array(got);
  let p = 0;
  for (const part of parts) { out.set(part, p); p += part.length; }
  return out;
}

/**
 * Fetches the NRO of the edition and the release's pipeline cache (optional); verifies them against
 * releases/manifest.json when present.
 */
export async function fetchBuild(config, edition, { baseUrl, fetchImpl = fetch, signal, progress, log }) {
  let manifest = null;
  try {
    const r = await fetchImpl(abs(config.build.manifest, baseUrl), { signal, cache: 'no-cache' });
    if (r.ok) manifest = await r.json();
  } catch (e) {
    if (signal?.aborted) throw new Cancelled();
  }
  const files = [{ key: 'nro', label: edition.nro, url: abs(config.build.siteDir + edition.nro, baseUrl), manifestName: edition.nro }];
  const cache = edition.pipelineCache;
  if (cache && !(manifest && !manifest.assets?.[cache])) {
    files.push({ key: 'pipelineCache', label: cache, url: abs(config.build.siteDir + cache, baseUrl), manifestName: cache, optional: true });
  } else {
    log?.('The release has no pipeline cache: the first battles compile pipelines during play.', 'warn');
  }
  const out = {};
  let doneBytes = 0;
  const totalBytes = files.reduce((a, f) => a + (manifest?.assets?.[f.manifestName]?.size ?? 0), 0) || 0;
  for (const f of files) {
    const base = doneBytes;
    let data;
    try {
      data = await fetchWithProgress(f.url, fetchImpl, signal, (got, total) => {
        progress({ done: base + got, total: totalBytes || base + (total || got), label: `Downloading ${f.label}` });
      });
    } catch (e) {
      if (!f.optional || e instanceof Cancelled || manifest?.assets?.[f.manifestName]) throw e;
      log?.(`${f.label} could not be downloaded (${e.message}); installing without it.`, 'warn');
      continue;
    }
    const entry = manifest?.assets?.[f.manifestName];
    if (entry) {
      if (entry.size !== data.length) throw new UserError(`${f.label} has the wrong size (${data.length}, expected ${entry.size}). Try again in a minute; the site may be updating.`, 'integrity');
      if (entry.sha256 && (await sha256Hex(data)) !== entry.sha256.toLowerCase()) {
        throw new UserError(`${f.label} does not match its checksum in manifest.json. Try again in a minute; the site may be updating.`, 'integrity');
      }
    } else if (f.key === 'nro') {
      log?.('No manifest.json on the site: the build could not be verified against a checksum.', 'warn');
    }
    out[f.key] = data;
    doneBytes += data.length;
  }
  out.tag = manifest?.tag ?? null;
  progress({ done: 1, total: 1, label: 'Build downloaded' });
  return out;
}

// ---- worker plumbing -------------------------------------------------------------------------------------------
/** One outstanding request at a time per worker: send a message, wait for the next message of an expected type. */
class Remote {
  constructor(worker) {
    this.worker = worker;
    this.waiting = null;
    worker.onmessage = (e) => this.#message(e.data);
    worker.onerror = (e) => this.#fail(new Error(e?.message || 'worker error'));
  }
  #message(m) {
    if (!this.waiting) return;
    const w = this.waiting;
    if (m.type === 'fatal') { this.waiting = null; w.reject(Object.assign(new UserError(`${m.file}: ${m.error}`, 'wasm'), { file: m.file })); return; }
    if (m.type === w.type) { this.waiting = null; clearTimeout(w.timer); w.resolve(m); }
  }
  #fail(e) {
    if (!this.waiting) return;
    const w = this.waiting;
    this.waiting = null;
    clearTimeout(w.timer);
    w.reject(e);
  }
  call(message, transfer, replyType, timeoutMs) {
    return new Promise((resolve, reject) => {
      const w = { type: replyType, resolve, reject, timer: null };
      if (timeoutMs) w.timer = setTimeout(() => { this.waiting = null; reject(Object.assign(new Error('timeout'), { timeout: true })); }, timeoutMs);
      this.waiting = w;
      this.worker.postMessage(message, transfer ?? []);
    });
  }
  terminate() { this.worker.terminate?.(); }
  cancel() { this.#fail(new Cancelled()); this.terminate(); }
}

class Workers {
  constructor(create) { this.create = create; this.live = new Set(); }
  spawn(kind) { const r = new Remote(this.create(kind)); this.live.add(r); return r; }
  kill(r) { r.terminate(); this.live.delete(r); }
  killAll() { for (const r of [...this.live]) r.cancel(); this.live.clear(); }
}

function throwIfCancelled(signal) { if (signal?.aborted) throw new Cancelled(); }

// ---- stages ----------------------------------------------------------------------------------------------------
/** Reads media/shaders/*.par and returns Map(name -> container bytes). */
async function scanStage({ files, config, signal, progress, log }) {
  const candidates = shaderFiles(files, config.disc);
  if (candidates.length === 0) throw new UserError(`The disc has no ${config.disc.shaderFolder}*${config.disc.shaderExtension} files, so there are no shaders to build.`, 'disc');
  const containers = new Map();
  let done = 0;
  for (const f of candidates) {
    throwIfCancelled(signal);
    const bytes = new Uint8Array(await f.blob.arrayBuffer());
    const found = scanContainers(bytes);
    for (const c of found) if (!containers.has(c.name)) containers.set(c.name, c.data);
    done++;
    progress({ done, total: candidates.length, label: `Reading the disc: ${done} of ${candidates.length} shader files, ${containers.size.toLocaleString('en-US')} shaders found` });
  }
  log(`Scan: ${candidates.length} shader files read, ${containers.size} distinct shader containers.`, 'info');
  return containers;
}

/** The shaders the game builds in memory while it runs (not on the disc): added to the disc's. */
async function addRuntimeContainers({ urls, containers, fetchImpl, signal, log }) {
  if (!urls.runtimeContainers) return;
  try {
    const r = await fetchImpl(urls.runtimeContainers, { signal, cache: 'no-cache' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const data = typeof r.json === 'function' ? await r.json() : JSON.parse(new TextDecoder().decode(r));
    let added = 0;
    for (const [name, b64] of Object.entries(data)) {
      if (containers.has(name)) continue;
      const bin = typeof Buffer !== 'undefined' ? new Uint8Array(Buffer.from(b64, 'base64')) : Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      containers.set(name, bin);
      added++;
    }
    log(`Added ${added} shaders the game builds while it runs. Total: ${containers.size.toLocaleString('en-US')} containers.`, 'info');
  } catch (e) {
    throw new UserError(`Could not load the run-time shader list (${e.message}). The game would miss graphics without it.`, 'runtime-containers');
  }
}

// A translator answer "not a shader container": the scanner's byte pattern matched random data whose header fails
// the runtime's layout checks. The game ignores such a container too, so it is not a missing shader.
const NOT_A_SHADER = /not a shader container/;

async function translateStage({ workers, urls, containers, config, signal, progress, log, packRemote }) {
  const names = [...containers.keys()];
  const total = names.length;
  if (total === 0) throw new UserError('No shader containers were found on the disc. Is this a complete Bakugan: Battle Brawlers disc?', 'disc');
  const failures = [];
  let done = 0, ok = 0;
  let batch = [];
  let addChain = Promise.resolve();
  const flush = () => {
    const items = batch;
    batch = [];
    if (items.length) {
      addChain = addChain.then(() => packRemote.call({ type: 'add', id: 0, items }, items.flatMap((i) => [i.spirv.buffer]), 'added'));
    }
  };
  const jobMs = config.limits.jobTimeoutMs;
  const startWorker = async () => {
    const remote = workers.spawn('shader');
    await remote.call({ type: 'init', urls }, [], 'ready', 120000);
    return remote;
  };
  const skippedLabel = () => (failures.length ? ` (${failures.length} not translated yet)` : '');
  // One pass over `queue` with `n` workers; failed containers go to `failed`.
  const pass = async (queue, n, failed, counted) => {
    const runners = [];
    for (let i = 0; i < n; i++) {
      runners.push((async () => {
        let remote = await startWorker();
        for (let name = queue.shift(); name; name = queue.shift()) {
          throwIfCancelled(signal);
          const data = containers.get(name);
          let r;
          try {
            r = await remote.call({ type: 'job', id: done, name, data }, [], 'result', jobMs);
          } catch (e) {
            if (!e.timeout) throw e;
            workers.kill(remote);
            r = { ok: false, stage: 'translate', reason: `no answer after ${Math.round(jobMs / 1000)} s` };
            remote = await startWorker();
          }
          if (counted) done++;
          if (r.ok) {
            ok++;
            batch.push({ name, container: data, spirv: r.spirv });
            if (batch.length >= 64) flush();
          } else {
            failed.push({ name, stage: r.stage, reason: r.reason });
          }
          if (counted && (done % 50 === 0 || done === total)) {
            progress({ done, total, label: `Translating shaders: ${done.toLocaleString('en-US')} of ${total.toLocaleString('en-US')}${skippedLabel()}` });
          }
        }
        workers.kill(remote);
      })());
    }
    await Promise.all(runners);
  };
  await pass(names.slice(), Math.max(1, Math.min(config.limits.maxShaderWorkers, total, workerBudget())), failures, true);

  const ignored = failures.filter((f) => NOT_A_SHADER.test(f.reason));
  let failed = failures.filter((f) => !NOT_A_SHADER.test(f.reason));
  // A real failure gets one more try, alone in a fresh worker. What fails twice is reported.
  if (failed.length) {
    log(`${failed.length} shader containers failed; trying them once more in a fresh worker.`, 'warn');
    const again = [];
    await pass(failed.map((f) => f.name), 1, again, false);
    failed = again;
  }
  flush();
  await addChain;
  progress({ done: total, total, label: `Translating shaders: ${total.toLocaleString('en-US')} of ${total.toLocaleString('en-US')}` });

  const knownList = config.knownShaderFailures ?? {};
  const known = failed.filter((f) => Object.prototype.hasOwnProperty.call(knownList, f.name));
  const unexpected = failed.filter((f) => !Object.prototype.hasOwnProperty.call(knownList, f.name));
  for (const f of ignored) log(`Not a shader: ${f.name}: ${f.reason}`, 'skip');
  for (const f of known) log(`Known untranslatable ${f.name} (${f.stage}): ${f.reason} [${knownList[f.name]}]`, 'skip');
  for (const f of unexpected) log(`FAILED ${f.name} (${f.stage}): ${f.reason}`, 'error');
  const real = total - ignored.length;
  log(`Translated ${ok} of ${real} shader containers${unexpected.length ? `; ${unexpected.length} FAILED` : ''}.`, unexpected.length ? 'error' : 'info');
  return { ok, failures: unexpected, known, ignored: ignored.length, total };
}

function workerBudget() {
  const hc = (typeof navigator !== 'undefined' && navigator.hardwareConcurrency) || 4;
  return Math.max(1, hc - 1);
}

async function* readChunks(blob, chunkBytes, signal, from = 0) {
  let next = null;
  const read = (pos) => blob.slice(pos, Math.min(pos + chunkBytes, blob.size)).arrayBuffer().then((b) => new Uint8Array(b));
  if (blob.size <= from) return;
  next = read(from);
  for (let pos = from; pos < blob.size; pos += chunkBytes) {
    throwIfCancelled(signal);
    const chunk = await next;
    const after = pos + chunkBytes;
    next = after < blob.size ? read(after) : null;
    yield chunk;
  }
}

async function* bytesOnce(bytes) { if (bytes.length > 0) yield bytes; }

async function* packageChunks(packRemote, file, size, chunkBytes, signal) {
  for (let offset = 0; offset < size; offset += chunkBytes) {
    throwIfCancelled(signal);
    const length = Math.min(chunkBytes, size - offset);
    const r = await packRemote.call({ type: 'read', id: offset, file, offset, length }, [], 'chunk');
    yield r.data;
  }
}

/**
 * Runs the whole thing.
 * options: { config, edition, files (disc files, re-rooted), mode: 'full'|'update', sink, createWorker(kind),
 *            baseUrl, fetchImpl, signal, onProgress(stageId, {done,total,label}), onLog(text, level) }
 * Returns { zipBytes, shaders: {ok, failures (always empty: a failure throws), known, ignored, total}, sinkResult, tag }.
 */
export async function run(options) {
  const { config, edition, files, mode, sink, createWorker, baseUrl, signal } = options;
  const fetchImpl = options.fetchImpl ?? fetch;
  const log = options.onLog ?? (() => {});
  const stage = (id) => (p) => options.onProgress?.(id, p);
  const workers = new Workers(createWorker);
  const urls = wasmUrls(config, baseUrl);
  const onAbort = () => workers.killAll();
  signal?.addEventListener('abort', onAbort);
  const names = config.files;
  const root = config.zip.root;
  try {
    if (names.shadersIndex !== `${names.shaders}.idx`) {
      throw new UserError('config.js: files.shadersIndex must be files.shaders + ".idx" (the packer derives the index name).', 'config');
    }
    throwIfCancelled(signal);
    const missing = await checkToolchain(config, baseUrl, fetchImpl);
    if (missing.length) {
      throw new UserError(`The shader tools are not on this site: missing ${missing.join(', ')}. ` +
        'The site build did not include the WebAssembly tools (see installer/README.md); the shaders cannot be made in the browser until it does.', 'wasm-missing');
    }
    const build = await fetchBuild(config, edition, { baseUrl, fetchImpl, signal, progress: stage('download'), log });
    log(build.tag ? `Build ${build.tag} downloaded.` : 'Build downloaded.', 'info');
    if (build.pipelineCache) log(`Pipeline cache of the release: ${build.pipelineCache.length.toLocaleString('en-US')} bytes.`, 'info');

    throwIfCancelled(signal);
    const containers = await scanStage({ files, config, signal, progress: stage('scan'), log });
    await addRuntimeContainers({ urls, containers, fetchImpl, signal, log });

    // The pack worker starts now so SPIR-V goes straight into it as the translators finish.
    const packRemote = workers.spawn('pack');
    await packRemote.call({ type: 'init', urls }, [], 'ready');
    const shaders = await translateStage({ workers, urls, containers, config, signal, progress: stage('translate'), log, packRemote });
    containers.clear();
    if (shaders.ok === 0) throw new UserError('No shader could be translated. The WebAssembly tools may be broken; see the log.', 'shaders');
    if (shaders.failures.length) {
      // Never ship a package with holes: the game would draw nothing (or the wrong thing) wherever these are used.
      const first = shaders.failures.slice(0, 5).map((f) => `${f.name} (${f.stage}: ${f.reason})`).join('; ');
      throw new UserError(`${shaders.failures.length} of ${(shaders.total - shaders.ignored).toLocaleString('en-US')} shaders could not be made, ` +
        `so no package was written (it would miss graphics in the game). First: ${first}. ` +
        'Try again in a fresh browser tab with other tabs closed; if it fails again, report it with the Details log.', 'shaders-incomplete');
    }
    if (shaders.ok < config.limits.expectedContainers * 0.95) {
      log(`Only ${shaders.ok} shaders were made (a complete disc gives about ${config.limits.expectedContainers}). The game may be missing graphics.`, 'warn');
    }

    throwIfCancelled(signal);
    stage('pack')({ done: 0, total: 1, label: 'Packing the shader library (this takes a while and a lot of memory)' });
    const packed = await packRemote.call({ type: 'pack', names: { package: names.shaders, index: names.shadersIndex } }, [], 'packed');
    if (!packed.ok) {
      throw new UserError(packed.memory
        ? `Packing the shaders ran out of memory (${packed.error}). Close other tabs and programs and try again in a desktop browser with at least 8 GB of RAM.`
        : `Packing the shaders failed: ${packed.error}`, 'pack');
    }
    const cap = config.limits.maxShaderPackageBytes;
    if (cap && packed.sizes.package > cap) {
      throw new UserError(`The shader package is ${packed.sizes.package.toLocaleString('en-US')} bytes, more than the ${cap.toLocaleString('en-US')} bytes (1 GiB) the game can load.`, 'shader-cap');
    }
    stage('pack')({ done: 1, total: 1, label: `Shader library packed: ${packed.sizes.package.toLocaleString('en-US')} bytes + ${packed.sizes.index.toLocaleString('en-US')} index` });
    log(packed.summary || 'Packed.', 'info');

    // ---- the zip ----
    throwIfCancelled(signal);
    const plan = mode === 'full' ? planGameFiles(files, config.disc) : null;
    const entries = [
      { name: `${root}/${names.nro}`, size: build.nro.length, chunks: () => bytesOnce(build.nro) },
      ...(build.pipelineCache ? [{ name: `${root}/${names.pipelineCache}`, size: build.pipelineCache.length, chunks: () => bytesOnce(build.pipelineCache) }] : []),
      { name: `${root}/${names.shaders}`, size: packed.sizes.package, chunks: () => packageChunks(packRemote, names.shaders, packed.sizes.package, 8 * 1024 * 1024, signal) },
      { name: `${root}/${names.shadersIndex}`, size: packed.sizes.index, chunks: () => packageChunks(packRemote, names.shadersIndex, packed.sizes.index, 8 * 1024 * 1024, signal) },
    ];
    if (plan) {
      for (const f of plan.copy) {
        entries.push({ name: `${root}/${config.zip.gameRootDir}/${f.path}`, size: f.size, chunks: () => readChunks(f.blob, config.limits.copyChunkBytes, signal) });
      }
    }
    const totalBytes = entries.reduce((a, e) => a + e.size, 0);
    let written = 0;
    const zip = new ZipWriter({ write: async (b) => { await sink.write(b); written += b.length; } });
    let fileNo = 0;
    for (const e of entries) {
      throwIfCancelled(signal);
      const tick = async function* () {
        for await (const c of e.chunks()) {
          yield c;
          stage('zip')({ done: written, total: totalBytes, label: `Writing the zip: file ${fileNo + 1} of ${entries.length}` });
        }
      };
      await zip.addFile(e.name, e.size, tick());
      fileNo++;
    }
    await zip.finish();
    stage('zip')({ done: totalBytes, total: totalBytes, label: 'Finishing the zip' });
    const sinkResult = await sink.close();
    return { zipBytes: zip.bytesWritten, shaders, sinkResult, tag: build.tag };
  } catch (e) {
    try { await sink.abort?.(); } catch { /* ignore */ }
    throw e;
  } finally {
    signal?.removeEventListener('abort', onAbort);
    workers.killAll();
  }
}
