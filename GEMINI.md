# AI 에이전트 개발 행동 강령 및 실수 방지 지침 (Core Rules & Anti-Patterns)

**[경고] 본 문서는 단순 기능 설명서나 커밋 로그가 아닙니다.**  
AI 에이전트(당신)가 개발 중 **실제로 저질렀던 치명적인 실수들을 박제하고 영구히 재발을 방지**하기 위한 강령입니다.  
다른 프로젝트에서도 공통으로 적용되는 **공통 규칙 및 빌드 환경**이 최상단에 배치되어 있으므로, 새로운 작업을 진행하기 전 반드시 숙지하세요.

### 🚨 최우선 원칙 1: 코딩/개발 지시 수신 시 구현 전 계획(Plan) 사전 공유 필수 (선 계획, 후 구현)
- **철칙**: 사용자가 기능 추가, 버그 수정, 리팩토링 등 **코드베이스를 수정하는 코딩 작업을 지시할 때**, 임의로 코드를 곧바로 작성하거나 수정하는 구현 작업을 시작하기 전에 반드시 무엇을 어떻게 진행할 것인지 명확한 실행 계획(Plan)을 먼저 상세히 설명하고 진행할 것.
- **예외(실행/운영 명령 즉시 수행)**: 빌드, 테스트, 패키징, 릴리스 등록 등 **이미 동작과 목표가 명확한 실행/운영 명령(Operational Execution)**은 계획 확인 질문으로 사용자를 번거롭게 되묻지 말고 즉시 원스톱으로 실행할 것.
- **방지 목적**: 사전 계획 공유 없이 성급하게 코드를 수정하여 엉뚱한 방향으로 구현되거나 불필요한 시행착오와 번복이 발생하는 것을 원천 차단함.

### 🚨 최우선 원칙 2: 실수 발생 즉시 자발적 문서 박제 (사용자 요청 대기 절대 금지)
- **실제 안일한 태도**: 빌드 실패, UI 깨짐, 텍스트 짤림 등의 치명적인 실수를 저지르고 수정 코드를 반영했음에도, "수정 완료되었습니다"라고만 보고하고 사용자가 "GEMINI.md에 추가할 건 없냐?"고 직접 지적할 때까지 가만히 있는 수동적인 태도를 보임.
- **철칙**: 버그 수정이나 실패 수습이 완료되면, **사용자가 묻기 전에 무조건 작업 완료 단계에서 본 문서(GEMINI.md)에 '실제 실수'와 '방지 원칙'을 자동으로 기록**하고 그 사실을 함께 보고할 것. 사용자가 먼저 문서를 챙기게 만드는 것은 에이전트의 심각한 직무유기임.
- **지식 자동 업데이트**: 향후 개발을 진행하면서 특정 프로젝트에 국한되지 않는 **일반적인 Win32 C++ 개발, 성능 최적화, 혹은 UI/UX 개선 패턴**을 새롭게 발견하거나 구현하게 되면, 스스로 판단하여 '개발 규칙' 항목에 해당 내용을 자동으로 요약 및 추가할 것 (단, 너무 지엽적인 예외 사항은 억지로 추가하지 않음).

---

# 1. 모든 C++/Win32 프로젝트 공통 필수 규칙 (Global & Common Rules)

다른 프로젝트에서도 동일하게 준수해야 하는 최우선 공통 규칙들입니다.

### ⚙️ 빌드 및 CLI 명령어 규칙 (Build & Environment)
- **절대 경로 MSBuild 사용**: Windows 환경 변수(PATH)에 `msbuild`나 `cmake`가 등록되어 있지 않은 경우가 많으므로 터미널에서 직접 실행하지 말 것.  
  반드시 Visual Studio 2022 Community MSBuild 절대 경로를 사용:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" easydownload.sln /p:Configuration=Release /p:Platform=x64`
- **인클루드 디렉터리 표준화**: 하위 폴더 소스들이 루트 헤더를 참조할 때 상대 경로 지옥(`../../`)이나 `C1083` 오류가 발생하지 않도록, `.vcxproj`의 `AdditionalIncludeDirectories`에 `$(ProjectDir);%(AdditionalIncludeDirectories)`를 필수로 등록할 것.
- **Git 명령어 임의 실행 절대 금지**: 사용자의 명시적인 지시가 없는 한 `git diff`, `git log`, `git checkout`, `git reset` 등 Git 명령어를 절대 임의로 실행하지 말 것.
- **바이너리 파일 잠금(`LNK1104`) 간과 및 미검증 완료 보고 절대 금지**:
  - **실제 실수**: 사용자가 테스트 중 프로그램을 켜둔 상태에서 빌드를 돌리면 `LINK : fatal error LNK1104: '...exe' 파일을 열 수 없습니다`로 링크가 실패함. 이 에러를 대충 넘기고 "수정 완료"라고 보고하여, 사용자가 버그가 전혀 고쳐지지 않은 구버전 바이너리를 보고 극도로 분노하게 만듦.
  - **방지 원칙**: 코드를 수정한 뒤에는 반드시 MSBuild 출력에서 **`빌드했습니다. 오류 0개, 경고 0개`와 `.exe` 생성 성공**을 100% 확인한 후에만 보고할 것. 실행 중인 프로세스로 인한 `LNK1104` 발생 시 프로세스 종료 후 재빌드를 완결할 것.
- **신규 설치 CLI 도구 환경 설정 사용자 떠넘기기 및 세션 PATH 미반영 금지**:
  - **실제 실수**: 도구 설치 후 에이전트가 직접 터미널 환경 변수를 갱신하거나 절대 경로로 후속 작업을 자동화할 수 있는 권한(`run_command`)이 있음에도 불구하고, 사용자에게 수동 작업을 떠넘기거나 부모 IDE 환경 변수 미반영으로 인한 오류를 유발함.
  - **방지 원칙**:
    1. 도구 설치 후 필요한 환경 변수 갱신, 바이너리 검증, 후속 작업은 **에이전트가 직접 실행(`run_command`)하여 사용자가 곧바로 쓸 수 있는 상태로 완결**해 둘 것.
    2. 사용자에게 터미널 명령어를 안내할 때는 IDE 재시작 전까지 환경 변수가 상속되지 않는 문제를 감안하여 **무조건 검증된 절대 경로**를 기본으로 제공할 것.
