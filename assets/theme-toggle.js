(function () {
  'use strict';

  var STORAGE_KEY = 'cwist-theme';
  var DEFAULT_THEME = 'dark';
  var root = document.documentElement;

  var wasmMod = null;

  function hexToRgb(hex) {
    if (wasmMod && typeof wasmMod.ccall === 'function') {
      var rPtr = wasmMod._malloc(4);
      var gPtr = wasmMod._malloc(4);
      var bPtr = wasmMod._malloc(4);
      wasmMod.ccall('cwist_hex_to_rgb', null, ['string', 'number', 'number', 'number'], [hex, rPtr, gPtr, bPtr]);
      var r = wasmMod.getValue(rPtr, 'i32');
      var g = wasmMod.getValue(gPtr, 'i32');
      var b = wasmMod.getValue(bPtr, 'i32');
      wasmMod._free(rPtr);
      wasmMod._free(gPtr);
      wasmMod._free(bPtr);
      return { r: r, g: g, b: b };
    }
    // Minimal fallback
    var normalized = String(hex || '').replace('#', '');
    if (normalized.length !== 6) return null;
    var value = parseInt(normalized, 16);
    return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 };
  }

  function applyTheme(theme) {
    if (theme === 'light' || theme === 'dark') {
      root.setAttribute('data-theme', theme);
    } else {
      root.removeAttribute('data-theme');
    }
    updateButtons();
  }

  function getSavedTheme() {
    try {
      return localStorage.getItem(STORAGE_KEY);
    } catch (_) {
      return null;
    }
  }

  function saveTheme(theme) {
    try {
      localStorage.setItem(STORAGE_KEY, theme);
    } catch (_) {
    }
  }

  function resolveTheme() {
    var explicit = getSavedTheme();
    if (explicit === 'light' || explicit === 'dark') return explicit;
    if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches) {
      return 'light';
    }
    return DEFAULT_THEME;
  }

  function getCurrentTheme() {
    var explicit = root.getAttribute('data-theme');
    if (explicit === 'light' || explicit === 'dark') return explicit;
    return resolveTheme();
  }

  function updateButtons() {
    var current = getCurrentTheme();
    var next = current === 'light' ? 'dark' : 'light';
    var text = current === 'light' ? '🌙 Dark' : '☀️ Light';
    var buttons = document.querySelectorAll('[data-theme-toggle]');
    for (var i = 0; i < buttons.length; i += 1) {
      buttons[i].setAttribute('aria-label', next + ' mode');
      buttons[i].setAttribute('title', next + ' mode');
      buttons[i].textContent = text;
    }
  }

  function onToggleClick() {
    var next = getCurrentTheme() === 'light' ? 'dark' : 'light';
    applyTheme(next);
    saveTheme(next);
  }

  function setupButtons() {
    var buttons = document.querySelectorAll('[data-theme-toggle]');
    for (var i = 0; i < buttons.length; i += 1) {
      buttons[i].addEventListener('click', onToggleClick);
    }
    updateButtons();
  }

  function syncMenuAccent() {
    if (!wasmMod) return;

    var sectionPtr = wasmMod.ccall('cwist_get_section_from_path', 'number', ['string'], [window.location.pathname]);
    var section = wasmMod.UTF8ToString(sectionPtr);
    if (!section) return;

    var accentPtr = wasmMod.ccall('cwist_get_accent_for_section', 'number', ['string'], [section]);
    var accent = wasmMod.UTF8ToString(accentPtr);
    var hoverPtr = wasmMod.ccall('cwist_get_hover_for_section', 'number', ['string'], [section]);
    var hover = wasmMod.UTF8ToString(hoverPtr);

    if (accent && hover) {
      root.style.setProperty('--accent', accent);
      root.style.setProperty('--accent-hover', hover);
      var rgb = hexToRgb(accent);
      if (rgb) {
        root.style.setProperty('--accent-dim', 'rgba(' + rgb.r + ', ' + rgb.g + ', ' + rgb.b + ', 0.12)');
        root.style.setProperty('--accent-glow', 'rgba(' + rgb.r + ', ' + rgb.g + ', ' + rgb.b + ', 0.22)');
      }
    }

    var navLinks = document.querySelectorAll('.nav-list a');
    for (var i = 0; i < navLinks.length; i += 1) {
      var link = navLinks[i];
      var href = link.getAttribute('href') || '';
      if (section === 'search' && href.indexOf('/search/') !== -1) {
        link.classList.add('active');
      } else if (href.indexOf('/category/' + section + '/') !== -1) {
        link.classList.add('active');
      }
    }
  }

  var initial = resolveTheme();
  applyTheme(initial);

  setupButtons();

  var wasmReady = window._cwistWasmPromise || (window._cwistWasmPromise = (typeof CwistSearchModule === 'function')
    ? CwistSearchModule().catch(function () { return null; })
    : Promise.resolve(null));

  wasmReady.then(function (mod) {
    wasmMod = mod;
    syncMenuAccent();
  });
}());
