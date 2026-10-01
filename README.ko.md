# Style and Grace

Style and Grace는 GitHub Pages 기반 블로그입니다. 손으로 쓴 JavaScript 없이 C만으로
동작하며, 역할은 세 갈래로 나뉩니다.

- **빌드 시점 (네이티브 C)** — `tools/generate_static.c`가 `blog/wasm`의 렌더링 커널을
  네이티브로 링크해 포스트 본문, 댓글, 홈 카드, 섹션 accent, 관계도(SVG)를 정적 HTML로 굽습니다.
- **CWIST WASI 0.2 컴포넌트** — 검색 커널(`blog/wasm`)은 `wasm32-wasip2`로 빌드한 CWIST 앱이고,
  CWIST의 `cwist-guest` WIT 월드(`lib/cwist/wit/cwist.wit`)를 export합니다.
- **Emscripten 페이지 호스트 (C)** — WASI 0.2에는 DOM·이벤트·스토리지가 없으므로, 그 부분은
  `blog/ui/blog_ui.c`를 Emscripten으로 빌드한 `assets/blog-ui.{js,wasm}`이 맡습니다.
  테마 토글, 검색 UI, 컴포넌트 로드, 컴포넌트에 보낼 HTTP/1.1 요청의 직렬화/파싱이 모두 C입니다.

---

## 아키텍처 개요

### 빌드 파이프라인

`make wasm-blog` (WASI 0.2 컴포넌트):

1. **wasi-sdk clang** — `blog/wasm/{blog_guest,search,search_ui}.c`를 `libcwist_wasip2`와 함께 reactor 코어 모듈로 링크
2. **wit-bindgen** — `cwist-guest` 월드의 canonical ABI 바인딩 생성
3. **wasm-tools component embed** — WASI 0.2 컴포넌트(`blog.component.wasm`) 생성
4. **jco transpile** (`--no-nodejs-compat --tla-compat`) — 브라우저용 ES 모듈 + 코어 wasm 샤드
5. **esbuild** — jco 출력 + preview2-shim 브라우저 빌드를 `assets/cwist-blog.mjs`로 번들

`make ui-blog` (Emscripten): `blog/ui/blog_ui.c` → `assets/blog-ui.js` + `assets/blog-ui.wasm`

`make static-site` (네이티브): `bin/bloggen`이 `docs/`를 생성 (`data/comments.json`이 있으면 댓글 포함)

### 런타임 경로

1. 모든 페이지의 `<head>`가 `assets/blog-ui.js`를 로드합니다. C `main()`이 저장된 테마
   (`localStorage`의 `cwist-theme`) 또는 `prefers-color-scheme`을 적용합니다.
2. 검색 페이지에서는 같은 모듈이 `cwist-blog.mjs`를 import하고 `search-index.json`을 받아
   컴포넌트에 넣은 뒤, 입력 디바운스와 방향키 이동을 처리합니다.
3. 포스트 페이지에서는 코드 블록이 있으면 highlight.js를, 본문에 `data-math`가 있으면 MathJax를 붙입니다.

| 라우트 | 요청 본문 | 응답 | 호출하는 쪽 |
|---|---|---|---|
| `PUT /search/index` | `search-index.json` | 204 | `blog_ui.c` |
| `POST /search` | 검색어 | 결과 카드 HTML | `blog_ui.c` |

DOM에 직접 닿는 브라우저 API 호출은 `blog_ui.c` 안의 `EM_JS` 바인딩(한 함수당 API 호출 하나)으로만 존재합니다.
배포 산출물의 `.js`/`.mjs`는 모두 Emscripten·jco·esbuild가 생성한 파일입니다.

댓글 작성은 스크립트 없이 GitHub 이슈 작성 화면을 여는 GET 폼입니다. 등록된 댓글은
`data/comments.json`을 바꾸면 다음 배포 때 페이지에 반영됩니다.

---

## 디렉터리 / 구성요소 안내

- `tools/generate_static.c` — 페이지 셸/본문/댓글/관계도/네비게이션 생성기 (네이티브 C)
- `blog/wasm/blog_guest.c` — CWIST 앱 라우트와 `cwist-guest` export 구현
- `blog/wasm/*.c` — markdown/댓글/검색/관계도 렌더링 커널 (생성기와 컴포넌트가 공유)
- `blog/ui/blog_ui.c` — Emscripten 페이지 호스트
- `blog/package.json` — jco / preview2-shim / esbuild 버전 고정
- `posts/<category>/*.md` — 포스트 원본 markdown
- `data/comments.json` — 댓글 데이터 (`<category>/<slug>` 키)
- `docs/` — GitHub Pages 배포용 생성 결과물

---

## 개발/빌드 메모

필요한 서브모듈만 초기화합니다.

```bash
git submodule update --init lib/md4c lib/cwist
git -C lib/cwist submodule update --init --filter=blob:none lib/cjson lib/libttak lib/boringssl
```

툴체인: wasi-sdk 25, wasm-tools 1.259, wit-bindgen 0.62 (`lib/cwist` CI와 동일), Emscripten 5.0.0, Node 22.

```bash
make wasm-blog WASI_SDK=$HOME/toolchains/wasi-sdk-25.0-x86_64-linux   # 컴포넌트 + 번들
make ui-blog                                                          # 페이지 호스트
make clean static-site                                                # docs/ 생성
# 또는 한 번에: make site WASI_SDK=...
```

`wasm-tools`/`wit-bindgen`/`emcc`가 PATH에 없다면 `WASM_TOOLS=`/`WIT_BINDGEN=`/`EMCC=`로 경로를 지정합니다.

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
- 테마는 페이지 호스트 wasm이 준비된 뒤에 적용되므로, 저장한 테마가 OS 설정과 다르면
  첫 화면에서 잠깐 반대 테마가 보일 수 있습니다.
- 정적 생성기는 빌드 시 포스트 원문을 메모리에 보관합니다.
