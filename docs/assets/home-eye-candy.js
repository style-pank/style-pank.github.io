(function () {
  'use strict';

  var mount = document.getElementById('home-eye-candy');
  if (!mount) return;

  var title = '블로그 빠른 둘러보기';
  var desc = '카테고리 중심으로 최근 글 흐름을 바로 확인할 수 있습니다.';

  function collectChips() {
    var titles = Array.prototype.slice.call(
      document.querySelectorAll('.cat-card-title')
    )
      .map(function (el) { return (el.textContent || '').trim(); })
      .filter(Boolean)
      .slice(0, 5);
    if (!titles.length) return '포스트,카테고리,검색';
    return titles.join(',');
  }

  var palette = collectChips();

  function escapeHtml(str) {
    return String(str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  function renderFallback() {
    return [
      '<section class="han-eye">',
      '  <div class="han-eye-header">',
      '    <h2 class="han-eye-title">' + escapeHtml(title) + '</h2>',
      '    <p class="han-eye-desc">' + escapeHtml(desc) + '</p>',
      '  </div>',
      '  <div class="han-palette">',
      palette.split(',').map(function (item) {
        return '<span class="han-chip">' + escapeHtml(item) + '</span>';
      }).join(''),
      '  </div>',
      '</section>'
    ].join('');
  }

  // Paint the plain version immediately, then swap in the component render.
  mount.innerHTML = renderFallback();
  if (window.CwistBlog) {
    window.CwistBlog.ready
      .then(function (cwist) {
        return cwist.text('POST', '/render/home',
          JSON.stringify({ title: title, description: desc, chips: palette }));
      })
      .then(function (html) { mount.innerHTML = html; })
      .catch(function () {});
  }
}());
