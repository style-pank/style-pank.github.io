(function () {
  'use strict';

  var section = document.getElementById('comment-section');
  if (!section) return;

  var slug      = section.dataset.slug;
  var listEl    = document.getElementById('comment-list');
  var countEl   = document.getElementById('comment-count');
  var textarea  = document.getElementById('comment-body');
  var submitBtn = document.getElementById('comment-submit');

  var i18n = {
    sectionTitle : '댓글',
    formTitle    : '댓글 작성',
    note         : 'GitHub 계정으로 댓글을 작성할 수 있습니다.',
    placeholder  : '댓글을 입력하세요...',
    submitLabel  : 'GitHub으로 댓글 달기',
    loading      : '로드 중...',
    empty        : '아직 댓글이 없습니다.',
    error        : '댓글을 불러올 수 없습니다.',
    alertEmpty   : '댓글을 입력해 주세요.',
    count        : function (n) { return n + '개의 댓글'; }
  };

  var el;
  el = document.getElementById('comment-section-title');
  if (el) el.textContent = i18n.sectionTitle;
  el = document.getElementById('comment-form-title');
  if (el) el.textContent = i18n.formTitle;
  el = document.getElementById('comment-form-note');
  if (el) el.textContent = i18n.note;
  if (textarea)  textarea.placeholder = i18n.placeholder;
  if (submitBtn) submitBtn.textContent = i18n.submitLabel;
  if (countEl)   countEl.textContent   = i18n.loading;

  function escapeHtml(str) {
    return String(str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function renderFallback(list) {
    return list.map(function (c) {
      var date = c.created_at ? c.created_at.slice(0, 10) : '';
      return '<div class="comment"><div class="comment-meta"><span class="comment-author">' +
        escapeHtml(c.author || 'Anonymous') + '</span><span class="comment-date">' +
        escapeHtml(date) + '</span></div><div class="comment-body"><p>' +
        escapeHtml(c.body) + '</p></div></div>';
    }).join('');
  }

  var cwistReady = window.CwistBlog
    ? window.CwistBlog.ready
    : Promise.reject(new Error('cwist-runtime.js not loaded'));

  fetch('/data/comments.json?v=' + Date.now(), { cache: 'no-store' })
    .then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    })
    .then(function (data) {
      var list = Array.isArray(data[slug]) ? data[slug] : [];
      if (countEl) {
        countEl.textContent = list.length ? i18n.count(list.length) : '';
      }
      if (!listEl) return;
      listEl.innerHTML = '';
      if (!list.length) {
        var emptyEl = document.createElement('p');
        emptyEl.className = 'no-comments';
        emptyEl.textContent = i18n.empty;
        listEl.appendChild(emptyEl);
        return;
      }

      // Comment markdown is rendered (raw HTML disabled) by the CWIST component.
      return cwistReady
        .then(function (cwist) {
          return cwist.text('POST', '/render/comments', JSON.stringify(list));
        })
        .catch(function (err) {
          console.warn('[comments] CWIST component render failed, using fallback', err);
          return renderFallback(list);
        })
        .then(function (html) {
          listEl.innerHTML = html;
        });
    })
    .catch(function (err) {
      console.error('[comments] load error', err);
      if (countEl) countEl.textContent = '';
      if (listEl) {
        listEl.innerHTML = '';
        var errorEl = document.createElement('p');
        errorEl.className = 'no-comments';
        errorEl.textContent = i18n.error;
        listEl.appendChild(errorEl);
      }
    });

  var REPO     = 'style-pank/style-pank.github.io';
  var BASE_URL = 'https://github.com/' + REPO + '/issues/new';

  if (submitBtn && textarea) {
    submitBtn.addEventListener('click', function (e) {
      e.preventDefault();
      var body = textarea.value.trim();
      if (!body) {
        alert(i18n.alertEmpty);
        return;
      }
      var issueTitle = '[comment] ' + slug;
      var issueUrl =
        BASE_URL +
        '?title=' + encodeURIComponent(issueTitle) +
        '&body='  + encodeURIComponent(body) +
        '&labels=comment';
      window.open(issueUrl, '_blank', 'noopener,noreferrer');
    });
  }
}());
