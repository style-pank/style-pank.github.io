# Style and Grace

Style and Grace는 GitHub Pages 기반 블로그/사이트이며, 포스트 본문 렌더링의 기준 경로를 **클라이언트 사이드 JavaScript**로 두는 구조입니다.

포스트 Markdown 원문은 브라우저로 전달되고, 런타임에서 `assets/post-renderer.js`가 HTML로 렌더링합니다.

---

## 왜 클라이언트 사이드 렌더링인가

이 프로젝트는 Markdown 렌더링 규칙을 브라우저 렌더러 하나로 통일합니다.

- 동일한 front matter 분리 동작
- 섹션별 Markdown 렌더링 분기 없음
- article body에 raw markdown를 직접 주입하는 fallback 제거

결과적으로 카테고리별 동작 차이나 경로별 렌더링 불일치를 줄일 수 있습니다.

---

## 아키텍처 개요

### 기준 포스트 렌더링 경로

1. 정적 생성기가 페이지 셸과 메타데이터(제목/날짜/태그 등)를 생성합니다.
2. 포스트 원문 Markdown을 JSON 스크립트(`#post-markdown`)에 담아 페이지에 포함합니다.
3. `assets/post-renderer.js`가 Markdown payload를 파싱하고 렌더링합니다.
4. JS가 렌더링된 HTML을 `#article-body`에 삽입합니다.

### 보조 도구

정적 생성기(`tools/generate_static.c`, `bin/bloggen`)는 홈/카테고리/검색 인덱스/네비게이션/에셋 배치를 위해 사용됩니다.

---

## 렌더링 파이프라인 상세

- **입력:** front matter를 포함할 수 있는 전체 markdown 원문
- **front matter 분리:** `assets/post-renderer.js`
- **본문 렌더링:** 브라우저 JS 렌더러
- **출력:** article body에 HTML 문자열 반영

보장되는 동작:

- front matter가 본문 렌더 결과에 노출되지 않음
- article body는 raw markdown가 아니라 렌더링된 HTML만 표시

---

## 디렉터리 / 구성요소 안내

- `assets/post-renderer.js`  
  브라우저 렌더러(front matter 분리 및 markdown-to-HTML 변환)

- `tools/generate_static.c`  
  페이지 셸/인덱스/네비게이션 생성기

- `assets/search-ui.js`, `blog/wasm/search.c`  
  검색 UI 및 검색 WASM 점수 모듈

- `posts/<category>/*.md`  
  포스트 원본 markdown(front matter 포함 가능)

- `docs/`  
  GitHub Pages 배포용 생성 결과물

---

## 개발/빌드 메모

먼저 서브모듈을 초기화합니다.

```bash
git submodule update --init --recursive
```

정적 사이트 출력 생성:

```bash
make clean static-site
```

검색 WASM 모듈 빌드(Emscripten 필요):

```bash
make wasm-search
```

일반적인 로컬 작업 흐름:

1. posts / styles / generator / renderer 수정
2. 검색 로직 변경 시 wasm 재빌드
3. `make clean static-site` 실행
4. 생성된 `docs/post/**/index.html`에서 다음 확인
   - `#article-body`가 초기 비어 있음
   - `#post-markdown` JSON payload 존재
   - post renderer 스크립트 로드

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

- 포스트 페이지 본문은 클라이언트 JavaScript 경로가 기준입니다.
- 검색은 별도의 WASM + 인덱스 경로를 사용합니다.
- 카테고리 목록 페이지는 현재 사전 렌더링된 HTML 스니펫을 사용합니다.
- WASM 모듈 빌드에는 Emscripten 환경이 필요합니다.
- 정적 생성기는 빌드 시 포스트 원문을 메모리에 보관합니다.
