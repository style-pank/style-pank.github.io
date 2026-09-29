(function () {
  'use strict';

  var inputEl    = document.getElementById('search-input');
  var resultsEl  = document.getElementById('search-results');
  var noResultEl = document.getElementById('search-no-result');

  if (!inputEl || !resultsEl) return;

  var rawIndexString = null;
  var wasmMod = null;
  var cursor  = -1;
  var renderTimer = null;
  var DEBOUNCE_MS = 120;

  function init() {
    var wasmReady =
      (typeof CwistSearchModule === 'function')
        ? CwistSearchModule().catch(function () { return null; })
        : Promise.resolve(null);

    wasmReady
      .then(function (m) {
        wasmMod = m;
        return fetch('../search-index.json?v=' + Date.now(), { cache: 'no-store' });
      })
      .then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
      .then(function (text) {
        rawIndexString = text;
        inputEl.disabled = false;
        inputEl.focus();
        render(inputEl.value);
      })
      .catch(function (e) {
        console.error('[cwist-search] init error:', e);
        inputEl.disabled = false;
      });
  }

  function render(query) {
    var q = (query || '').trim();
    if (!q || !rawIndexString || !wasmMod) {
      resultsEl.innerHTML = '';
      if (noResultEl) noResultEl.hidden = true;
      cursor = -1;
      return;
    }

    var htmlPtr = wasmMod.ccall('cwist_search_and_render', 'number', ['string', 'string'], [rawIndexString, q]);
    if (htmlPtr) {
      var html = wasmMod.UTF8ToString(htmlPtr);
      resultsEl.innerHTML = html;
      wasmMod.ccall('cwist_free_html', null, ['number'], [htmlPtr]);
      if (noResultEl) noResultEl.hidden = html !== '';
    } else {
      resultsEl.innerHTML = '';
      if (noResultEl) noResultEl.hidden = false;
    }
    cursor = -1;
  }

  function requestRender() {
    if (renderTimer) clearTimeout(renderTimer);
    renderTimer = setTimeout(function () {
      renderTimer = null;
      render(inputEl.value);
    }, DEBOUNCE_MS);
  }

  inputEl.addEventListener('input', requestRender);

  inputEl.addEventListener('keydown', function (e) {
    var items = resultsEl.querySelectorAll('.search-result');
    if (!items.length) return;

    if (e.key === 'ArrowDown') {
      e.preventDefault();
      cursor = Math.min(cursor + 1, items.length - 1);
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      cursor = Math.max(cursor - 1, -1);
    } else if (e.key === 'Enter' && cursor >= 0) {
      e.preventDefault();
      items[cursor].click();
      return;
    } else {
      return;
    }

    items.forEach(function (el, i) {
      el.classList.toggle('focused', i === cursor);
      if (i === cursor) el.scrollIntoView({ block: 'nearest' });
    });
  });

  init();
}());
