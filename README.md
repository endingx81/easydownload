# ⚡ EasyDownload (고속 분할 다운로드 가속기 & 크롬 확장 프로그램)

> **C++ Win32 네이티브 고속 다운로드 가속기 + Chrome 확장 프로그램(Manifest V3) + 실시간 비디오/HLS 스트림 다운로더**

---

## 🌟 주요 기능

1. **자동 분할 고속 다운로드 (Segmented Chunk Download)**
   - 대용량 파일을 8~32개 청크로 동시 분할하여 대역폭 한계치까지 가속 다운로드.
   - 단일 파일 직접 오프셋 쓰기(`Direct Offset Write`) 및 사전 디스크 공간 할당으로 임시 파일 복사 낭비 제로.
   - 네트워크 단절 및 일시정지 시 언제든 이어받기(`Resume`) 지원 (`.egpart` 메타데이터).
   - 서버의 `Accept-Ranges` 지원 여부를 자동 판별하여 일반 단일 스트림으로 자동 폴백.

2. **크롬 브라우저 다운로드 100% 자동 가로채기 (Chrome Extension MV3)**
   - Chrome 내에서 파일 다운로드 클릭 시 브라우저 내장 다운로드를 자동 취소하고 데스크톱 가속기로 즉시 연결.
   - 세션 쿠키, Referer, 파일명, 인증 헤더를 보존하여 로그인 전용 파일도 완벽 다운로드.

3. **모든 비디오/오디오 스트림 다운로드 (Video Stream Sniffer)**
   - 웹서핑 및 동영상 재생 시 네트워크 트래픽에서 `.m3u8`(HLS), `.mpd`(DASH), `.mp4`, `.webm` 스트림을 실시간 자동 감지.
   - 확장 프로그램 아이콘에 감지된 비디오 개수 배지(Badge) 표시 및 원클릭 다운로드 팝업 제공.
   - HLS Master/Media Playlist를 파싱하여 수백 개의 `.ts` 세그먼트를 멀티스레드로 병렬 다운로드 후 단일 `.mp4`로 무손실 순차 자동 병합.
   - 복잡한 비디오 플랫폼 대응을 위한 `yt-dlp` 자동 연동 지원.

4. **DPI / SNI 차단 우회 및 통신사 DNS 오염 원천 방어 엔진 (GoodbyeDPI -5 규격 완전 호환)**
   - 별도 외부 프로세스(`goodbyedpi.exe`) 없이 EasyDownload 자체 내부 C++ 스레드로 직접 패킷 조작 및 SNI 2바이트 역순 분할(TLS Record Fragmentation) 수행.
   - 통신사(KT, SK, LG 등)의 UDP 53 DNS 오염 및 하이재킹을 WinDivert 레벨에서 가로채어 비표준 안전 DNS(`77.88.8.8:1253` 등)로 실시간 리다이렉션하여 첫 접속 시 0초 만에 100% 즉시 로딩 보장.
   - 우회 시작 즉시 Windows 로컬 DNS 캐시(`DnsFlushResolverCache`)를 자동 소거하여 이전 오염/실패 캐시로 인한 새로고침 지연 완전 해결.
   - 통신사 감시 장비의 위조 RST 패킷 자동 차단 및 QUIC(UDP 443) 즉시 차단으로 브라우저가 대기 없이 TCP HTTPS로 직행하도록 유도.
   - 프로그램 종료, 재설치, 업데이트 시 WinDivert 드라이버 핸들 및 서비스를 자동 해제하여 파일 잠금이나 충돌 없이 완벽한 재설치 지원.

5. **현대적인 Win32 네이티브 UI & 시각화**
   - **Per-Monitor V2 고DPI 지원**: 4K 및 다중 모니터 배율 변화 시에도 선명한 폰트와 레이아웃 유지.
   - **IDM 스타일 청크 진행률 바 (`SegmentBar`)**: 각 분할 청크의 다운로드 상태를 그래픽 바 형태로 실시간 시각화.
   - **깜빡임 없는 리스트뷰**: In-place 업데이트로 화면 깜빡임 및 스크롤 초기화 방지.
   - **시스템 트레이 상주**: 백그라운드 다운로드 및 완료 시 Windows 토스트/풍선 알림.

---

## 🚀 빠른 시작 가이드

### 1. 데스크톱 앱 실행 또는 설치
- **간편 설치**: `EasyDownload_Setup.exe` 실행 (바탕화면 및 시작 메뉴 바로가기 자동 등록)
- **무설치 포터블**: `EasyDownload_v1.4.3_Portable.zip` 압축 해제 후 `easydownload.exe` 실행

### 2. Chrome 확장 프로그램 등록 방법 (10초 소요)
1. Chrome 브라우저 주소창에 `chrome://extensions` 입력 후 이동.
2. 페이지 우측 상단의 **[개발자 모드]** 스위치 켜기.
3. 좌측 상단의 **[압축해제된 확장 프로그램을 로드합니다]** 클릭.
4. EasyDownload의 `extension\` 폴더를 선택하면 등록 완료!
   *(EasyDownload 프로그램 툴바의 **[🧩 크롬 확장 설치]** 버튼을 누르면 폴더와 브라우저 설정 창이 자동으로 열립니다.)*

---

## 🛠️ 개발 및 빌드 환경

- **언어/플랫폼**: C++17, Win32 API, WinHTTP, WinSock2
- **빌드 도구**: Visual Studio 2022 Community MSBuild (`Release|x64`)
  ```powershell
  & "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" easydownload.sln /p:Configuration=Release /p:Platform=x64
  ```
- **패키징 도구**: `easyinstall.exe` (자체 제작 인스톨러 빌더)