- **배포 산출물 중복 생성 및 패치 수정 시 버전(Version) 미갱신 금지**:
  - 배포 아카이브는 중복 파일 없이 **단일 표준 버전 명칭(예: `easydownload_v1.0.0_Portable.zip`) 딱 1개만** 정갈하게 생성할 것.
  - 코드/리소스 수정 및 배포가 발생하면 반드시 **시맨틱 버저닝(SemVer) 규칙에 따라 버전 번호를 명확히 상향(`v1.0.0` ➡️ `v1.0.1`)**하여 릴리스할 것.
- **설치 프로그램(Setup.exe) 생성 시 외부 도구 탐색 낭비 금지 및 EasyInstall 표준 파이프라인 준수**:
  - **실제 실수**: 설치 프로그램(Setup.exe) 생성 지시를 받았을 때, 프로젝트 환경에 구축된 자체 빌더를 확인하지 않고 Inno Setup(`iscc`), NSIS(`makensis`) 등 엉뚱한 외부 도구를 찾으며 헤매고 시간을 지체함. 동일 워크스페이스 그룹(`D:\coding\visualstudio\easyinstall`)에 전용 인스톨러 빌더가 이미 완성되어 있음에도 이를 즉시 활용하지 못함.
  - **방지 원칙 (표준 배포 패키징 파이프라인)**:
    1. **단일 설치 파일 빌더 절대 경로**:  
       `& "D:\coding\visualstudio\easyinstall\bin\Release\easyinstall.exe" -i "<입력_바이너리_또는_폴더>" -o "<출력경로>\Setup.exe" -n "<프로그램명>" -v "<버전>" -p "purestory" -m "<메인_exe명>"`  
       *(예: `& "D:\coding\visualstudio\easyinstall\bin\Release\easyinstall.exe" -i "D:\coding\visualstudio\easydownload\bin\Release\easydownload.exe" -o "D:\coding\visualstudio\easydownload\EasyDownload_Setup.exe" -n "easydownload" -v "1.00" -p "purestory" -m "easydownload.exe"`)*
    2. **무설치 포터블 압축 아카이브 생성**:  
       `powershell -Command "Compress-Archive -Path '<바이너리_경로>' -DestinationPath '<출력경로>\<프로그램명>_portable.zip' -Force"`
    3. 설치 파일 생성 요구 시 타사 도구(`iscc`, `makensis` 등)를 일절 찾지 말고, 위 `easyinstall.exe` 절대 경로를 이용하여 즉시 1초 만에 원스톱으로 빌드할 것.

### 🛠️ 도구 사용 실수 방지: `replace_file_content` 줄 번호 포함 금지
- **실제 실수**: `view_file`로 확인한 내용 앞의 줄 번호(`136: `, `150: ` 등)를 복사해 `TargetContent`나 `ReplacementContent`에 전달했다가 `chunk 0: target content not found` 에러를 반복 유발함.
- **방지 원칙**: 파일 수정 도구를 호출할 때는 파일의 **순수한 실제 코드(공백, 들여쓰기 포함, 줄 번호 절대 제외)**만을 정확하게 지정할 것.

### 🖥️ UI 컨트롤 라이프사이클 4단계 및 너비·줄바꿈·동적 리사이징 원칙 ('반쪽짜리 구현' 금지)
- **동적 레이아웃 및 리사이징 (WM_SIZE 처리)**:
  - `.rc` 파일에 정의된 컨트롤 크기 및 위치는 픽셀이 아닌 **DLU(Dialog Units)** 기반이므로, 시스템 DPI나 시스템 폰트에 따라 렌더링되는 크기가 달라집니다.
  - 따라서 `WM_SIZE` 핸들러에서 하드코딩된 픽셀 값(예: `width - 90`)을 사용한 위치/크기 계산을 피해야 합니다.
  - 항상 `GetWindowRect`로 컨트롤의 실제 렌더링 크기(width/height)를 구하거나, `MapDialogRect`를 통해 DLU를 픽셀로 변환한 후, 부모 클라이언트 영역(`GetClientRect`)에 비례하여 동적으로 배치(우측/하단 정렬 등)하세요.
- **UI 구현 4단계 완결**: 리소스 선언(`.rc`) ➡️ 컨트롤 초기화(`WM_INITDIALOG`) ➡️ 조작/렌더링(`WM_COMMAND`, `WM_DRAWITEM` 등) ➡️ 반응형 재배치(`WM_SIZE`).
- **1줄 라벨 줄바꿈 방지**: 1줄 라벨에는 반드시 `SS_LEFTNOWORDWRAP`을 사용하고, 한글 기준 글자당 최소 14~15px 이상의 넉넉한 너비를 할당할 것.
- **체크박스/라디오 너비 계산**: `텍스트 글자 폭 + 체크마크(24px) + 안전 마진(20~30%)`을 무조건 합산하여 할당할 것.
- **최소 창 크기 방어**: 반응형 대화상자는 `WM_GETMINMAXINFO`를 필수로 구현하여 사용자가 창을 줄이더라도 컨트롤이 겹치거나 깨지지 않도록 최소 너비/높이를 제한할 것.

### 🎨 깜빡임 없는 UI 업데이트 (In-place Update & Notification Suppression)
- 리스트뷰(`ListView`) 등의 아이템 상태가 일부 변경되었을 때, 전체 리스트를 지우고 새로고침(`DeleteAllItems` 후 재로드)하지 말 것.
- 화면 깜빡임과 스크롤 초기화를 방지하기 위해 `ListView_SetItemText` 등을 사용하여 **변경된 특정 행(Row)만 부분 업데이트(In-place Update)** 해야 함.
- 대량의 아이템 갱신 시 불리언 가드(`g_bUpdating = true`)와 `WM_SETREDRAW`를 걸어 `LVN_ITEMCHANGED` 연쇄 이벤트 폭주 및 화면 깜빡임을 원천 차단할 것.

