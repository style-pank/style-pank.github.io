(function () {
  'use strict';

  var STORAGE_KEY = 'cwist-theme';
  var DEFAULT_THEME = 'dark';
  var root = document.documentElement;

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

  // Section accent comes from the CWIST component (POST /theme).
  function syncMenuAccent(theme) {
    var section = theme && theme.section;
    if (!section) return;

    if (theme.accent && theme.hover) {
      root.style.setProperty('--accent', theme.accent);
      root.style.setProperty('--accent-hover', theme.hover);
      var rgb = theme.rgb;
      if (rgb && rgb.length === 3) {
        var base = rgb[0] + ', ' + rgb[1] + ', ' + rgb[2];
        root.style.setProperty('--accent-dim', 'rgba(' + base + ', 0.12)');
        root.style.setProperty('--accent-glow', 'rgba(' + base + ', 0.22)');
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

  if (window.CwistBlog) {
    window.CwistBlog.ready
      .then(function (cwist) { return cwist.json('POST', '/theme', window.location.pathname); })
      .then(syncMenuAccent)
      .catch(function () {});
  }
}());
