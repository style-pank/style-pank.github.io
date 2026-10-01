CC  ?= gcc

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

.PHONY: site static-site wasm-blog cwist-wasip2 clean clean-wasm

# Full build: render component first, then the static shell that copies assets/.
site: wasm-blog static-site

# --- CWIST WASI 0.2 render component -----------------------------------------
# The blog's render kernel (blog/wasm) is a CWIST app built for wasm32-wasip2
# and exported through the cwist-guest world (lib/cwist/wit/cwist.wit):
#   wasi-sdk clang  -> core module linked against libcwist_wasip2
#   wasm-tools      -> component (cwist-guest world embedded)
#   jco transpile   -> ES module + core wasm shards (browser, no node compat)
#   esbuild         -> assets/cwist-blog.mjs (jco output + cwist-wasm adapter)
# Toolchain pins match lib/cwist CI: wasi-sdk 25, wasm-tools 1.259,
# wit-bindgen 0.62; jco/preview2-shim/esbuild are pinned in blog/package.json.
WASI_SDK    ?= $(HOME)/toolchains/wasi-sdk-25.0-x86_64-linux
WASM_TOOLS  ?= wasm-tools
WIT_BINDGEN ?= wit-bindgen
NPM         ?= npm

CWIST_DIR        := lib/cwist
CWIST_WIT        := $(CWIST_DIR)/wit
CWIST_WASIP2_AR  := $(CWIST_DIR)/libcwist_wasip2_wasm32-wasip2.a
BLOG_BUILD       := blog/.build
BLOG_BINDINGS    := $(BLOG_BUILD)/bindings
BLOG_JCO         := $(BLOG_BUILD)/jco
BLOG_CORE        := $(BLOG_BUILD)/blog.core.wasm
BLOG_COMPONENT   := $(BLOG_BUILD)/blog.component.wasm

BLOG_GUEST_SRCS := \
	blog/wasm/blog_guest.c \
	blog/wasm/post_renderer.c \
	blog/wasm/comments_ui.c \
	blog/wasm/search.c \
	blog/wasm/search_ui.c \
	blog/wasm/relations.c \
	blog/wasm/theme.c \
	lib/md4c/src/md4c.c \
	lib/md4c/src/md4c-html.c \
	lib/md4c/src/entity.c

BLOG_GUEST_CFLAGS := --target=wasm32-wasip2 -mexec-model=reactor \
	-std=c17 -O2 -Wall -fvisibility=hidden \
	-D_GNU_SOURCE -D_XOPEN_SOURCE=700 -D_WASI_EMULATED_GETPID \
	-I$(CWIST_DIR)/include -I$(CWIST_DIR)/lib -I$(CWIST_DIR)/lib/cjson \
	-I$(CWIST_DIR)/lib/boringssl/include -I$(CWIST_DIR)/lib/libttak/include \
	-I$(CWIST_DIR)/lib/sqlite3 \
	-Ilib/md4c/src -I$(BLOG_BINDINGS)

BLOG_GUEST_LDFLAGS := -lwasi-emulated-pthread -lwasi-emulated-getpid \
	-Wl,--gc-sections -Wl,-z,stack-size=1048576

cwist-wasip2: $(CWIST_WASIP2_AR)

$(CWIST_WASIP2_AR):
	$(MAKE) -C $(CWIST_DIR) libcwist_wasip2_wasm32-wasip2.a WASI_SDK=$(WASI_SDK)

$(BLOG_BINDINGS)/cwist_guest.c: $(CWIST_WIT)/cwist.wit
	rm -rf $(BLOG_BINDINGS) && mkdir -p $(BLOG_BINDINGS)
	$(WIT_BINDGEN) c $(CWIST_WIT) --world cwist-guest --out-dir $(BLOG_BINDINGS)

$(BLOG_CORE): $(BLOG_GUEST_SRCS) blog/wasm/blog.h $(BLOG_BINDINGS)/cwist_guest.c $(CWIST_WASIP2_AR)
	$(WASI_SDK)/bin/clang $(BLOG_GUEST_CFLAGS) -o $@ \
	    $(BLOG_GUEST_SRCS) $(BLOG_BINDINGS)/cwist_guest.c \
	    $(BLOG_BINDINGS)/cwist_guest_component_type.o \
	    $(CWIST_WASIP2_AR) $(BLOG_GUEST_LDFLAGS)

$(BLOG_COMPONENT): $(BLOG_CORE)
	$(WASM_TOOLS) component embed $(CWIST_WIT) --world cwist-guest $< -o $@
	$(WASM_TOOLS) validate $@

blog/node_modules/.package-lock.json: blog/package.json
	$(NPM) --prefix blog ci --no-fund --no-audit

wasm-blog: $(BLOG_COMPONENT) blog/node_modules/.package-lock.json blog/js/cwist-blog-entry.js
	rm -rf $(BLOG_JCO) assets/cwist-blog.mjs assets/blog.component.core*.wasm
	blog/node_modules/.bin/jco transpile $(BLOG_COMPONENT) --name blog.component \
	    --no-nodejs-compat --tla-compat --out-dir $(BLOG_JCO)
	blog/node_modules/.bin/esbuild blog/js/cwist-blog-entry.js \
	    --bundle --platform=browser --format=esm --conditions=browser \
	    --target=es2022 --minify \
	    --alias:cwist-blog-component=./$(BLOG_JCO)/blog.component.js \
	    --outfile=assets/cwist-blog.mjs
	cp $(BLOG_JCO)/*.wasm assets/

clean-wasm:
	rm -rf $(BLOG_BUILD)
	$(MAKE) -C $(CWIST_DIR) clean-wasip2

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

$(STATIC_GENERATOR): $(STATIC_SRCS)
	@mkdir -p $(@D)
	$(CC) -std=c17 -O2 $(STATIC_SRCS) $(STATIC_INCLUDES) -o $@

clean:
	rm -rf bin docs
