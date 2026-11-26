import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';

const [jsPath, romPath, conformanceRom, expectedPath, typographyRom] = process.argv.slice(2);
if (!jsPath || !romPath) {
  console.error('usage: node tests/wasm-smoke.mjs WASM_JS ROM [CONFORMANCE_ROM EXPECTED [TYPOGRAPHY_ROM]]');
  process.exit(2);
}

const STATUS = [
  'VM_OK',
  'VM_HALTED',
  'VM_ERR_BAD_IP',
  'VM_ERR_BAD_OP',
  'VM_ERR_STACK_OVERFLOW',
  'VM_ERR_STACK_UNDERFLOW',
  'VM_ERR_HEAP_OVERFLOW',
  'VM_ERR_BAD_ADDR',
  'VM_ERR_DIV_ZERO',
  'VM_ERR_IO',
  'VM_ERR_BAD_FRAME',
  'VM_ERR_BAD_LOCAL'
];

const output = [];
let fakeNow = 0;
globalThis.__0xvmHost = {
  screen: {
    init() {},
    reset() { fakeNow = 0; },
    now() { fakeNow += 1; return fakeNow; },
    color() {}, pixel() {}, draw() {}, clear() {}
  },
  console: { write(text) { output.push(text); } }
};

const require = createRequire(import.meta.url);
const create0xvm = require(resolve(jsPath));
const wasmPath = join(dirname(resolve(jsPath)), '0xvm-wasm.wasm');
const mod = await create0xvm({
  locateFile(path) {
    return path.endsWith('.wasm') ? wasmPath : path;
  }
});

const api = {
  init: mod.cwrap('vmw_init', 'number', ['number', 'number']),
  load: mod.cwrap('vmw_load', 'number', ['array', 'number']),
  run: mod.cwrap('vmw_run', 'number', ['number']),
  ticks: mod.cwrap('vmw_ticks', 'number', []),
  consoleInput(text) {
    mod.ccall('vmw_console_input', null, ['string'], [text]);
  }
};

function fail(message) {
  console.error(`FAIL ${message}`);
  console.error(output.join(''));
  process.exit(1);
}

const initStatus = api.init(320, 240);
if (initStatus !== 0) fail(`vmw_init returned ${initStatus}`);

const rom = Array.from(readFileSync(romPath));
let status = api.load(rom, rom.length);
if (status !== 0) fail(`vmw_load returned ${status}`);
for (let i = 0; i < 100 && status === 0; i++) status = api.run(100000);

const text = output.join('');
if (!text.includes('SNAKE | arrows move')) fail('missing Snake intro');
if (status !== 1) fail(`expected VM_HALTED, got ${STATUS[status] || status}`);
if (!/SCORE \d\d/.test(text)) fail('missing score output');

console.log(`wasm snake smoke: ${STATUS[status]} (${api.ticks()} ticks)`);

if (conformanceRom) {
  if (!expectedPath) fail('missing expected conformance transcript');
  output.length = 0;
  if (api.init(320, 240) !== 0) fail('second vmw_init failed');
  const conformance = Array.from(readFileSync(conformanceRom));
  status = api.load(conformance, conformance.length);
  if (status !== 0) fail(`conformance vmw_load returned ${status}`);
  api.consoleInput('B-7\nhey\n');
  for (let i = 0; i < 100 && status === 0; i++) status = api.run(100000);
  if (status !== 1) fail(`conformance expected VM_HALTED, got ${STATUS[status] || status}`);

  const actual = output.join('').replace(/\r\n/g, '\n');
  const expected = readFileSync(expectedPath, 'utf8').replace(/\r\n/g, '\n');
  if (actual !== expected) {
    console.error('FAIL wasm conformance transcript mismatch');
    console.error('--- expected ---\n' + expected + '--- actual ---\n' + actual);
    process.exit(1);
  }
  console.log('wasm console conformance: VM_HALTED');
}

if (typographyRom) {
  output.length = 0;
  if (api.init(320, 240) !== 0) fail('typography init failed');
  const typography = Array.from(readFileSync(typographyRom));
  status = api.load(typography, typography.length);
  if (status !== 0) fail(`typography vmw_load returned ${status}`);

  const pushKey = mod.cwrap('vmw_push_key', null, ['number']);
  const peek8 = mod.cwrap('vmw_peek8', 'number', ['number']);
  const rd32 = (addr) => ((peek8(addr) << 24) | (peek8(addr + 1) << 16) |
                          (peek8(addr + 2) << 8) | peek8(addr + 3)) | 0;
  const frames = () => rd32(32);
  const waitFrame = () => {
    const before = frames();
    for (let i = 0; i < 100 && frames() === before && status === 0; i++) {
      status = api.run(10000);
    }
    if (status !== 0) fail(`typography stopped: ${STATUS[status] || status}`);
  };
  const press = (key) => { pushKey(key); waitFrame(); };
  const text = () => {
    const len = rd32(20), addr = rd32(16);
    let s = '';
    for (let i = 0; i < len && i < 32; i++) s += String.fromCharCode(peek8(addr + i));
    return s;
  };

  waitFrame(); // startup draws the initial frame
  press(65); press(66); press(67);          // ABC
  if (text() !== 'ABC') fail(`typography insert: got "${text()}"`);
  press(3); press(3);                       // caret between B and C
  if (rd32(24) !== 1) fail(`typography caret: got ${rd32(24)}`);
  press(8);                                 // backspace removes A
  if (text() !== 'BC') fail(`typography backspace: got "${text()}"`);
  press(88); press(89);                     // XY at the caret -> XYBC
  press(9);                                 // delete removes B
  if (text() !== 'XYC') fail(`typography delete: got "${text()}"`);
  if (rd32(24) !== 2) fail(`typography caret after delete: got ${rd32(24)}`);

  console.log('wasm typography editing: OK');
}
