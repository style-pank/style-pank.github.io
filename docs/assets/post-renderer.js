(function () {
  'use strict';

  var markdownEl = document.getElementById('post-markdown');
  var bodyEl = document.getElementById('article-body');
  if (!markdownEl || !bodyEl) return;

  function getMarkdownText(el) {
    var raw = el.textContent || '';
    try {
      var parsed = JSON.parse(raw);
      return String(parsed || '').replace(/\r\n/g, '\n');
    } catch (_) {
      return raw.replace(/\r\n/g, '\n');
    }
  }

  function stripFrontMatter(md) {
    var text = String(md || '');
    if (!text.startsWith('---\n')) return text;
    var end = text.indexOf('\n---\n', 4);
    if (end === -1) return text;
    return text.slice(end + 5);
  }

  function escapeHtml(s) {
    return String(s || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  function parseInline(text) {
    var s = escapeHtml(text || '');
    s = s.replace(/~~([^~]+)~~/g, '<del>$1</del>');
    s = s.replace(/`([^`]+)`/g, '<code>$1</code>');
    s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    s = s.replace(/\*([^*]+)\*/g, '<em>$1</em>');
    s = s.replace(/\$\$([\s\S]+?)\$\$/g, '<span class="math-block">$$$1$$</span>');
    s = s.replace(/(^|[^\\])\$([^$\n]+)\$/g, '$1<span class="math-inline">\$$2\$</span>');

    // Image support: ![alt](src)
    s = s.replace(/!\[([^\]]+)\]\(([^)]+)\)/g, '<img src="$2" alt="$1" style="max-width: 100%; height: auto; display: block; margin: 1em auto; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.1);">');

    // Link support: [text](href)
    s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2">$1</a>');

    // Autolinks: <http://...> or <email@...>
    s = s.replace(/&lt;(https?:\/\/[^&]+)&gt;/g, '<a href="$1">$1</a>');
    s = s.replace(/&lt;([^@&]+@[^@&]+\.[^@&]+)&gt;/g, '<a href="mailto:$1">$1</a>');

    // Bare URLs: http://... (preceded by space or start of line)
    s = s.replace(/(^|\s)(https?:\/\/[^\s<"']+)/g, '$1<a href="$2">$2</a>');

    return s;
  }

  function renderMarkdownLite(md) {
    var lines = String(md || '').split('\n');
    var html = [];
    var inCode = false;
    var codeLang = '';
    var listTag = '';
    var inBlockquote = false;
    var inTable = false;
    var tableRows = [];

    function closeList() {
      if (listTag) html.push('</' + listTag + '>');
      listTag = '';
    }
    function closeBlockquote() {
      if (inBlockquote) html.push('</blockquote>');
      inBlockquote = false;
    }
    function flushTable() {
      if (!inTable || !tableRows.length) { inTable = false; tableRows = []; return; }
      var header = tableRows[0];
      var body = tableRows.slice(1);
      html.push('<table><thead><tr>' + header.map(function (c) { return '<th>' + parseInline(c) + '</th>'; }).join('') + '</tr></thead><tbody>');
      body.forEach(function (row) {
        html.push('<tr>' + row.map(function (c) { return '<td>' + parseInline(c) + '</td>'; }).join('') + '</tr>');
      });
      html.push('</tbody></table>');
      inTable = false;
      tableRows = [];
    }

    lines.forEach(function (line) {
      var raw = line || '';
      var fence = raw.trim().match(/^```([A-Za-z0-9_+-]*)\s*$/);
      if (fence) {
        closeList();
        closeBlockquote();
        flushTable();
        if (!inCode) {
          codeLang = (fence[1] || '').toLowerCase();
          var cls = codeLang ? ' class="language-' + codeLang + '"' : '';
          html.push('<pre><code' + cls + '>');
        } else {
          html.push('</code></pre>');
          codeLang = '';
        }
        inCode = !inCode;
        return;
      }
      if (inCode) {
        html.push(escapeHtml(raw) + '\n');
        return;
      }
      if (!raw.trim()) {
        closeList();
        closeBlockquote();
        flushTable();
        return;
      }
      if (/^\s*[-|: ]+\|\s*[-|: ]+/.test(raw)) return;
      if (/^\s*\|.*\|\s*$/.test(raw)) {
        closeList();
        closeBlockquote();
        inTable = true;
        var cells = raw.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map(function (c) { return c.trim(); });
        tableRows.push(cells);
        return;
      }
      flushTable();
      if (/^\s*>\s?/.test(raw)) {
        closeList();
        if (!inBlockquote) { html.push('<blockquote>'); inBlockquote = true; }
        html.push('<p>' + parseInline(raw.replace(/^\s*>\s?/, '')) + '</p>');
        return;
      }
      closeBlockquote();
      if (/^\s*---+\s*$/.test(raw) || /^\s*\*\*\*+\s*$/.test(raw)) {
        closeList();
        html.push('<hr>');
        return;
      }
      var h = raw.match(/^(#{1,4})\s+(.+)$/);
      if (h) {
        closeList();
        closeBlockquote();
        var level = h[1].length;
        html.push('<h' + level + '>' + parseInline(h[2]) + '</h' + level + '>');
        return;
      }
      var task = raw.match(/^\s*[-*]\s+\[( |x|X)\]\s+(.+)$/);
      if (task) {
        if (listTag !== 'ul') { closeList(); listTag = 'ul'; html.push('<ul class="task-list">'); }
        var checked = task[1].toLowerCase() === 'x' ? ' checked' : '';
        html.push('<li class="task-list-item"><input type="checkbox" disabled' + checked + '><span>' + parseInline(task[2]) + '</span></li>');
        return;
      }
      var ul = raw.match(/^\s*[-*]\s+(.+)$/);
      if (ul) {
        if (listTag !== 'ul') { closeList(); listTag = 'ul'; html.push('<ul>'); }
        html.push('<li>' + parseInline(ul[1]) + '</li>');
        return;
      }
      var ol = raw.match(/^\s*\d+\.\s+(.+)$/);
      if (ol) {
        if (listTag !== 'ol') { closeList(); listTag = 'ol'; html.push('<ol>'); }
        html.push('<li>' + parseInline(ol[1]) + '</li>');
        return;
      }
      closeList();
      html.push('<p>' + parseInline(raw) + '</p>');
    });

    closeList();
    closeBlockquote();
    flushTable();
    if (inCode) html.push('</code></pre>');
    return html.join('\n');
  }

  function assetsPrefix() {
    var linkEl = document.querySelector('link[href*="assets/styles.css"]');
    if (!linkEl) return '';
    var href = linkEl.getAttribute('href');
    var idx = href.indexOf('assets/');
    return idx !== -1 ? href.substring(0, idx) : '';
  }

  function fixPaths(html, prefix) {
    if (!prefix) return html;
    // Replace absolute /assets/ paths with relative paths (handles both " and ')
    return html.replace(/(src|href)=["']\/assets\//g, function (match, attr) {
      return attr + '="' + prefix + 'assets/';
    });
  }

  // md4c emits LaTeX spans as <x-equation>; hand them to MathJax as TeX.
  function equationsToTex(root) {
    var eqs = root.querySelectorAll('x-equation');
    for (var i = 0; i < eqs.length; i++) {
      var eq = eqs[i];
      var display = eq.getAttribute('type') === 'display';
      var tex = eq.textContent || '';
      eq.replaceWith(document.createTextNode(display ? '\\[' + tex + '\\]' : '\\(' + tex + '\\)'));
    }
  }

  function processLinks() {
    var links = bodyEl.querySelectorAll('a');
    links.forEach(function (link) {
      var href = link.getAttribute('href');
      if (href && (href.startsWith('http://') || href.startsWith('https://'))) {
        try {
          var url = new URL(href);
          if (url.hostname !== window.location.hostname) {
            link.setAttribute('target', '_blank');
            link.setAttribute('rel', 'noopener noreferrer');
          }
        } catch (e) {}
      }
    });
  }

  function cwistReady() {
    return window.CwistBlog
      ? window.CwistBlog.ready
      : Promise.reject(new Error('cwist-runtime.js not loaded'));
  }

  function renderPost() {
    var source = stripFrontMatter(getMarkdownText(markdownEl)).trim();
    if (!source) {
      bodyEl.innerHTML = '<p>이 게시물 본문이 비어 있습니다.</p>';
      return;
    }

    cwistReady()
      .then(function (cwist) {
        return cwist.text('POST', '/render/markdown', source);
      })
      .catch(function (err) {
        console.warn('[post-renderer] CWIST component render failed, using js fallback', err);
        return renderMarkdownLite(source);
      })
      .then(function (html) {
        bodyEl.innerHTML = fixPaths(html, assetsPrefix());
        equationsToTex(bodyEl);
        processLinks();
        if (window.hljs && typeof window.hljs.highlightAll === 'function') {
          window.hljs.highlightAll();
        }
        if (window.MathJax && typeof window.MathJax.typesetPromise === 'function') {
          window.MathJax.typesetPromise([bodyEl]).catch(function () {});
        }
      });
  }

  renderPost();
}());