### 🖌️ Win32 Dialog 내 Custom Draw 처리 (`DWLP_MSGRESULT`)
- DialogBox 기반의 윈도우(DialogProc)에서 `WM_NOTIFY` 메시지의 `NM_CUSTOMDRAW`를 처리할 때, 직접 return 값을 반환하면 시스템이 인식하지 못하고 무시하는 경우가 발생함.
- 반드시 `SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 반환값)`을 호출하여 결과를 명시한 후, `return (INT_PTR)TRUE;`를 반환해야 정상적으로 렌더링을 가로채거나(`CDRF_NOTIFYPOSTPAINT`) 넘길 수 있음.

### 🌲 TreeView 폴더 확장 시 Auto-Scroll(화면 튐) 방지
- 노드 삽입 직전 `GetScrollPos(hTree, SB_VERT)`로 스크롤 위치를 기억하고, `PostMessage` 등을 이용해 비동기로(즉시) 해당 스크롤 위치를 복구(`WM_VSCROLL`)하여 화면이 그 상태 그대로 유지되도록 처리해야 함.

### 🖥️ 고DPI 및 다중 모니터 환경에서의 폰트/창 비트맵 스트레칭 방지 (Per-Monitor V2 DPI & MulDiv 스케일링)
- 구형 API인 `SetProcessDPIAware()`는 프로세스를 단일 시스템 DPI에 고정시킴. 이로 인해 다중 모니터(배율 불일치), 절전 모드(Sleep) 복귀, 화면 해상도/배율 변경 시 Windows DWM이 창을 강제로 비트맵 확대/축소(Bitmap Stretching)하여 폰트가 저해상도 이미지처럼 뭉개지고 블러(Blur) 처리되는 현상이 간헐적으로 발생함.
- 반드시 런타임에 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`를 최우선 적용(실패 시 `SetProcessDpiAwareness` -> `SetProcessDPIAware` Fallback)하여 DWM 가상화 확대를 원천 차단해야 함.
- 또한 모든 폰트 높이(`CreateFontW`) 및 윈도우 배치 좌표/패딩은 `-MulDiv(기본픽셀, dpi, 96)`로 현재 모니터의 실제 DPI(`GetDpiForWindow`)에 비례하여 동적으로 계산하고, `WM_DPICHANGED`와 `WM_SETTINGCHANGE` / `WM_THEMECHANGED` 메시지를 핸들링하여 배율 및 테마 변화 시 레이아웃과 폰트를 즉시 갱신해야 함.

### 🖼️ 고DPI 및 다중 환경 대응을 위한 아이콘(.ico) 멀티 해상도 패키징 원칙
- 반드시 256×256 (PNG 압축), 128×128, 64×64, 48×48, 32×32, 24×24, 16×16 (32-bit RGBA) 전체 표준 규격 프레임을 단일 `.ico` 파일에 통합 패키징하여 배포할 것.

### 🔤 C++ 트레일링 역슬래시(`\`) 줄바꿈(Line-Splicing) 버그 방지
- C/C++ 소스 및 헤더의 한 줄 주석(`//`) 끝에는 절대로 단독 역슬래시(`\`)를 두지 말고, `\\` 또는 `(예: C:\\)` 등으로 처리할 것.

### 🔒 멀티스레드 동기화 뮤텍스(Lock) 내부 동기식 디스크/네트워크 I/O 배제
- 워커 스레드 동기화 임계 영역(Mutex Lock) 내부에서는 디스크/네트워크 시스템 콜을 일절 배제하고 **순수 RAM 메모리 해시셋/큐 조회 및 삽입**만 수행할 것.

### 📜 PowerShell 텍스트 파일 조작 시 UTF-8 인코딩 강제
- 파일 I/O를 수행하는 셸 스크립트를 작성할 때는 반드시 `-Encoding UTF8` 파라미터를 명시하여 인코딩 깨짐을 원천 차단할 것.

### 🖥️ Windows 배치 스크립트(.bat) 생성/수정 시 CP949(EUC-KR) 인코딩 강제
- **배경**: 한국어 Windows 명령 프롬프트(`cmd.exe`)는 기본 콘솔 인코딩으로 CP949(코드페이지 949)를 사용함.
- **철칙**: 배치 파일(`.bat`)에 한글 안내 메시지(`echo ...`)나 주석이 포함되는 경우, 일반 UTF-8로 저장하면 콘솔 실행 시 심각한 한글 깨짐(모지바케)이 발생하므로 반드시 한국어 Windows ANSI 규격인 **CP949(EUC-KR)**로 명시적으로 인코딩하여 저장할 것. (PowerShell 조작 시 `[System.Text.Encoding]::GetEncoding(949)` 또는 Python `encoding='cp949'` 사용)

### 📝 C++ 파일/스크립트 I/O 시 std::wofstream 사용 절대 금지
- 스크립트/배치 파일이나 설정 파일 생성 시 `std::wofstream`을 절대 쓰지 말 것. 반드시 명시적인 UTF-8 변환(`WideCharToMultiByte(CP_UTF8, ...)` 또는 `toUtf8` 헬퍼)을 거쳐 `std::ofstream` + 바이너리(`ios::binary`)로 출력할 것.

### ⚠️ Windows 헤더 min/max 매크로와 C++ <algorithm> 충돌 방지
- `#define NOMINMAX`를 Windows 헤더 인클루드 전에 선언하여 매크로 오염을 사전에 차단할 것.

---

# 2. easydownload 프로젝트 특화 실수 및 개발 규칙 (Project-Specific Traps)

본 프로젝트(다운로드 가속기, 크롬 확장 프로그램 연동, 멀티파트 분할 다운로드, 비디오 스트림 추출/다운로드) 도메인 특성상 준수해야 할 필수 규칙입니다.

### ⚡ 멀티스레드 분할 다운로드(Chunked / Range Download) 무결성 원칙
1. **Range 헤더 지원 여부 선행 검사 (`Accept-Ranges: bytes`)**:
   - 서버가 `Range` 헤더를 지원하지 않는 경우(`206 Partial Content` 대신 `200 OK` 반환), 즉시 단일 스트림 다운로드 모드로 폴백하여 데이터 손상(0부터 다시 덮어쓰기)을 방지할 것.
