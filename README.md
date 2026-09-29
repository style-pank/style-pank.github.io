# Style and Grace

Style and Grace is a GitHub Pages blog/site that uses a **client-side JavaScript rendering pipeline** as the canonical way to render post content.

Post markdown source is delivered to the browser and rendered at runtime by `assets/post-renderer.js`.

---

## Why client-side rendering

This project keeps markdown rendering logic in one place: the browser renderer.

- One front-matter parsing behavior in one script
- No section-specific markdown rendering logic
- No raw-markdown fallback injection into the article body

This reduces drift between categories and avoids split behavior where some pages render markdown differently.

---

## Architecture overview

### Canonical post rendering path

1. Static page shell is generated with post metadata (title/date/tags/etc).
2. Raw post markdown is embedded in a JSON script payload (`#post-markdown`).
3. `assets/post-renderer.js` parses and renders the markdown payload.
4. JS writes rendered HTML into `#article-body`.

### Secondary tooling

The C static generator (`tools/generate_static.c`, `bin/bloggen`) is used to build page shells, indexes, navigation, and assets in `docs/`.

---

## Rendering pipeline details

- **Input:** full markdown source (possibly with front matter)
- **Front matter split:** performed in `assets/post-renderer.js`
- **Markdown render:** JS renderer in the browser
- **Output:** HTML string injected into article body

Guarantees:

- front matter does not appear in rendered post body
- article body receives rendered HTML output, not raw markdown text

---

## Directory / component guide

- `assets/post-renderer.js`  
  Browser renderer for front matter split and markdown-to-HTML conversion.

- `tools/generate_static.c`  
  Static page shell/index generator (home/category/post/search scaffolding).

- `assets/search-ui.js`, `blog/wasm/search.c`  
  Search runtime and search WASM scoring module.

- `posts/<category>/*.md`  
  Post source markdown files (with optional YAML front matter).

- `docs/`  
  Generated publishable site output for GitHub Pages workflows.

---

## Development and build notes

Initialize submodules first:

```bash
git submodule update --init --recursive
```

Build static site shell/output:

```bash
make clean static-site
```

Build search WASM module (requires Emscripten):

```bash
make wasm-search
```

Typical local flow:

1. Edit posts / styles / generator / renderer
2. Rebuild search WASM module when search logic changes
3. Run `make clean static-site`
4. Validate generated `docs/post/**/index.html` includes:
   - empty `#article-body`
   - `#post-markdown` JSON payload
   - post renderer script

---

## Current status and known limitations

- Canonical post rendering is client-side JavaScript for post pages.
- Search remains a separate WASM path and index pipeline.
- Category listing pages still use pre-rendered HTML snippets for list content.
- Building WASM modules requires a local Emscripten toolchain.
- The static generator currently stores full post source in memory while building.

If you are looking for the Korean documentation, see `README.ko.md`.
