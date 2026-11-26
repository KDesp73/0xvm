// SPDX-License-Identifier: MIT
/* Emscripten JS library for the browser host. The page installs
 * globalThis.__0xvmHost with screen/console callbacks before starting the VM. */

mergeInto(LibraryManager.library, {
  vmw_js_screen_init: function(width, height) {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.init === 'function') s.init(width, height);
  },

  vmw_js_screen_color: function(r, g, b) {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.color === 'function') s.color(r, g, b);
  },

  vmw_js_screen_pixel: function(x, y) {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.pixel === 'function') s.pixel(x, y);
  },

  vmw_js_screen_draw: function() {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.draw === 'function') s.draw();
  },

  vmw_js_screen_clear: function() {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.clear === 'function') s.clear();
  },

  vmw_js_timer_reset: function() {
    globalThis.__0xvmWasmStart = Date.now();
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.reset === 'function') s.reset();
  },

  vmw_js_now_ms: function() {
    var h = globalThis.__0xvmHost;
    var s = h && h.screen;
    if (s && typeof s.now === 'function') return s.now();
    return Date.now() - (globalThis.__0xvmWasmStart || Date.now());
  },

  vmw_js_console_write: function(ptr, len) {
    var h = globalThis.__0xvmHost;
    var c = h && h.console;
    var bytes = HEAPU8.subarray(ptr, ptr + len);
    var text = '';
    if (typeof TextDecoder !== 'undefined') {
      text = new TextDecoder('utf-8').decode(bytes);
    } else {
      for (var i = 0; i < bytes.length; i++) {
        text += String.fromCharCode(bytes[i]);
      }
    }
    if (c && typeof c.write === 'function') c.write(text);
    else if (typeof console !== 'undefined') console.log(text);
  }
});
