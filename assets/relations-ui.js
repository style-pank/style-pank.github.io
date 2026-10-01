(function () {
  'use strict';

  var canvas = document.getElementById('relations-canvas');
  var legend = document.getElementById('relations-legend');
  if (!canvas) return;

  var ctx = canvas.getContext('2d');
  var posts = [];
  var nodes = [];
  var edges = [];
  var rafId = null;
  var hoveredNode = -1;
  var pointer = { x: 0, y: 0, active: false };
  var DPR = window.devicePixelRatio || 1;

  function resize() {
    var rect = canvas.getBoundingClientRect();
    canvas.width = Math.max(640, Math.floor(rect.width * DPR));
    canvas.height = Math.max(420, Math.floor(rect.height * DPR));
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.scale(DPR, DPR);
  }

  function normalizePost(item, idx) {
    var tags = Array.isArray(item.tags) ? item.tags.filter(Boolean).map(function (t) {
      return String(t).trim().toLowerCase();
    }).filter(Boolean) : [];
    var rawUrl = (typeof item.url === 'string' && item.url) ? item.url : '/';
    var normalizedUrl = rawUrl.startsWith('/docs/post/')
      ? rawUrl
      : (rawUrl.startsWith('/post/') ? '/docs' + rawUrl : rawUrl);
    return {
      id: idx,
      title: item.title || 'Untitled',
      url: normalizedUrl,
      tags: tags,
      tagsStr: tags.join(' '),
      degree: 0
    };
  }

  // JS stand-in for the component's tag affinity when it cannot load.
  function fallbackEdges() {
    var out = [];
    for (var a = 0; a < posts.length; a++) {
      for (var b = a + 1; b < posts.length; b++) {
        var common = 0;
        for (var i = 0; i < posts[a].tags.length; i++) {
          if (posts[b].tags.indexOf(posts[a].tags[i]) !== -1) common += 1;
        }
        if (common > 0) out.push({ a: a, b: b, w: common * 2.2 });
      }
    }
    return out;
  }

  function isLightTheme() {
    var root = document.documentElement;
    var explicit = root.getAttribute('data-theme');
    if (explicit === 'light') return true;
    if (explicit === 'dark') return false;
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches;
  }

  function setLegend(text) {
    if (legend) legend.textContent = text;
  }

  function stageSize() {
    return {
      width: canvas.clientWidth || 640,
      height: canvas.clientHeight || 420
    };
  }

  function createNode(post, angle, radius, cx, cy) {
    var x = cx + Math.cos(angle) * radius;
    var y = cy + Math.sin(angle) * radius;
    return {
      post: post,
      x: x,
      y: y,
      homeX: x,
      homeY: y
    };
  }

  function buildMesh(pairEdges) {
    var stage = stageSize();
    var cx = stage.width / 2;
    var cy = stage.height / 2;
    var ringGap = Math.min(stage.width, stage.height) * 0.17;
    var ring = 0;
    var inRing = 0;
    var ringSlots = 1;

    nodes = [];
    posts.forEach(function (post) { post.degree = 0; });
    for (var i = 0; i < posts.length; i++) {
      if (inRing >= ringSlots) {
        ring += 1;
        inRing = 0;
        ringSlots = Math.max(6, ring * 8);
      }
      var angle = ((Math.PI * 2) / ringSlots) * inRing + (ring * Math.PI / 8);
      var radius = ring === 0 ? 0 : ring * ringGap;
      nodes.push(createNode(posts[i], angle, radius, cx, cy));
      inRing += 1;
    }

    edges = [];
    pairEdges.forEach(function (e) {
      if (!posts[e.a] || !posts[e.b] || e.w <= 0) return;
      edges.push({ a: e.a, b: e.b, w: Math.min(10, e.w) });
      posts[e.a].degree += 1;
      posts[e.b].degree += 1;
    });

    edges.sort(function (a, b) { return b.w - a.w; });
    edges = edges.slice(0, Math.min(140, Math.max(posts.length * 4, 24)));

    setLegend('노드 ' + nodes.length + '개 · 연결 ' + edges.length + '개');
  }

  function moveNode(node, width, height) {
    var clampedX = node.homeX;
    var clampedY = node.homeY;
    if (clampedX < 24) clampedX = 24;
    if (clampedX > width - 24) clampedX = width - 24;
    if (clampedY < 24) clampedY = 24;
    if (clampedY > height - 24) clampedY = height - 24;
    node.homeX = clampedX;
    node.homeY = clampedY;
    node.x = clampedX;
    node.y = clampedY;
  }

  function nearestNode(x, y) {
    var bestIdx = -1;
    var bestDist = 20;
    for (var i = 0; i < nodes.length; i++) {
      var dx = nodes[i].x - x;
      var dy = nodes[i].y - y;
      var d = Math.sqrt(dx * dx + dy * dy);
      if (d < bestDist) {
        bestDist = d;
        bestIdx = i;
      }
    }
    return bestIdx;
  }

  function step() {
    var width = canvas.clientWidth;
    var height = canvas.clientHeight;

    ctx.clearRect(0, 0, width, height);

    for (var i = 0; i < edges.length; i++) {
      var edge = edges[i];
      var a = nodes[edge.a];
      var b = nodes[edge.b];
      if (!a || !b) continue;
      var highlight = hoveredNode === edge.a || hoveredNode === edge.b;
      ctx.lineWidth = 0.45 + edge.w * 0.16 + (highlight ? 1.15 : 0);
      ctx.strokeStyle = highlight ? 'rgba(238, 179, 88, 0.82)' : 'rgba(196, 75, 59, 0.34)';
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      var dx = b.x - a.x;
      var dy = b.y - a.y;
      var dist = Math.sqrt(dx * dx + dy * dy);
      var cx = (a.x + b.x) / 2 + dy * 0.08;
      var curvature = Math.pow(Math.max(dist, 1), 0.62) * 0.95 + edge.w * 0.8;
      var cy = (a.y + b.y) / 2 - curvature;
      ctx.quadraticCurveTo(cx, cy, b.x, b.y);
      ctx.stroke();
    }

    nodes.forEach(function (n, idx) {
      moveNode(n, width, height);
      var active = idx === hoveredNode;
      var size = active ? 8 : 6;
      ctx.fillStyle = active ? '#a8925e' : '#bf5f45';
      ctx.strokeStyle = 'rgba(255,255,255,0.4)';
      ctx.lineWidth = active ? 1.4 : 1;

      ctx.save();
      ctx.translate(n.x, n.y);
      ctx.rotate(Math.PI / 4 + (active ? Math.PI / 8 : 0));
      ctx.fillRect(-size, -size, size * 2, size * 2);
      ctx.restore();

      ctx.fillStyle = isLightTheme() ? 'rgba(29, 34, 51, 0.9)' : 'rgba(237, 240, 248, 0.9)';
      ctx.font = (active ? '700 12px' : '12px') + ' NixgonFont, sans-serif';
      ctx.fillText(n.post.title.slice(0, 18), n.x + 10, n.y - 10);
    });

    rafId = window.requestAnimationFrame(step);
  }

  function init() {
    resize();

    var cwistReady = window.CwistBlog
      ? window.CwistBlog.ready
      : Promise.reject(new Error('cwist-runtime.js not loaded'));

    fetch('../search-index.json?v=' + Date.now(), { cache: 'no-store' })
      .then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
      .then(function (text) {
        var data = JSON.parse(text);
        posts = (Array.isArray(data) ? data : []).map(normalizePost);
        // Every post pair is scored inside the CWIST component in one call.
        return cwistReady
          .then(function (cwist) { return cwist.json('POST', '/relations', text); })
          .then(function (res) { return res && Array.isArray(res.edges) ? res.edges : []; })
          .catch(function (err) {
            console.warn('[relations-ui] CWIST component unavailable, using fallback', err);
            return fallbackEdges();
          });
      })
      .then(function (pairEdges) {
        buildMesh(pairEdges);
        step();
      })
      .catch(function (err) {
        setLegend('관계 데이터를 불러오지 못했습니다.');
        console.error('[relations-ui] init error', err);
      });
  }

  canvas.addEventListener('mousemove', function (ev) {
    var rect = canvas.getBoundingClientRect();
    pointer.x = ev.clientX - rect.left;
    pointer.y = ev.clientY - rect.top;
    pointer.active = true;
    hoveredNode = nearestNode(pointer.x, pointer.y);
    canvas.style.cursor = hoveredNode >= 0 ? 'pointer' : 'default';
    if (hoveredNode >= 0) {
      setLegend(nodes[hoveredNode].post.title + ' · 태그 ' + nodes[hoveredNode].post.tags.length + '개');
    } else {
      setLegend('노드 ' + nodes.length + '개 · 연결 ' + edges.length + '개');
    }
  });

  canvas.addEventListener('mouseleave', function () {
    pointer.active = false;
    hoveredNode = -1;
    canvas.style.cursor = 'default';
    setLegend('노드 ' + nodes.length + '개 · 연결 ' + edges.length + '개');
  });

  canvas.addEventListener('click', function () {
    if (hoveredNode < 0 || !nodes[hoveredNode]) return;
    window.location.href = nodes[hoveredNode].post.url;
  });

  window.addEventListener('resize', resize);
  window.addEventListener('beforeunload', function () {
    if (rafId) window.cancelAnimationFrame(rafId);
  });

  init();
}());
