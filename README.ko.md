# Style and Grace

Style and Grace는 GitHub Pages 기반 블로그이며, 포스트 본문·검색·댓글·관계도 렌더링을
**CWIST WASI 0.2 컴포넌트**가 브라우저 안에서 수행합니다.

렌더링 커널(`blog/wasm`)은 `wasm32-wasip2`로 빌드한 CWIST 앱이고, CWIST의
`cwist-guest` WIT 월드(`lib/cwist/wit/cwist.wit`)를 export합니다. 페이지 스크립트는
이 컴포넌트에 HTTP/1.1 요청을 보내듯이 렌더링을 요청합니다.

---

## 아키텍처 개요

### 빌드 파이프라인 (`make wasm-blog`)

1. **wasi-sdk clang** — `blog/wasm/*.c` + md4c를 `libcwist_wasip2`와 함께 reactor 코어 모듈로 링크
2. **wit-bindgen** — `cwist-guest` 월드의 canonical ABI 바인딩 생성
3. **wasm-tools component embed** — WASI 0.2 컴포넌트(`blog.component.wasm`) 생성
4. **jco transpile** (`--no-nodejs-compat --tla-compat`) — 브라우저용 ES 모듈 + 코어 wasm 샤드
5. **esbuild** — jco 출력 + cwist-wasm 컴포넌트 어댑터 + preview2-shim 브라우저 빌드를
   `assets/cwist-blog.mjs`로 번들

### 런타임 경로

1. `assets/cwist-runtime.js`가 모든 페이지 `<head>`에서 `window.CwistBlog.ready`를 공개하고
   `cwist-blog.mjs`를 모듈로 로드합니다.
2. 페이지 스크립트는 `CwistBlog.ready`가 넘겨주는 클라이언트로 컴포넌트 안의 CWIST 라우트를 호출합니다.

| 라우트 | 요청 본문 | 응답 | 사용처 |
|---|---|---|---|
| `POST /render/markdown` | markdown | HTML | `post-renderer.js` |
| `POST /render/comments` | 댓글 JSON 배열 | HTML (raw HTML 차단) | `comments.js` |
| `POST /render/home` | `{title,description,chips}` | HTML | `home-eye-candy.js` |
| `PUT /search/index` | `search-index.json` | 204 | `search-ui.js` |
| `POST /search` | 검색어 | 결과 카드 HTML | `search-ui.js` |
| `POST /relations` | `search-index.json` | `{edges:[{a,b,w}]}` | `relations-ui.js` |
| `POST /theme` | `location.pathname` | 섹션 accent JSON | `theme-toggle.js` |

컴포넌트를 불러오지 못하면(`ready` reject) 각 스크립트는 순수 JS fallback으로 렌더링합니다.

---

## 디렉터리 / 구성요소 안내

- `blog/wasm/blog_guest.c` — CWIST 앱 라우트와 `cwist-guest` export 구현
- `blog/wasm/*.c` — markdown/댓글/검색/관계도/테마 렌더링 커널
- `blog/js/cwist-blog-entry.js` — 브라우저 번들 엔트리
- `blog/package.json` — jco / preview2-shim / esbuild 버전 고정
- `assets/cwist-runtime.js` — 컴포넌트 로더
- `tools/generate_static.c` — 페이지 셸/인덱스/네비게이션 생성기 (네이티브 C)
- `posts/<category>/*.md` — 포스트 원본 markdown
- `docs/` — GitHub Pages 배포용 생성 결과물

---

## 개발/빌드 메모

필요한 서브모듈만 초기화합니다.

```bash
git submodule update --init lib/md4c lib/cwist
git -C lib/cwist submodule update --init --depth 1 lib/cjson lib/libttak lib/boringssl
```

툴체인(버전은 `lib/cwist` CI와 동일): wasi-sdk 25, wasm-tools 1.259, wit-bindgen 0.62, Node 22.

```bash
make wasm-blog WASI_SDK=$HOME/toolchains/wasi-sdk-25.0-x86_64-linux   # 컴포넌트 + 번들
make clean static-site                                                # docs/ 생성
# 또는 한 번에: make site WASI_SDK=...
```

`wasm-tools`/`wit-bindgen`이 PATH에 없다면 `WASM_TOOLS=`/`WIT_BINDGEN=`으로 경로를 지정합니다.

---

## 이미지 첨부 방법

GitHub Pages 스타일로 이미지를 첨부하려면 다음 절차를 따르세요:

1. `assets/images/` 디렉터리에 이미지 파일을 추가합니다.
2. 포스트 Markdown 파일에서 다음과 같은 형식으로 이미지를 참조합니다.

- **절대 경로 방식 (권장):** `/assets/images/파일명.확장자`
  ```markdown
  ![설명](/assets/images/photo.jpg)
  ```
- **상대 경로 방식:** `../../../assets/images/파일명.확장자`
  ```markdown
  ![설명](../../../assets/images/photo.jpg)
  ```

빌드 시 `assets/` 디렉터리의 모든 내용이 `docs/assets/`로 복사되어 실제 사이트에서 접근 가능해집니다.

---

## 현재 상태와 한계

- `cwist-guest` 월드는 동기 `dispatch` 하나뿐이라, jco가 만든 내보내기 호출은 메인 스레드에서 실행됩니다.
- 카테고리 목록 페이지는 사전 렌더링된 HTML 스니펫을 사용합니다.
- 정적 생성기는 빌드 시 포스트 원문을 메모리에 보관합니다.