2. **사전 파일 크기 할당 (`SetFilePointerEx` + `SetEndOfFile`)**:
   - 다운로드 시작 즉시 최종 크기만큼 파일을 사전 할당(Sparse 또는 사전 생성)하여 디스크 단편화를 줄이고 각 분할 청크가 서로 다른 오프셋에 동시 안전 기록(Pwrite)될 수 있도록 구성할 것.
3. **다운로드 청크 병합 시 디스크 복사 낭비 배제**:
   - 임시 조각 파일들을 여러 개 만든 후 메인 파일로 복사하는 비효율적인 방식 대신, 하나의 타겟 파일에 오프셋별로 직접 기록(`OVERLAPPED` 또는 `pwrite` 스타일)하거나 안전한 청크 파일 세그먼트 포맷을 사용할 것.
4. **동적 작업 인수(Dynamic Work-Stealing) 및 미완료 큐(`RangeQueue`) 무결성**:
   - 워커 스레드를 단일 고정 청크에 1:1로 고정하지 말 것.
   - 서버의 동시 접속 제한(IP당 제한)이나 연결 끊김으로 일부 워커가 중단될 경우, 미완료 바이트 범위(`currentOffset ~ endByte`)를 중앙 `m_rangeQueue`에 즉시 반환할 것.
   - 살아있는 단 1개의 워커라도 큐의 잔여 범위를 순차적으로 인수(Work-Stealing)하여 소진하도록 하여, 어떤 접속 단절 환경에서도 단 1바이트의 누락 없이 100% 파일 완성을 보장할 것.
   - 여유 스레드는 대용량 잔여 청크(>= 2MB)를 실시간으로 반씩 분할(`TryStealWork`)하여 최대 12개 한도 내에서 동적 가속을 유지할 것.
5. **속도 적응형 동적 분할(Adaptive Speed-Ramping) 원칙**:
   - 무조건 처음부터 최대 분할 수(12개 등)로 시작하지 말 것. 단일 스트림으로도 최고 속도가 나오는 고속 CDN 서버는 무의미한 분할이 오히려 소켓 낭비와 서버 차단을 유발함.
   - 1~2개 연결로 가볍게 시작한 뒤, 실시간 속도 증감(12% 이상 상승 여부)을 판별하여 다중 연결로 속도가 오르는 서버에서만 최대 12개까지 동적으로 청크를 증설하고, 속도 증가가 미미하거나 포화된 경우 즉시 추가 분할을 멈추어 최적의 리소스로 최고 효율을 유지할 것.

### 🌐 크롬 확장 프로그램(Manifest V3) 및 로컬 브리지(Native Host / Local API) 규칙
1. **Chrome 다운로드 인터셉트 무결성**:
   - `chrome.downloads.onCreated` 및 `chrome.downloads.onDeterminingFilename` 이벤트를 가로채어 브라우저 내장 다운로드를 안전하게 취소(`chrome.downloads.cancel`)하고, 해당 요청의 URL, Referer, Cookies, Filename을 로컬 EasyDownload 엔진으로 완벽 전달할 것.
2. **Local HTTP / WebSocket 통신 및 방화벽/CORS 방어**:
   - 확장 프로그램과 데스크톱 앱 간 통신은 표준 Native Messaging Host 또는 로컬 루프백 엔드포인트(`127.0.0.1:18989`)를 통해 통신하되, Origin/Header 검증 및 보안 토큰을 적용하여 안전성을 확보할 것.
3. **비디오 스트리밍 탐지(HLS / M3U8 / MP4 Sniffing)**:
   - `chrome.webRequest` / `chrome.declarativeNetRequest` 리스너로 `.m3u8`, `.mpd`, `.mp4`, `.ts`, `video/*` 네트워크 트래픽을 감지하여 탭별 비디오 목록을 수집하고, 브라우저 상단 확장 아이콘에 배지(Badge) 카운트 표시 및 다운로드 팝업 UI를 제공할 것.

### 🎬 HLS(M3U8) 비디오 다운로드 및 자동 결합 파이프라인
1. Master Playlist / Media Playlist 파싱:
   - 다중 해상도(Multi-bitrate) M3U8인 경우 최상위 해상도를 기본 선택하되 해상도 선택이 가능하도록 할 것.
2. 암호화되지 않은 세그먼트(`.ts`)들을 멀티스레드로 고속 병렬 다운로드하고 단일 `.mp4` / `.ts` 컨테이너로 손실 없이 순차 병합할 것.
3. 복잡한 스트림 및 YouTube 등의 비디오 URL 분석을 위해 `yt-dlp` / `ffmpeg`와의 스마트 연동 파이프라인을 내장 지원할 것.

---

# 3. easydownload 빌드 및 리소스 실수 박제 (Build & Resource Traps)

### ⚠️ Winsock2 와 Windows.h 헤더 충돌 (`error C2011: 'sockaddr': struct 형식 재정의`)
- **실제 실수**: 로컬 브리지 서버 소켓을 위해 `<winsock2.h>`와 `<ws2tcpip.h>`를 사용할 때, `main.cpp` 등에서 `<windows.h>`가 먼저 include되거나 `WIN32_LEAN_AND_MEAN`이 누락되어 구형 `<winsock.h>`(v1.1)가 선점 로드됨. 이로 인해 200여 개의 심벌 중복 및 구조체 재정의 컴파일 에러 발생.
- **방지 원칙**: `.vcxproj`의 `PreprocessorDefinitions`에 `WIN32_LEAN_AND_MEAN;`을 글로벌 선언하고, 모든 소스에서 `<winsock2.h>`를 `<windows.h>`보다 반드시 먼저 include할 것.

### ⚠️ 매니페스트 리소스 2중 임베딩 충돌 (`CVTRES : fatal error CVT1100`)
- **실제 실수**: `.rc` 파일에 `1 RT_MANIFEST "manifest.xml"`을 수동 선언한 상태에서, `.vcxproj`의 `<Manifest>` 항목 및 링커 옵션(`/manifest:embed`)이 활성화되어 있어 COFF 변환 시 `CVT1100: 리소스가 중복되었습니다. 형식:MANIFEST, 이름:1` 에러로 링크가 실패함.
- **방지 원칙**: MSBuild / MSVC 프로젝트에서 `<Manifest>` 태그 및 링커 매니페스트 병합을 사용할 경우, `.rc` 파일 내부의 수동 `RT_MANIFEST` 선언 라인은 반드시 제거하여 중복 임베딩을 원천 차단할 것.

