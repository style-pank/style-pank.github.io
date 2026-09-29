(function () {
  'use strict';

  function text(el) { return (el && el.textContent || '').trim(); }
  function isBrokenHome() {
    var main = document.querySelector('main.main');
    if (!main) return false;
    var catGrid = main.querySelector('.cat-grid');
    if (catGrid && catGrid.children.length > 0) return false;
    var bodyText = text(main);
    return bodyText.indexOf('[dummy]') !== -1 || bodyText.indexOf('fallback') !== -1;
  }

  function groupByCategory(rows) {
    var map = Object.create(null);
    rows.forEach(function (item) {
      var key = item.category || 'general';
      if (!map[key]) map[key] = { title: item.category_title || key, count: 0, desc: '' };
      map[key].count += 1;
      if (!map[key].desc && item.summary) map[key].desc = item.summary;
    });
    return map;
  }

  function renderFallback(map) {
    var main = document.querySelector('main.main');
    if (!main) return;
    var keys = Object.keys(map);
    if (!keys.length) return;
    var cards = keys.map(function (key) {
      var c = map[key];
      return '<a class="cat-card" href="category/' + key + '/">' +
        '<div class="cat-card-bar"></div>' +
        '<div class="cat-card-body">' +
        '<h3 class="cat-card-title">' + c.title + '</h3>' +
        '<p class="cat-card-desc">' + (c.desc || '카테고리 글 모아보기') + '</p>' +
        '<div class="cat-card-meta"><span class="cat-card-count">' + c.count + ' 포스트</span><span class="cat-card-arrow">&rarr;</span></div>' +
        '</div></a>';
    }).join('');

    main.innerHTML = [
      '<section class="hero">',
      '<h1 class="hero-title">Style and Grace</h1>',
      '<p class="hero-desc">카테고리와 최근 글을 불러왔습니다.</p>',
      '</section>',
      '<section><h2 class="section-title">카테고리</h2><div class="cat-grid">', cards, '</div></section>'
    ].join('');
    document.body.classList.add('home-fallback-applied');
  }

  if (!isBrokenHome()) return;
  fetch('search-index.json?v=' + Date.now(), { cache: 'no-store' })
    .then(function (r) { return r.ok ? r.json() : []; })
    .then(function (rows) { if (Array.isArray(rows)) renderFallback(groupByCategory(rows)); })
    .catch(function () {});
}());
