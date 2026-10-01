# Style and Grace

Style and Grace is a GitHub Pages blog whose post bodies, search, comments, and
relations graph are rendered in the browser by a **CWIST WASI 0.2 component**.

The render kernel (`blog/wasm`) is a CWIST app built for `wasm32-wasip2` that
exports CWIST's `cwist-guest` WIT world (`lib/cwist/wit/cwist.wit`). Page
scripts ask it for renders the way a client talks to a CWIST server: one
serialized HTTP/1.1 request per call.

---

## Architecture

### Build pipeline (`make wasm-blog`)

1. **wasi-sdk clang** links `blog/wasm/*.c` + md4c against `libcwist_wasip2` as a reactor core module
2. **wit-bindgen** generates the canonical-ABI bindings for the `cwist-guest` world
3. **wasm-tools component embed** produces the WASI 0.2 component (`blog.component.wasm`)
4. **jco transpile** (`--no-nodejs-compat --tla-compat`) emits a browser ES module plus core wasm shards
5. **esbuild** bundles the jco output, the cwist-wasm component adapter, and the
   preview2-shim browser build into `assets/cwist-blog.mjs`

### Runtime path

1. `assets/cwist-runtime.js`, in every page's `<head>`, publishes `window.CwistBlog.ready`
   and loads `cwist-blog.mjs` as a module.
2. Page scripts call CWIST routes inside the component through the client `ready` resolves to.

| Route | Request body | Response | Used by |
|---|---|---|---|
| `POST /render/markdown` | markdown | HTML | `post-renderer.js` |
| `POST /render/comments` | JSON array of comments | HTML (raw HTML disabled) | `comments.js` |
| `POST /render/home` | `{title,description,chips}` | HTML | `home-eye-candy.js` |
| `PUT /search/index` | `search-index.json` | 204 | `search-ui.js` |
| `POST /search` | query | result-card HTML | `search-ui.js` |
| `POST /relations` | `search-index.json` | `{edges:[{a,b,w}]}` | `relations-ui.js` |
| `POST /theme` | `location.pathname` | section accent JSON | `theme-toggle.js` |

If the component cannot load (`ready` rejects), each script falls back to a plain-JS render.

---

## Directory / component guide

- `blog/wasm/blog_guest.c` — CWIST app routes and the `cwist-guest` exports
- `blog/wasm/*.c` — markdown / comment / search / relations / theme kernels
- `blog/js/cwist-blog-entry.js` — browser bundle entry
- `blog/package.json` — pins jco, preview2-shim, esbuild
- `assets/cwist-runtime.js` — component loader
- `tools/generate_static.c` — page shell / index / navigation generator (native C)
- `posts/<category>/*.md` — post sources
- `docs/` — generated GitHub Pages output

---

## Development and build notes

Initialize only the submodules the build needs:

```bash
git submodule update --init lib/md4c lib/cwist
git -C lib/cwist submodule update --init --filter=blob:none lib/cjson lib/libttak lib/boringssl
```

Toolchain (same pins as `lib/cwist` CI): wasi-sdk 25, wasm-tools 1.259, wit-bindgen 0.62, Node 22.

```bash
make wasm-blog WASI_SDK=$HOME/toolchains/wasi-sdk-25.0-x86_64-linux   # component + bundle
make clean static-site                                                # generate docs/
# or both: make site WASI_SDK=...
```

Point `WASM_TOOLS=` / `WIT_BINDGEN=` at the binaries if they are not on PATH.

---

## Current status and known limitations

- The `cwist-guest` world has a single synchronous `dispatch`, so jco-lowered export calls run on the main thread.
- Category listing pages still use pre-rendered HTML snippets.
- The static generator keeps full post sources in memory while building.

If you are looking for the Korean documentation, see `README.ko.md`.