### ⚠️ 리소스 컴파일러 `IDC_STATIC` 미정의 오류 (`RC2104`)
- **실제 실수**: 다이얼로그 라벨이나 그룹박스에 `IDC_STATIC`을 사용했으나, `resource.h`에 해당 매크로가 정의되어 있지 않아 rc.exe 컴파일 시 `RC2104: undefined keyword or key name: IDC_STATIC` 에러 발생.
- **방지 원칙**: `resource.h` 상단에 반드시 `#ifndef IDC_STATIC \n #define IDC_STATIC (-1) \n #endif` 가드를 필수로 배치할 것.

### ⚠️ 코어 모듈 상태 열거형(Enum) 심벌 불일치 (`error C2065: 'Failed': 선언되지 않은 식별자입니다`)
- **실제 실수**: UI 다이얼로그(`DownloadProgressDialog.cpp`)에서 작업 실패 상태를 처리할 때, 코어 `DownloadTask.h`에 정의된 `TaskStatus::Error` 대신 임의로 유추한 `TaskStatus::Failed`를 사용하여 컴파일 에러 발생.
- **방지 원칙**: 코어 상태 열거형이나 콜백을 신규 UI 대화상자에 연동할 때는 반드시 `DownloadTask.h` 등의 원본 정의를 사전 확인하고 프로젝트 전반의 통일된 열거형 심벌(Queued, Connecting, Downloading, Paused, Completed, Error)을 정확히 사용할 것.

### ⚠️ CreateDialogParam 비동기 오인 및 WM_INITDIALOG 핸들 바인딩 누락 (진행바 멈춤 버그)
- **실제 실수**: `CreateDialogParamW()` 호출 후 전역 상태 맵에 핸들을 등록하는 구조로 작성하였으나, `CreateDialogParamW`는 함수 반환 전에 동기식으로 `WM_INITDIALOG`를 먼저 호출함. 이로 인해 `WM_INITDIALOG` 시점에 상태 객체를 찾지 못해 컨트롤 핸들(`hProgressBar`, `hSegmentBar`)이 모두 `NULL`로 남아 타이머 갱신 메시지(`SendMessage(NULL, PBM_SETPOS, ...)`)가 무시되어 다운로드 중에도 진행바가 전혀 움직이지 않는 치명적인 버그 유발.
- **방지 원칙**: 모달리스 다이얼로그 생성 시 `WM_INITDIALOG` 내부에서 `SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)pState)`를 통해 상태 객체와 컨트롤 핸들을 창 생성 즉시 완전하게 바인딩하고, 리소스 선언(`.rc`) 시에도 프로그레스 바에는 `PBS_SMOOTH | WS_CHILD | WS_VISIBLE | WS_BORDER`를 필수로 명시할 것.

### ⚠️ Windows 배치 스크립트(.bat) 한글 깨짐 (CP949 vs UTF-8 인코딩)
- **배경**: 한국어 Windows 명령 프롬프트(`cmd.exe`)는 기본 콘솔 인코딩으로 CP949(코드페이지 949)를 사용함.
- **실제 실수**: `install_extension.bat` 등 한글 안내가 포함된 윈도우 배치 파일을 일반 UTF-8로 저장하여, 기본 콘솔 환경(`cmd.exe` 기본 코드페이지 949)에서 실행 시 한글 텍스트가 전부 모지바케(깨진 문자)로 출력됨.
- **방지 원칙**: 윈도우 콘솔 명령 프롬프트(`cmd.exe`)용 배치 파일(`.bat`)은 UTF-8이 아닌 한국어 Windows ANSI 규격인 **CP949(EUC-KR)**로 명시적으로 인코딩하여 저장할 것.

### ⚠️ 무분별한 트레이 알림(Balloon / Toast) 스팸 및 알림음 방지 (`NIIF_NOSOUND`)
- **실제 실수**: 창을 닫을 때(`WM_CLOSE`)나 백그라운드로 최소화할 때마다 `ShowTrayNotification()`을 호출하여 시스템 트레이 알림 풍선과 Windows 알림 경고음("딩동~")을 연속으로 재생시켜 사용자에게 소음 피로감을 유발함.
- **방지 원칙**: 
  1. 창 닫기(`WM_CLOSE`)나 시작 시 최소화는 조용히 숨김(`ShowWindow(SW_HIDE)`) 처리만 수행하고 불필요한 트레이 알림을 일절 띄우지 말 것.
  2. 트레이 알림을 발송해야 하는 필수 상황에서도 `dwInfoFlags`에 반드시 `NIIF_NOSOUND`를 기본 지정하여 Windows 시스템 사운드가 사용자의 작업을 방해하지 않도록 할 것.

### ⚠️ 확장자별/카테고리별 다운로드 지정 폴더 우회 및 기본 폴더 하드코딩 버그
- **실제 실수**: 사용자가 환경 설정에서 동영상(`Category_video`) 등 특정 확장자의 전용 저장 폴더를 지정했음에도 불구하고, 비디오 다운로드 대화상자(`VideoDownloadDialog.cpp`)나 크롬 확장 수신 루틴(`WM_EXT_DOWNLOAD`)에서 파일명에 따른 카테고리 경로(`GetTargetFolderForFilename`)를 평가하지 않고 `GetDefaultDownloadDir()`(기본 폴더)를 하드코딩 호출하여 지정된 폴더가 무시되는 심각한 결함을 유발함.
- **방지 원칙**: 
  1. 새 다운로드, 비디오 다운로드, 브라우저 확장 인터셉트 등 모든 경로 결정 시점에는 파일명/확장자를 바탕으로 항상 `Settings::Instance().GetTargetFolderForFilename(filename)`을 최우선 평가하여 지정된 카테고리 폴더로 분기할 것 (지정되지 않은 경우에만 기본 폴더로 폴백).
  2. 다운로드 입력 대화상자에는 사용자가 최종 저장 위치를 눈으로 확인하고 즉시 변경할 수 있도록 '저장 위치' 에디트와 '찾아보기' 버튼을 필수로 구비할 것.

