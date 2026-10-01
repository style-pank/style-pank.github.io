(function () {
  'use strict';

  var inputEl    = document.getElementById('search-input');
  var resultsEl  = document.getElementById('search-results');
  var noResultEl = document.getElementById('search-no-result');

  if (!inputEl || !resultsEl) return;

  var cwist = null;
  var indexLoaded = false;
  var cursor  = -1;
  var renderTimer = null;
  var renderSeq = 0;
  var DEBOUNCE_MS = 120;

  // The index is pushed into the CWIST component once (PUT /search/index);
  // each query is then a POST /search that returns rendered result cards.
  function init() {
    var ready = window.CwistBlog
      ? window.CwistBlog.ready
      : Promise.reject(new Error('cwist-runtime.js not loaded'));

    Promise.all([
      ready,
      fetch('../search-index.json?v=' + Date.now(), { cache: 'no-store' }).then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
    ])
      .then(function (results) {
        cwist = results[0];
        return cwist.text('PUT', '/search/index', results[1]);
      })
      .then(function () {
        indexLoaded = true;
        inputEl.disabled = false;
        inputEl.focus();
        render(inputEl.value);
      })
      .catch(function (e) {
        console.error('[cwist-search] init error:', e);
        inputEl.disabled = false;
      });
  }

  function showResults(html) {
    resultsEl.innerHTML = html;
    if (noResultEl) noResultEl.hidden = html !== '';
    cursor = -1;
  }

  function render(query) {
    var q = (query || '').trim();
    var seq = ++renderSeq;
    if (!q || !indexLoaded) {
      resultsEl.innerHTML = '';
      if (noResultEl) noResultEl.hidden = true;
      cursor = -1;
      return;
    }

    cwist.text('POST', '/search', q)
      .then(function (html) {
        if (seq === renderSeq) showResults(html);
      })
      .catch(function (e) {
        console.error('[cwist-search] query error:', e);
        if (seq === renderSeq) showResults('');
      });
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
