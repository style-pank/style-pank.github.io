---
title: 이미지 첨부 테스트
date: 2026-05-01
excerpt: 마크다운에서 이미지를 첨부하는 방법을 테스트합니다.
tags: test, image
---

# 이미지 첨부 테스트

이 포스트는 마크다운에서 이미지를 첨부하는 기능을 테스트하기 위해 작성되었습니다.

## 방법 1: 절대 경로 사용 (권장)

GitHub Pages의 루트를 기준으로 한 경로를 사용합니다.

`![샘플 이미지](/assets/images/sample.jpg)`

![샘플 이미지](/assets/images/sample.jpg)

## 방법 2: 상대 경로 사용

현재 포스트 위치(`post/general/image-test/`)를 기준으로 한 상대 경로를 사용합니다.

`![샘플 이미지](../../../assets/images/sample.jpg)`

![샘플 이미지](../../../assets/images/sample.jpg)

---

위의 두 방법 모두 이미지가 정상적으로 출력되어야 합니다.