### ⚠️ 웹 비디오 감지 오버레이 버튼 화면 가림(Occlusion) 및 부피 방지 원칙
- **실제 실수**: 웹 비디오 위에 긴 텍스트 버튼(`⚡ 이 동영상 다운로드 ▾`)을 영상 화면 내부(`top: 12px, right: 12px`)에 크게 배치하여, 영상 재생 중 자막이나 상단 컨트롤을 가리는 심각한 UX 불편을 초래함.
- **방지 원칙**: 
  1. 비디오 다운로드 버튼은 영상 화면 안쪽을 가리지 않도록 **영상 프레임 바로 위쪽 바깥(`top: -30px`)**에 배치할 것.
  2. 버튼 형태는 부피가 큰 텍스트 배너가 아닌 **초소형 컴팩트 아이콘(28×28px, 📥 SVG)**으로 구현하고, 툴팁을 제공하며 화질/용량 메뉴는 클릭 시에만 드롭다운으로 펼쳐지도록 구성할 것.

### ⚠️ 동일 파일명 다운로드 시 덮어쓰기 방지 및 자동 넘버링 원칙 (`GenerateUniqueSavePath`)
- **실제 실수**: 이미 다운로드 완료된 파일과 동일한 이름의 파일을 다시 다운로드하거나 큐에 동일 파일명 작업이 있는 상태에서 새 작업을 등록할 때, `CreateFileW(..., OPEN_ALWAYS)`로 기존 파일을 그대로 덮어쓰거나 `file (1).ext` 등의 번호 증분 처리를 하지 않아 기존 데이터를 유실시키는 치명적인 결함을 유발함.
- **방지 원칙**: 
  1. 다운로드 작업 등록 전 또는 대화상자 경로 생성 시 `PathFileExistsW` 및 활성 작업 큐 조회를 통해 동일 파일명이 존재할 경우 자동으로 `(1)`, `(2)` 접미사를 붙이는 `GenerateUniqueSavePath`를 중앙 집중형으로 호출하여 고유 저장 경로를 확정할 것.
  2. 다운로드 입력 대화상자(`NewDownloadDialog`, `VideoDownloadDialog`)에서도 파일명 변경 시 중복 검사를 실시간으로 연동하여 사용자가 직관적으로 최종 저장 파일명을 확인할 수 있도록 할 것.

### ⚠️ 클라우드 스토리지(Gofile 등) 다운로드 실패 방지: Manifest 권한, Root 도메인 쿠키, WinHTTP 리다이렉트/Referer
- **실제 실수**: Gofile.io 등 최신 클라우드 스토리지는 서브도메인 다운로드 서버(`store-eu-par-10.gofile.io`) 호출 시 루트 도메인(`.gofile.io`)의 세션 토큰 쿠키와 일치하는 `Referer`(`https://gofile.io/`)를 강제함. 확장 프로그램 `manifest.json`에 `"cookies"` 권한이 누락되어 쿠키 조회가 차단되었고, 대상 호스트 쿠키만 조회하여 루트 쿠키를 누락했으며, C++ 엔진에서도 `Referer` 헤더 누락 및 `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS` 부재로 인해 다운로드 대신 3KB HTML 차단 페이지를 수신하는 결함을 발생시킴.
- **방지 원칙**: 
  1. 확장 프로그램 매니페스트에 `"cookies"` 권한을 포함하고, 대상 URL뿐만 아니라 부모 루트 도메인(`origin`, `.gofile.io` 등)의 쿠키까지 통합 수집(`chrome.cookies.getAll`)하여 백엔드로 전달할 것.
  2. C++ `HttpEngine`에서는 요청 시 `Referer` 헤더를 누락 없이 조합하여 전송하고, `WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS`를 설정하여 크로스 도메인 리디렉션을 안전하게 추적할 것.

### ⚠️ 동적 가속 분할 청크 큐 누락 방지 및 완료 보정 원칙 (`TryStealWork` Orphan Chunk 방지)
- **실제 실수**: `Adaptive speed-ramping`에서 다운로드 속도 가속을 위해 `TryStealWork(stealRange)`를 통해 기존 청크를 동적 분할하고 `m_chunks`에 신규 청크를 등록했으나, 분할된 `stealRange`를 작업 큐(`m_rangeQueue`)에 `push_back`하지 않고 새 워커 스레드만 띄움. 이로 인해 생성된 1~2개 청크는 어느 스레드도 할당받지 못한 고아 청크(`downloaded: 0, completed: false`)로 남아, 파일 전체는 100% 정상 다운로드되었음에도 분할 진행 화면(SegmentBar)에서는 다운로드가 끝날 때까지 1~2개 청크가 미완료(파란색/빈칸) 상태로 남는 황당한 UI 결함을 유발함.
- **방지 원칙**: 
  1. 동적 작업 분할(Work-Stealing) 시 새로 분리된 청크 범위(`stealRange`)는 반드시 작업 큐(`m_rangeQueue`)에 즉시 등록(`push_back`)하여 워커 스레드가 정상 인수하도록 보장할 것.
  2. 쪼개진 원본 청크를 다운로드 중인 기존 워커 스레드 역시 동적으로 단축된 `effectiveEndByte`를 실시간 확인하여 중복 다운로드 없이 즉시 완료 처리할 것.
  3. 작업 완료(`TaskStatus::Completed`) 시점에는 `m_chunks`의 모든 세그먼트 상태를 `cp.completed = true`, `downloadedBytes = chunkTotal`로 명시적 강제 동기화하여 분할 화면이 100% 에메랄드 그린으로 완성되도록 할 것.

