// The installer page: wires the HTML to the pipeline (js/pipeline.js). English only; nothing here touches the game's
// files except through the pipeline. Based on the page of the masseffect-nx installer.
import { CONFIG } from '../config.js';
import { run, stagesFor, Cancelled, UserError } from './pipeline.js';
import { sourceFromIso, sourceFromFileList, sourceFromDataTransfer, inspectDisc } from './source.js';
import { openSink, describeSinkSupport, cleanStaleTemporaryFiles } from './sink.js';
import { planGameFiles, formatBytes, formatDuration, languageSkips, languageBytes, buildAtLeast } from './plan.js';

const $ = (id) => document.getElementById(id);
const show = (el, on) => { el.hidden = !on; };

const STAGE_LABELS = {
  download: 'Download the build and its cache files',
  scan: 'Read the game\'s shader files',
  translate: 'Translate the shaders',
  pack: 'Pack the shader library',
  zip: 'Write the zip',
};

let disc = null; // { files, edition, ... } of the picked game
let running = null; // { controller }
let selectedLanguages = new Set(CONFIG.languages.list.map((l) => l.id));
let languagesAllowed = true; // false while the published build does not know the language fallback

// ---- environment ----------------------------------------------------------------------------------------------
function checkEnvironment() {
  const problems = [];
  if (!window.isSecureContext) problems.push('This page must be opened over https:// (or http://localhost): hashing the game needs a secure context.');
  if (typeof WebAssembly !== 'object') problems.push('This browser has no WebAssembly.');
  let moduleWorkers = false;
  try { new Worker(URL.createObjectURL(new Blob([''])), { get type() { moduleWorkers = true; return 'module'; } }).terminate(); } catch { /* ignore */ }
  if (!moduleWorkers) problems.push('This browser does not support module workers. Use a recent Chrome, Edge, Firefox or Safari.');
  const box = $('env-warning');
  if (problems.length) { box.textContent = problems.join(' '); show(box, true); }
  const sink = describeSinkSupport();
  if (sink === 'memory') {
    const note = 'This browser cannot stream the zip to disk, so the whole package is built in memory. Use a recent Chrome or Edge for the first install.';
    box.textContent = `${box.textContent} ${note}`.trim();
    show(box, true);
  }
}

// ---- wizard ---------------------------------------------------------------------------------------------------
function setWizard(active, done = []) {
  document.querySelectorAll('#wizard .wstep').forEach((li) => {
    const n = Number(li.dataset.step);
    li.className = `wstep ${done.includes(n) ? 'done' : n === active ? 'active' : 'pending'}`;
  });
}

// ---- step 1: the source ----------------------------------------------------------------------------------------
function currentFormat() {
  return document.querySelector('input[name="format"]:checked').value;
}

function updateFormatUi() {
  const iso = currentFormat() === 'iso';
  $('pick').textContent = iso ? 'Choose the .iso file' : 'Choose the folder';
  $('format-hint').textContent = iso
    ? 'The disc image of the Xbox 360 game.'
    : 'The extracted disc: the folder that contains default.xex and media/ (or its parent folder).';
}

async function loadSource(producer) {
  show($('step-edition'), false);
  show($('step-create'), false);
  disc = null;
  const status = $('source-status');
  status.className = 'status';
  status.textContent = 'Reading the game...';
  try {
    const source = await producer();
    const found = await inspectDisc(source, CONFIG);
    disc = found;
    status.textContent = `${source.label}: ${found.files.length.toLocaleString('en-US')} files (${source.detail}).`;
    showEdition(found);
  } catch (e) {
    status.className = 'status error';
    status.textContent = e.message || String(e);
    setWizard(1);
  }
}

function showEdition(found) {
  const body = $('edition-body');
  body.replaceChildren();
  const p = document.createElement('p');
  show($('step-edition'), true);
  if (!found.edition) {
    p.className = 'notice warn';
    p.textContent = found.matchType === 'foreign_header'
      ? 'This default.xex belongs to a different game or edition. Only the edition below is supported.'
      : found.matchType === 'invalid'
        ? `default.xex is not a valid Xbox 360 executable (${found.error}).`
        : 'This edition of the game is not supported yet. The port is the recompiled program of one exact default.xex.';
    body.append(p);
    const list = document.createElement('p');
    list.className = 'hint';
    list.textContent = `Supported: ${CONFIG.editions.map((e) => e.name).join('; ')}. Your default.xex SHA-256: ${found.sha256}`;
    body.append(list);
    setWizard(2);
    return;
  }
  p.textContent = `${found.edition.name}${found.matchType === 'exact' ? '' : ' (matched by its header, not by the exact file)'}`;
  body.append(p);
  setWizard(3, [1, 2]);
  selectedLanguages = new Set(CONFIG.languages.list.map((l) => l.id));
  showLanguages(found);
  updateCreateInfo();
  show($('step-create'), true);
}

