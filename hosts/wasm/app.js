// SPDX-License-Identifier: MIT
(() => {
  'use strict';

  const STATUS_NAMES = [
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

  const DEFAULT_ROM = 'boot.rom';

  const canvas = document.getElementById('screen');
  const ctx = canvas.getContext('2d');
  const consoleEl = document.getElementById('console');
  const statusEl = document.getElementById('status');
  const runBtn = document.getElementById('run');
  const resetBtn = document.getElementById('reset');
  const loadBtn = document.getElementById('load');
  const romInput = document.getElementById('rom');
  const inputForm = document.getElementById('input-form');
  const consoleInput = document.getElementById('console-input');
  const screenSizeEl = document.getElementById('screen-size');
  const sizeButtons = Array.from(document.querySelectorAll('[data-screen-w]'));

  let startMs = performance.now();
  let currentR = 0;
  let currentG = 0;
  let currentB = 0;
  let frame = null;
  let frameBytes = null;
  let screenW = 320;
  let screenH = 240;

  /* The host keeps an offscreen framebuffer so a partially drawn VM frame is
   * never presented. This mirrors the SDL3 host: CLEAR and PIXEL only write to
   * the buffer, and DRAW presents the finished frame. */
  function present() {
    if (frame) ctx.putImageData(frame, 0, 0);
  }

  function fillFrame() {
    for (let i = 0; i < frameBytes.length; i += 4) {
      frameBytes[i] = currentR;
      frameBytes[i + 1] = currentG;
      frameBytes[i + 2] = currentB;
      frameBytes[i + 3] = 255;
    }
  }

  /* The Emscripten library calls into this object. */
  globalThis.__0xvmHost = {
    screen: {
      init(width, height) {
        canvas.width = width;
        canvas.height = height;
        screenSizeEl.textContent = `${width} × ${height}`;
        ctx.imageSmoothingEnabled = false;
        frame = ctx.createImageData(width, height);
        frameBytes = frame.data;
        this.reset();
        this.clear();
        present();
      },
      reset() {
        startMs = performance.now();
      },
      now() {
        return performance.now() - startMs;
      },
      color(r, g, b) {
        currentR = r;
        currentG = g;
        currentB = b;
      },
      pixel(x, y) {
        if (!frame || x < 0 || y < 0 || x >= frame.width || y >= frame.height) return;
        const i = (y * frame.width + x) * 4;
        frameBytes[i] = currentR;
        frameBytes[i + 1] = currentG;
        frameBytes[i + 2] = currentB;
        frameBytes[i + 3] = 255;
      },
      draw() {
        present();
      },
      clear() {
        fillFrame();
      }
    },
    console: {
      write(text) {
        consoleEl.textContent += text;
        consoleEl.scrollTop = consoleEl.scrollHeight;
      }
    }
  };

  function setStatus(text, state = 'idle') {
    statusEl.textContent = text;
    statusEl.dataset.state = state;
  }

  function statusState(st) {
    if (st === 0) return 'running';
    if (st === 1 || st === 10) return 'halted';
    return 'error';
  }

  function mapKey(event) {
    switch (event.key) {
      case 'ArrowUp': return 1;
      case 'ArrowDown': return 2;
      case 'ArrowLeft': return 3;
      case 'ArrowRight': return 4;
      case ' ': return 5;
      case 'Enter': return 6;
      case 'Escape': return 7;
      case 'Backspace': return 8;
      case 'Delete': return 9;
      default: break;
    }
    if (event.key.length === 1) {
      const code = event.key.charCodeAt(0);
      if (code >= 32 && code <= 126) return code;
    }
    return 0;
  }

  async function start() {
    if (typeof create0xvm !== 'function') {
      setStatus('0xvm-wasm.js did not load', 'error');
      return;
    }

    const wasmUrl = new URL('0xvm-wasm.wasm', document.baseURI).href;
    const mod = await create0xvm({
      locateFile(path) {
        return path.endsWith('.wasm') ? wasmUrl : path;
      }
    });

    const api = {
      init: mod.cwrap('vmw_init', 'number', ['number', 'number']),
      load: mod.cwrap('vmw_load', 'number', ['array', 'number']),
      run: mod.cwrap('vmw_run', 'number', ['number']),
      ticks: mod.cwrap('vmw_ticks', 'number', []),
      pushKey(code) {
        mod.ccall('vmw_push_key', null, ['number'], [code]);
      },
      quit() {
        mod.ccall('vmw_request_quit', null, [], []);
      },
      consoleInput(text) {
        mod.ccall('vmw_console_input', null, ['string'], [text + '\n']);
      }
    };

    let running = false;
    let romBytes = null;
    let romName = '';

    function load(bytes, name) {
      running = false;
      romBytes = bytes;
      romName = name;
      api.init(screenW, screenH);
      consoleEl.textContent = '';
      const st = api.load(bytes, bytes.length);
      setStatus(`${name}: ${STATUS_NAMES[st] || 'UNKNOWN'}`, statusState(st));
      running = st === 0;
      runBtn.textContent = running ? 'Pause' : 'Run';
      canvas.focus();
    }

    async function loadRomUrl(url) {
      try {
        // ROMs are rebuilt often while developing; never serve a cached copy.
        const response = await fetch(url, { cache: 'no-store' });
        if (!response.ok) throw new Error(String(response.status));
        load(Array.from(new Uint8Array(await response.arrayBuffer())), url);
      } catch (err) {
        setStatus(`Could not load ${url}; choose a ROM file`, 'error');
      }
    }

    function markSize() {
      for (const button of sizeButtons) {
        const active = Number(button.dataset.screenW) === screenW &&
                       Number(button.dataset.screenH) === screenH;
        button.classList.toggle('primary', active);
        button.setAttribute('aria-pressed', String(active));
      }
    }

    function applySize(w, h) {
      if (w < 16 || w > 4096 || h < 16 || h > 4096) return;
      screenW = w;
      screenH = h;
      markSize();
      if (romBytes) {
        load(romBytes, romName);
      } else {
        api.init(screenW, screenH);
        setStatus(`screen ${screenW} × ${screenH}`, 'idle');
      }
    }

    markSize();

    function pump() {
      if (running) {
        const st = api.run(250000);
        if (st === 0) {
          setStatus(`${romName} · Running · ${api.ticks()} ticks`, 'running');
        } else {
          running = false;
          runBtn.textContent = 'Run';
          const state = statusState(st);
          const label = STATUS_NAMES[st] || st;
          setStatus(
            `${romName} · ${state === 'error' ? 'ERROR ' : ''}${label} · ${api.ticks()} ticks`,
            state
          );
        }
      }
      requestAnimationFrame(pump);
    }

    runBtn.disabled = false;
    resetBtn.disabled = false;
    runBtn.addEventListener('click', () => {
      if (!romBytes) return;
      running = !running;
      runBtn.textContent = running ? 'Pause' : 'Run';
    });
    resetBtn.addEventListener('click', () => {
      if (romBytes) load(romBytes, romName);
    });

    for (const button of sizeButtons) {
      button.addEventListener('click', () => {
        applySize(
          Number(button.dataset.screenW),
          Number(button.dataset.screenH)
        );
      });
    }

    loadBtn.addEventListener('click', () => romInput.click());
    romInput.addEventListener('change', async () => {
      const file = romInput.files && romInput.files[0];
      if (!file) return;
      romInput.value = '';
      load(Array.from(new Uint8Array(await file.arrayBuffer())), file.name);
    });

    inputForm.addEventListener('submit', (event) => {
      event.preventDefault();
      const text = consoleInput.value;
      consoleInput.value = '';
      if (!text) return;
      api.consoleInput(text);
      if (!running && romBytes) {
        running = true;
        runBtn.textContent = 'Pause';
      }
    });

    window.addEventListener('keydown', (event) => {
      if (event.target.closest && event.target.closest('form')) return;
      const code = mapKey(event);
      if (code !== 0) {
        api.pushKey(code);
        event.preventDefault();
      }
    });

    window.addEventListener('beforeunload', () => api.quit());

    await loadRomUrl(DEFAULT_ROM);

    requestAnimationFrame(pump);
  }

  start().catch((err) => setStatus(String(err), 'error'));
})();