### ⚠️ 외부 임베드 플레이어 비디오 탐지를 위한 Chrome 확장 iframe 주입 원칙 (`all_frames: true`)
- **실제 실수**: Pornavhd, Streamtape, Doodstream 등 대다수 웹 비디오 사이트는 플레이어를 `<iframe>`으로 임베드하여 서비스함. 확장 프로그램 `manifest.json`에 `"all_frames": true`가 누락되어 최상위 부모 창에만 `content.js`가 주입되고, 정작 실제 `<video>` 태그가 존재하는 iframe 내부에는 스크립트가 실행되지 않아 다운로드 버튼이 전혀 나타나지 않는 심각한 탐지 실패를 초래함.
- **방지 원칙**: 
  1. 비디오 스니핑이나 DOM 오버레이를 수행하는 확장 프로그램의 `content_scripts`에는 반드시 `"all_frames": true`를 명시하여 모든 iframe 내부의 미디어 태그까지 빈틈없이 탐지할 것.
  2. `play`, `playing`, `loadedmetadata`, `canplay` 등 미디어 재생 이벤트를 캡처 모드로 리스닝하여, 재생 버튼을 누르는 순간 지연 생성되는 비디오 태그도 즉각 포착하여 다운로드 아이콘을 부착할 것.
  3. 브라우저 툴바 액션 아이콘에는 전용 팝업 메뉴(`default_popup: "popup.html"`)를 항상 제공하여, 웹페이지 내 오버레이가 가려진 환경에서도 사용자가 툴바 클릭 한 번으로 감지된 스트림 목록을 확인하고 즉시 다운로드할 수 있도록 보장할 것.

### ⚠️ HLS(M3U8) 다운로드 시 코어 엔진(`DownloadTask::WorkerLoop`) 연동 누락 및 Referer 차단 방지
- **실제 실수**: `HlsDownloader::DownloadHlsStream` 세그먼트 병합 모듈이 구현되어 있었음에도, 실제 다운로드를 총괄하는 `DownloadTask::WorkerLoop`에서 HLS 여부를 분기하지 않고 `.m3u8` 텍스트 파일 자체를 단일 파일로 다운로드하려 시도함. 또한 비디오 작업 등록 시 `referer`를 누락하여 `recordplay.biz` 등 동영상 CDN 서버가 403 Forbidden으로 차단해 동영상 다운로드가 완전히 실패하고 다중 화질 목록도 뜨지 않는 치명적인 결함을 유발함.
- **방지 원칙**: 
  1. `DownloadTask::WorkerLoop` 최상단에서 `.m3u8` 또는 `m_isHls` 플래그를 검사하여 `HlsDownloader::DownloadHlsStream`으로 즉시 분기하고, 모든 `.ts` 세그먼트를 멀티스레드로 수신하여 타겟 파일로 무손실 순차 병합하도록 파이프라인을 완전하게 연결할 것.
  2. M3U8 마스터/미디어 파싱 및 세그먼트 다운로드 전 과정에 원본 페이지 URL(`referer`)을 필수로 전달하여 스트리밍 CDN의 도난 방지 차단(Hotlinking 403)을 원천 방지할 것.
  3. `index-f3-v1-a1.m3u8` 같은 서브 화질 주소가 인입된 경우, 동일 경로의 상위 마스터(`master.m3u8` 또는 `index.m3u8`)를 스마트 탐색하여 1080p, 720p, 480p 등 전체 화질 목록을 빠짐없이 선택할 수 있도록 보장할 것.
  4. 세그먼트 다운로드 루틴에는 최소 3회의 지수 백오프 재시도(Retry) 루프를 장착하여 네트워크 불안정으로 인한 세그먼트 누락 실패를 철저히 방어할 것.

### ⚠️ Iframe 내부 오버레이 렌더링 차단 및 최상위 부모 창 바깥 배치 원칙 (IDM 스타일 플레이어 상단 앵커링)
- **실제 실수**: `all_frames: true`를 적용한 상태에서 `content.js`가 iframe 내부에서 실행될 때 `<video>`의 `rect.top`이 0이 됨. 이로 인해 버튼이 플레이어 영상 화면 내부(`top: 6px`)에 강제로 렌더링되어 영상 자막과 재생 컨트롤을 가리고 시야를 방해하는 치명적인 UI 결함 발생.
- **방지 원칙**: 
  1. `window.top !== window.self`인 서브프레임(iframe) 내부에서는 DOM 버튼이나 스타일 주입을 원천 차단하고, 순수 비디오 재생 이벤트 포착 및 `chrome.runtime.sendMessage({ action: "videoDetected" })` 통지만 수행할 것.
  2. 다운로드 버튼 UI는 오직 최상위 창(`window.top`)에서만 생성하되, `<video>`뿐만 아니라 플레이어 `<iframe>`의 바운딩 렉트(`getBoundingClientRect()`)를 감지하여 해당 플레이어 프레임의 **바깥 바로 위(`top = rect.top - 34px`)**에만 정확하게 배치할 것.

### ⚠️ 인페이지 비디오 다운로드 트리거 버튼 폼 팩터 원칙 (컴팩트 정사각 아이콘)
- **실제 실수**: 인페이지 다운로드 버튼에 "동영상 다운로드"와 같은 긴 텍스트를 포함하여 바 형태로 렌더링함으로써, 플레이어 상단 영역을 불필요하게 많이 차지하고 시야를 가리는 답답한 레이아웃을 초래함.
- **방지 원칙**: 인페이지 다운로드 버튼은 불필요한 텍스트 라벨 없이 **28×28px 규격의 컴팩트한 정사각 📥 다운로드 아이콘 단독 버튼**으로 축소 배치하고, 상세 화질/용량 선택은 클릭 시 드롭다운 메뉴로 제공할 것.

### ⚠️ 다운로드 목록 삭제 동작의 안전성 및 단순성 원칙 (목록 삭제와 디스크 파일 분리)
- **실제 실수**: 다운로드 리스트에서 항목 삭제 시 `MB_YESNOCANCEL` 메시지박스를 띄워 파일까지 함께 지울지 번거롭게 질의하거나 혼란을 유발함.
- **방지 원칙**: 작업 관리자 목록에서의 삭제는 항상 **다운로드 큐/목록에서만 삭제(`deleteFile = false`)**하도록 동작을 일원화하고, 원본 파일의 디스크 보존을 기본 원칙으로 삼을 것.