// What the zip holds besides the game files.
function buildBytes() {
  return CONFIG.shaderMode === 'embedded'
    ? CONFIG.limits.expectedEmbeddedBytes
    : CONFIG.limits.expectedShaderBytes + CONFIG.limits.expectedNroBytes;
}

function showObsolete() {
  const ul = $('obsolete-list');
  ul.replaceChildren();
  for (const f of CONFIG.obsoleteFiles ?? []) {
    const li = document.createElement('li');
    li.textContent = f;
    ul.append(li);
  }
  show($('obsolete'), (CONFIG.obsoleteFiles ?? []).length > 0);
}

function updateCreateInfo() {
  if (!disc?.edition) return;
  const plan = planGameFiles(disc.files, CONFIG.disc, languageSkips(CONFIG.languages, selectedLanguages));
  $('create-info').textContent =
    `Full zip: about ${formatBytes(plan.copyBytes + buildBytes())} ` +
    `(${plan.copy.length.toLocaleString('en-US')} game files and the build). ` +
    `Update zip: about ${formatBytes(buildBytes())}.`;
}

// ---- step 3: languages -------------------------------------------------------------------------------------------
async function checkLanguageSupport() {
  // The language choice needs an NRO that serves a missing language's files from an installed one (CONFIG.languages.minBuild).
  try {
    const r = await fetch(new URL(CONFIG.build.manifest, location.href), { cache: 'no-cache' });
    if (r.ok) languagesAllowed = buildAtLeast((await r.json()).tag, CONFIG.languages.minBuild);
  } catch { /* keep the default */ }
}

function showLanguages(found) {
  const box = $('lang-list');
  box.replaceChildren();
  const present = CONFIG.languages.list.filter((l) => languageBytes(found.files, l) > 0);
  if (!present.length) { show($('step-languages'), false); return; }
  const status = $('lang-status');
  if (!languagesAllowed) {
    status.className = 'status';
    status.textContent = `The published build is older than ${CONFIG.languages.minBuild}, so every language is included. The choice appears with the next release.`;
  } else {
    status.textContent = '';
  }
  for (const l of present) {
    const label = document.createElement('label');
    const input = document.createElement('input');
    input.type = 'checkbox';
    input.checked = true;
    input.disabled = !languagesAllowed;
    input.dataset.lang = l.id;
    input.addEventListener('change', () => {
      if (input.checked) selectedLanguages.add(l.id); else selectedLanguages.delete(l.id);
      if (selectedLanguages.size === 0) { selectedLanguages.add(l.id); input.checked = true; status.className = 'status error'; status.textContent = 'At least one language must stay selected.'; }
      else status.textContent = '';
      updateCreateInfo();
    });
    const name = document.createElement('span');
    name.textContent = l.name;
    const size = document.createElement('span');
    size.className = 'size';
    size.textContent = formatBytes(languageBytes(found.files, l));
    label.append(input, name, size);
    box.append(label);
  }
  show($('step-languages'), true);
}

function wireSource() {
  const pickIso = $('file-iso');
  const pickFolder = $('file-folder');
  $('pick').addEventListener('click', () => (currentFormat() === 'iso' ? pickIso : pickFolder).click());
  pickIso.addEventListener('change', () => { const f = pickIso.files[0]; if (f) loadSource(() => sourceFromIso(f)); });
  pickFolder.addEventListener('change', () => { if (pickFolder.files.length) loadSource(async () => sourceFromFileList(pickFolder.files)); });
  document.querySelectorAll('input[name="format"]').forEach((r) => r.addEventListener('change', updateFormatUi));
  const drop = $('drop');
  drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('over'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('over'));
  drop.addEventListener('drop', (e) => {
    e.preventDefault();
    drop.classList.remove('over');
    loadSource(async () => {
      const r = await sourceFromDataTransfer(e.dataTransfer, (n) => { $('source-status').textContent = `Reading the folder: ${n} files...`; });
      if (r.source) return r.source;
      return sourceFromIso(r.file);
    });
  });
  updateFormatUi();
}

