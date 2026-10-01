/*
 * Browser entry for the CWIST WASI 0.2 render component.
 *
 * `cwist-blog-component` is the jco transpile of blog/.build/blog.component.wasm
 * (aliased by the Makefile, transpiled with --tla-compat so instantiation is
 * the $init promise rather than a top-level await), with the WASI imports
 * served by @bytecodealliance/preview2-shim's browser build. The
 * cwist-wasm component adapter turns the guest's dispatch export into a
 * fetch-shaped handler, so every render is an HTTP/1.1 request to the CWIST
 * app inside the component (routes: blog/wasm/blog_guest.c).
 *
 * Bundled to assets/cwist-blog.mjs and loaded by assets/cwist-runtime.js,
 * which owns window.CwistBlog.ready.
 */
import { guest, $init } from 'cwist-blog-component';
import { createCwistFromComponent } from '../../lib/cwist/wasm/npm/component.js';

const decoder = new TextDecoder();

function connect() {
  const handle = createCwistFromComponent({
    dispatch: guest.dispatch,
    useSession: guest.useSession,
  });

  async function text(method, path, body) {
    const res = await handle({
      method,
      path,
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: body == null ? '' : String(body),
    });
    const out = decoder.decode(res.body);
    if (res.status >= 400) {
      throw new Error(`cwist ${method} ${path}: ${res.status} ${out}`);
    }
    return out;
  }

  async function json(method, path, body) {
    const out = await text(method, path, body);
    return out ? JSON.parse(out) : null;
  }

  return { text, json, handle };
}

const host = window.CwistBlog;
$init.then(connect).then(
  (api) => {
    if (host && host._resolve) host._resolve(api);
    else window.CwistBlog = { ready: Promise.resolve(api) };
  },
  (err) => {
    console.error('[cwist-blog] component failed to instantiate', err);
    if (host && host._reject) host._reject(err);
  },
);