### ⚠️ 다운로드 진행창 버튼 좌우 레이아웃 및 완료 시 닫기 UX 원칙
- **실제 실수**: 진행창의 완료 후 동작에 대한 고려 없이 '취소' 버튼만 유지되어, 다운로드 완료 후 사용자가 직관적으로 창을 닫을 수 있는 '닫기' 버튼이 부재함. 또한 버튼 배치가 우측에 편중되거나 파일 열기 버튼 위치가 부자연스러웠음.
- **방지 원칙**:
  1. 진행창 하단 버튼은 **좌측(Left)에 파일/폴더 접근(`파일 열기`, `폴더 열기`)**, **우측(Right)에 작업 제어(`일시정지`, `취소/닫기`)**로 시각적 역할을 명확히 분리할 것.
  2. 다운로드가 완료(`TaskStatus::Completed`)되면 즉시 `취소` 버튼을 **`닫기`**로 자동 전환하고 `일시정지` 버튼은 숨겨(Hide), 사용자가 엔터/Esc나 클릭 한 번으로 자연스럽게 창을 닫을 수 있도록 완결할 것.

### ⚠️ M3U8 스트리밍 분할 화면(SegmentBar) 미표시 방지 및 청크 상대 스팬(Span) 렌더링 원칙
- **실제 실수**: HLS 스트림은 초기에 `m_totalBytes`가 0이고 이후 계산되더라도 수억 바이트로 증가함. `SegmentBar::Paint`에서 `totalBytes <= 0` 검사에 걸려 안내 텍스트만 표시되거나, 8개 청크의 가상 바이트 오프셋(0~8000)과 실제 파일 바이트(수억 바이트)의 스케일 불일치로 청크가 0픽셀로 찌그러져 동영상 다운로드 중 분할 화면이 전혀 나오지 않는 치명적인 렌더링 버그 발생.
- **방지 원칙**: `SegmentBar`는 `totalBytes`의 유무나 크기에 종속되지 않고, 등록된 `chunks`의 개수 및 각 청크의 상대적 비율(`chunkTotal / totalSpan`)을 기반으로 부모 영역(`innerW`)에 100% 균등 매핑하여 렌더링할 것. 이를 통해 HTTP Range 다운로드든 HLS 8분할 스트림이든 상관없이 1~8번 청크와 실시간 다운로드 진행 바가 선명하게 표시되도록 보장할 것.

### ⚠️ 크롬 확장 동영상 다운로드 2중 팝업 방지 및 원스톱 자동 시작 원칙
- **실제 실수**: 사용자가 크롬 확장 프로그램이나 웹 인페이지 메뉴에서 이미 화질(1080p 등)을 선택하고 [다운로드]를 눌렀음에도, 데스크톱 프로그램의 `WM_EXT_VIDEO` 핸들러가 또다시 `VideoDownloadDialog` 분석 창을 띄워 사용자가 또 "비디오 다운로드" 버튼을 수동으로 눌러야만 다운로드가 시작되는 불필요한 번복과 지연을 유발함.
- **방지 원칙**: 브라우저 확장으로부터 비디오 다운로드 요청(`WM_EXT_VIDEO`)이 인입되면 추가 질의나 모달 창 없이 즉시 카테고리 저장 경로를 확정하고 `AddTask(..., startImmediately = true)`로 자동 시작한 뒤 진행 창(`DownloadProgressDialog`)을 전면에 표시하여 사용자가 아무런 추가 조작 없이 즉각 다운로드를 체감할 수 있도록 원스톱 자동화할 것.

### ⚠️ 다운로드 완료 후 자동 창 닫기 UX 및 사용자 선택 유지 원칙
- **실제 실수**: 완료 시 파일을 무조건 강제로 열도록 유도하여 대량 다운로드 환경에서 불필요한 프로그램 실행 폭주를 유발함.
- **방지 원칙**: "다운로드 완료 시 창 자동으로 닫기" 옵션을 기본 제공하여, 완료 시 깔끔하게 다이얼로그만 자동 종료되도록 지원하고 체크 상태를 전역 유지할 것.

### ⚠️ 완료된 작업 확인 시 진행 창 재활용 금지 및 전용 상세 정보창(Properties Dialog) 분리 원칙
- **실제 실수**: 이미 완료된 다운로드 항목을 리스트에서 열었을 때도 진행 중 창(`DownloadProgressDialog`)을 그대로 재활용함. 이로 인해 '완료 시 창 닫기' 체크박스가 켜져 있으면 창이 열리자마자 0.1초 만에 플래시처럼 닫혀버려 정보를 전혀 확인할 수 없는 치명적인 UX 결함 발생.
- **방지 원칙**:
  1. 완료된 작업(`TaskStatus::Completed`)은 진행 바나 자동 닫기 타이머가 전혀 없는 **전용 `DownloadInfoDialog`(상세 정보창)**로 분기하여 띄울 것. 사용자가 직접 [닫기]를 누르기 전까지 파일명, 용량, 전체 경로, 원본 URL, 시작/완료 시간을 안정적으로 확인하고 [파일 열기], [폴더 열기], [주소 복사]를 자유롭게 수행할 수 있도록 보장할 것.
  2. 진행 창(`DownloadProgressDialog`)에서도 실시간 다운로드 중 ➡️ 완료로 전이되는 순간에만 자동 닫기가 동작하도록 `wasActiveOnOpen` 안전 불리언 가드를 필수로 장착할 것.

### ⚠️ 다운로드 목록 우클릭 시 단일 창 강제 오픈 금지 및 표준 컨텍스트 메뉴 제공 원칙
- **실제 실수**: 리스트 항목 우클릭 시 단일 정보 창만 강제로 열려, 파일 열기, 폴더 탐색, 다운로드 주소 복사 등 자주 쓰이는 핵심 파일 액션을 수행하기 위해 다른 곳을 찾아 헤매게 만듦.
- **방지 원칙**: 우클릭(`NM_RCLICK`) 시 표준 팝업 메뉴(`TrackPopupMenu`)를 열어 **열기, 폴더 열기, 다운로드 주소 복사, 다운로드 정보/진행 창**을 즉시 선별 실행할 수 있도록 완결할 것.

















