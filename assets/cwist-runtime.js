/*
 * Loader for the CWIST WASI 0.2 render component (assets/cwist-blog.mjs).
 *
 * Classic page scripts run before module scripts, so this publishes
 * window.CwistBlog.ready up front and injects the module bundle next to
 * itself. Consumers do:
 *
 *   CwistBlog.ready.then(function (cwist) {
 *     return cwist.text('POST', '/render/markdown', source);
 *   });
 *
 * ready rejects when the component cannot load; every consumer keeps a
 * plain-JS fallback for that case.
 */
(function () {
  'use strict';

  if (window.CwistBlog) return;

  var resolveFn;
  var rejectFn;
  var ready = new Promise(function (resolve, reject) {
    resolveFn = resolve;
    rejectFn = reject;
  });
  window.CwistBlog = { ready: ready, _resolve: resolveFn, _reject: rejectFn };

  var self = document.currentScript;
  var src = self && self.src
    ? self.src.replace(/cwist-runtime\.js(\?.*)?$/, 'cwist-blog.mjs')
    : 'assets/cwist-blog.mjs';

  if (typeof WebAssembly !== 'object') {
    rejectFn(new Error('WebAssembly unavailable'));
    return;
  }

  var script = document.createElement('script');
  script.type = 'module';
  script.src = src;
  script.onerror = function () {
    rejectFn(new Error('failed to load ' + src));
  };
  document.head.appendChild(script);
}());
