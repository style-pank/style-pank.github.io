CC  ?= gcc
EMCC ?= emcc

STATIC_GENERATOR := bin/bloggen
STATIC_SRCS := \
	tools/generate_static.c \
	tools/scheduler.c \
	lib/cwist/src/sys/err/error.c \
	lib/cwist/src/core/sstring/sstring.c \
	blog/wasm/cwist_alloc_stub.c \
	lib/cwist/lib/cjson/cJSON.c \
	lib/md4c/src/md4c.c \
	lib/md4c/src/md4c-html.c \
	lib/md4c/src/entity.c

# cwist include paths: real cjson must be found before the wasm stub dir
STATIC_INCLUDES := \
	-Ilib/cwist/include \
	-Ilib/cwist/lib \
	-Ilib/cwist/lib/cjson \
	-Iblog/wasm/stubs \
	-Ilib/md4c/src

.PHONY: static-site wasm-search wasm-search-fast clean

WASM_SEARCH_SRCS := \
	blog/wasm/search.c \
	blog/wasm/search_ui.c \
	blog/wasm/post_renderer.c \
	blog/wasm/relations.c \
	blog/wasm/comments_ui.c \
	blog/wasm/theme.c \
	blog/wasm/cwist_alloc_stub.c \
	blog/wasm/cwist_gc_stub.c \
	lib/cwist/src/sys/err/error.c \
	lib/cwist/src/core/sstring/sstring.c \
	lib/cwist/src/core/html/builder.c \
	lib/cwist/lib/cjson/cJSON.c \
	lib/md4c/src/md4c.c \
	lib/md4c/src/md4c-html.c \
	lib/md4c/src/entity.c

WASM_SEARCH_INCLUDES := \
	-Ilib/cwist/include \
	-Ilib/cwist/lib \
	-Ilib/cwist/lib/cjson \
	-Iblog/wasm/stubs \
	-Ilib/md4c/src

static-site: $(STATIC_GENERATOR)
	rm -rf docs
	mkdir -p docs
	cp -r assets docs/assets
	./$(STATIC_GENERATOR) categories.cfg posts assets/styles.css docs
	mkdir -p docs/data
	if [ -f data/comments.json ]; then cp data/comments.json docs/data/comments.json; else echo '{}' > docs/data/comments.json; fi
	touch docs/.nojekyll

static-site-fallback:
	rm -rf docs
	mkdir -p docs
	cp -r 9530abc-testing-build/. docs/
	cp -r assets docs/assets
	mkdir -p docs/data
	if [ -f data/comments.json ]; then cp data/comments.json docs/data/comments.json; else echo '{}' > docs/data/comments.json; fi
	touch docs/.nojekyll

# Build the CWIST search WASM module (requires Emscripten).
# Outputs: assets/search-module.js  assets/search-module.wasm
WASM_OPT_LEVEL ?= -O2

wasm-search: $(WASM_SEARCH_SRCS)
	@mkdir -p assets
	$(EMCC) $(WASM_OPT_LEVEL) --no-entry \
	    -s WASM=1 \
	    -s MODULARIZE=1 \
	    -s EXPORT_NAME=CwistSearchModule \
	    -s "EXPORTED_FUNCTIONS=[\"_cwist_score\",\"_cwist_pair_score\",\"_cwist_render_search_result\",\"_cwist_render_home_eye_candy\",\"_cwist_render_markdown\",\"_cwist_render_comment\",\"_cwist_search_and_render\",\"_cwist_get_section_from_path\",\"_cwist_get_accent_for_section\",\"_cwist_get_hover_for_section\",\"_cwist_hex_to_rgb\",\"_cwist_free_html\"]" \
	    -s "EXPORTED_RUNTIME_METHODS=[\"ccall\",\"cwrap\",\"UTF8ToString\"]" \
	    -s FILESYSTEM=0 \
	    -s ENVIRONMENT=web \
	    -s ALLOW_MEMORY_GROWTH=1 \
	    -o assets/search-module.js $(WASM_SEARCH_SRCS) $(WASM_SEARCH_INCLUDES)

wasm-search-fast: WASM_OPT_LEVEL=-O3
wasm-search-fast: wasm-search

$(STATIC_GENERATOR): $(STATIC_SRCS)
	@mkdir -p $(@D)
	$(CC) -std=c17 -O2 $(STATIC_SRCS) $(STATIC_INCLUDES) -o $@

clean:
	rm -rf bin docs
