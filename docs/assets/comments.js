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

  var wasmReady = window._cwistWasmPromise || (window._cwistWasmPromise = (typeof CwistSearchModule === 'function')
    ? CwistSearchModule().catch(function () { return null; })
    : Promise.resolve(null));

  Promise.all([
    wasmReady,
    fetch('/data/comments.json?v=' + Date.now(), { cache: 'no-store' }).then(function(r) {
      if (!r.ok) throw new Error('HTTP ' + r.status);
      return r.json();
    })
  ])
    .then(function (results) {
      var mod = results[0];
      var data = results[1];
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
      
      var htmlStrings = [];
      list.forEach(function (c) {
        var author = c.author || 'Anonymous';
        var date = c.created_at ? c.created_at.slice(0, 10) : '';
        var bodyMd = c.body || '';

        if (mod && typeof mod.ccall === 'function') {
          var ptr = mod.ccall('cwist_render_comment', 'number', ['string', 'string', 'string'], [author, date, bodyMd]);
          if (ptr) {
            htmlStrings.push(mod.UTF8ToString(ptr));
            mod.ccall('cwist_free_html', null, ['number'], [ptr]);
          } else {
            htmlStrings.push('<div class="comment"><b>' + author + '</b> ' + date + '<div>' + bodyMd + '</div></div>');
          }
        } else {
          htmlStrings.push('<div class="comment"><b>' + author + '</b> ' + date + '<div>' + bodyMd + '</div></div>');
        }
      });
      listEl.innerHTML = htmlStrings.join('');
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
