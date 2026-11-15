# EasyDownload v1.5.6 릴리스 노트

EasyDownload v1.5.6 업데이트에서는 1회성 토큰 CDN(MegaUp, 1Fichier 등) 및 단일 연결 호스트 다운로드 시 토큰 소진이나 Cloudflare 403 차단 없이 크롬 브라우저에서 인계된 직링크를 100% 안전하게 고속 다운로드할 수 있도록 직결 스트리밍 파이프라인을 전면 혁신하였습니다.

### 주요 변경 사항

1. **단일 직결 스트리밍(Zero-Probe Direct Streaming) 도입**:
   - MegaUp, 1Fichier(무료 티어) 등 1회용 다운로드 티켓 호스트에서 사전 `Range: bytes=0-0` 탐색 요청을 전면 배제.
   - 단 1번의 직결 HTTP GET 연결로 응답 헤더 수신 즉시 인라인으로 파일 크기와 파일명을 검증하고, 동일 소켓을 유지하여 끊김 없이 본체 파일 데이터를 스트리밍 수신.
   - 1회성 토큰 사전 소각(Burnout), 416 Range Not Satisfiable, 15분 쿨다운 잠금 현상 원천 차단.

2. **브라우저 User-Agent 및 세션 쿠키 100% 무결 상속**:
   - 크롬 확장 프로그램에서 다운로드 인계 시 `navigator.userAgent`를 데스크톱 코어로 필수 전달.
   - Cloudflare Turnstile 암호화 검증 토큰(`cf_clearance`)과 TLS 핑거프린트가 완벽히 일치하도록 WinHTTP 세션에 적용하여 접근 거부(403 Forbidden) 원천 차단.

3. **직링크 보호 가드 (Already-Direct Guard)**:
   - 크롬 확장에서 이미 성공적으로 인계된 CDN 직링크(`download.megaup.net`, `a-XX.1fichier.com` 등)를 작업 루프에서 오인하여 스크래핑 엔진(`LinkExtractor`)으로 재추출하려다 링크를 훼손하는 현상 방지.

4. **배포 산출물**:
   - `EasyDownload_Setup.exe` (v1.5.6 단일 설치 파일, EasyInstall 패키징)
   - `EasyDownload_v1.5.6_Portable.zip` (v1.5.6 무설치 포터블 압축본)