// ---- step 3: run -----------------------------------------------------------------------------------------------
function createWorker(kind) {
  return new Worker(new URL(`./workers/${kind}.worker.js?v=4`, import.meta.url), { type: 'module' });
}

function buildStageList() {
  const ol = $('stages');
  ol.replaceChildren();
  for (const id of stagesFor(CONFIG)) {
    const li = document.createElement('li');
    li.className = 'stage pending';
    li.dataset.stage = id;
    li.innerHTML = '<span class="slabel"></span><span class="bar"><span class="fill" style="width:0%"></span></span>';
    li.querySelector('.slabel').textContent = STAGE_LABELS[id];
    ol.append(li);
  }
}

function setStage(id, p) {
  const li = document.querySelector(`#stages [data-stage="${id}"]`);
  if (!li) return;
  const pct = p.total ? Math.min(100, Math.round((p.done / p.total) * 100)) : 0;
  li.className = `stage ${pct >= 100 ? 'done' : 'active'}`;
  li.querySelector('.slabel').textContent = p.label || STAGE_LABELS[id];
  li.querySelector('.fill').style.width = `${pct}%`;
}

function log(text, level = 'info') {
  const pre = $('log');
  pre.textContent += `[${level}] ${text}\n`;
  $('log-count').textContent = `(${pre.textContent.split('\n').length - 1} lines)`;
}

function showResult(kind, html) {
  const r = $('result');
  r.className = `notice ${kind}`;
  r.innerHTML = html;
  show(r, true);
}

async function create(mode) {
  if (!disc?.edition || running) return;
  const name = mode === 'full' ? CONFIG.zip.fullName : CONFIG.zip.updateName;
  const plan = planGameFiles(disc.files, CONFIG.disc, languageSkips(CONFIG.languages, selectedLanguages));
  const expected = buildBytes() + (mode === 'full' ? plan.copyBytes : 0);
  const sink = await openSink(name, expected); // needs the click's user gesture
  if (!sink) return;
  const controller = new AbortController();
  running = { controller };
  show($('result'), false);
  show($('progress'), true);
  $('log').textContent = '';
  buildStageList();
  $('create-full').disabled = $('create-update').disabled = true;
  const started = Date.now();
  const timer = setInterval(() => { $('elapsed').textContent = `Elapsed ${formatDuration((Date.now() - started) / 1000)}`; }, 1000);
  try {
    const result = await run({
      config: CONFIG, edition: disc.edition, files: disc.files, mode, sink, createWorker, languages: selectedLanguages,
      baseUrl: location.href, signal: controller.signal,
      onProgress: setStage, onLog: log,
    });
    showResult('ok', `Done: <strong>${name}</strong> (${formatBytes(result.zipBytes)}). Extract it into <code>sdmc:/switch/</code> on the SD card.` +
      (mode === 'full' ? '' : ' (This is an update: extract it over your existing <code>bakugan-nx</code> folder.)') +
      (CONFIG.shaderMode === 'embedded' ? ' <strong>Before you extract it, delete the old shader and cache files from the SD card</strong> (see the list below).' : ''));
  } catch (e) {
    if (e instanceof Cancelled) showResult('warn', 'Cancelled.');
    else {
      log(e.stack || String(e), 'error');
      showResult('error', `${e instanceof UserError ? '' : 'Unexpected error: '}${e.message}`);
    }
  } finally {
    clearInterval(timer);
    running = null;
    $('create-full').disabled = $('create-update').disabled = false;
  }
}

function wireCreate() {
  $('create-full').addEventListener('click', () => create('full'));
  $('create-update').addEventListener('click', () => create('update'));
  $('cancel').addEventListener('click', () => running?.controller.abort());
}

// ---- start -----------------------------------------------------------------------------------------------------
$('releases-link').href = CONFIG.project.releasesUrl;
$('repo-link').href = CONFIG.project.repoUrl;
$('author').textContent = CONFIG.project.author;
cleanStaleTemporaryFiles();
showObsolete();
checkEnvironment();
checkLanguageSupport();
wireSource();
wireCreate();
