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
       `& "D:\coding\visualstudio\easyinstall\bin\Release\easyinstall.exe" -i "<입력_바이너리_또는_폴더>" -o "<출력경로>\Setup.exe" -n "<프로그램명>" -v "<버전>" -p "ai-open.kr" -m "<메인_exe명>"`  
       *(예: `& "D:\coding\visualstudio\easyinstall\bin\Release\easyinstall.exe" -i "D:\coding\visualstudio\easydownload\bin\Release" -o "D:\coding\visualstudio\easydownload\EasyDownload_Setup.exe" -n "EasyDownload" -v "1.4.7" -p "ai-open.kr" -m "easydownload.exe"`)*
       > [!CAUTION]
       > **단일 파일 vs 폴더 패키징 맹점 방지**: DLL(`WinDivert.dll`), 드라이버(`WinDivert64.sys`), 확장 프로그램 등 동적 의존 파일이 있는 프로젝트에서 `-i`에 단일 `exe` 경로만 전달하면 부속 파일들이 패키징에서 전부 누락됨. 이로 인해 설치 후 기존 구버전 DLL이 잔존하거나 DLL이 없어 `프로시저 시작 지점(WinDivertShutdown 등)을 찾을 수 없습니다` 오류가 발생함. 따라서 반드시 **바이너리 전체가 빌드된 폴더(`bin\Release`)를 `-i`로 전달**하여 모든 종속 파일이 100% 동봉되도록 할 것!
    2. **무설치 포터블 압축 아카이브 생성**:  
       `powershell -Command "Compress-Archive -Path '<바이너리_경로>' -DestinationPath '<출력경로>\<프로그램명>_portable.zip' -Force"`
    3. 설치 파일 생성 요구 시 타사 도구(`iscc`, `makensis` 등)를 일절 찾지 말고, 위 `easyinstall.exe` 절대 경로를 이용하여 즉시 1초 만에 원스톱으로 빌드할 것.
    4. **인스톨러(Setup.exe)의 기존 프로세스 감지 시 대상 디렉터리 종속 맹점 방지 원칙**:
       - `findRunningProcesses`에서 프로세스를 감지할 때 단순히 `targetDir` 내부 경로만 검사하면, 사용자가 포터블 폴더, 개발/빌드 폴더(`bin\Release`), 혹은 이전 경로에서 실행 중인 동일 프로그램을 감지하지 못하고 그냥 지나쳐 창이 닫히지 않고 설치되는 결함이 발생함.
       - 반드시 `curExe == mainExeName`을 최우선 검사하여 **어느 경로에서 실행 중이든 동일한 실행 파일명이면 무조건 프로세스 종료 대상(PID)에 포함**시켜, 100% 안전하게 창을 닫고 교체 설치를 완결할 것.

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

### 📑 Win32 단일 다이얼로그 탭 컨트롤(`SysTabControl32`) 구현 시 정적 컨트롤(`IDC_STATIC`) 중복 충돌 방지 및 그룹핑 원칙
- **배경**: 별도의 자식 대화상자(Child Dialog)를 여러 개 생성하지 않고 단일 대화상자 템플릿에 모든 컨트롤을 배치한 뒤 `ShowWindow(SW_SHOW / SW_HIDE)`로 탭 전환을 구현할 때, 그룹박스나 안내 문구 라벨을 리소스 기본값인 `IDC_STATIC`(-1)으로 남겨두면 치명적인 문제가 발생함.
- **실제 문제**: `GetDlgItem(hDlg, IDC_STATIC)`을 호출하면 윈도우 내부 Z-순서상 최초 1개의 정적 컨트롤 핸들만 반환되므로, 다른 탭에 속한 그룹박스나 라벨들이 숨겨지지 않고 화면에 그대로 중첩되어 잔상이 남거나 텍스트가 겹치는 렌더링 결함이 발생함.
- **방지 원칙**:
  1. 탭 전환 대상이 되는 모든 정적 라벨과 그룹박스는 `resource.h`에 명시적인 고유 ID(`IDC_GRP_TAB0_1`, `IDC_LBL_TAB0_DESC` 등)를 반드시 부여할 것.
  2. 대화상자 소스코드에서 각 탭 인덱스별 컨트롤 ID 정수 배열(`s_tab0Controls[]`, `s_tab1Controls[]` 등)을 선언하고, `TCN_SELCHANGE` 발생 시 해당 탭의 컨트롤만 `SW_SHOW`, 타 탭의 컨트롤은 `SW_HIDE`로 완벽하게 토글할 것. 이를 통해 깜빡임이나 잔상 없이 가볍고 빠른 네이티브 탭 UI를 완성할 수 있음.

### 🪟 Win32 탭 대화상자 내 정적 텍스트/체크박스 사각형 배경 얼룩 방지 (`WM_CTLCOLORSTATIC` 투명화)
- **실제 실수/문제**: `SysTabControl32`나 테마 대화상자 위에 배치된 정적 텍스트(`LTEXT`) 및 체크박스(`BS_AUTOCHECKBOX`)는 기본 윈도우 배경색(`COLOR_BTNFACE`) 브러시로 영역을 채우므로, 탭 페이지나 그라데이션 배경 위에 지저분한 사각형 배경 얼룩 박스가 나타남.
- **방지 원칙**:
  1. `WM_INITDIALOG`에서 `EnableThemeDialogTexture(hDlg, ETDT_ENABLETAB)`을 호출하여 부모 대화상자의 테마 텍스처를 활성화할 것.
  2. 다이얼로그 프로시저에서 `WM_CTLCOLORSTATIC` 메시지를 가로채어 반드시 `SetBkMode((HDC)wParam, TRANSPARENT);`를 설정하고 `return (INT_PTR)GetStockObject(NULL_BRUSH);`를 반환할 것. 이를 통해 텍스트와 라벨이 대화상자 배경과 완벽하게 융합되어 투명하게 렌더링됨.

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
5. **회선 대역폭 포화 기반 속도 적응형 동적 분할(Bandwidth-Saturating Adaptive Splitting) 원칙**:
   - **가속 및 분할 목적**: 무조건적인 다중 연결 남발이 아니라, '연결 수를 능동적으로 늘려 다운로드 속도를 사용자의 실제 인터넷 회선 한계치까지 극대화'하는 것이 목적임.
   - **TCP Slow-Start 및 속도 가속 중 연결 추가 보류**: 새 연결이 추가되거나 초기 기동 직후에는 TCP Slow-Start 윈도우 스케일링으로 속도가 상승하는 과정이므로 최소 2.5~3초의 웜업(Cooldown) 유예 기간을 부여할 것. 이전 초 대비 속도가 계속 증가 중(5% 이상 상승)일 때는 연결 추가를 보류하고 속도가 자연스럽게 치솟도록 대기함.
   - **속도 정체(Plateau) 확인 후 순차 1개씩 증설**: 속도 상승이 멈추고 2초 이상 정체되었으며, 그 정체 속도가 사용자가 설정한 인터넷 회선 대역폭 한계치에 미치지 못할 때에만 가장 큰 잔여 청크(>= 4MB)를 딱 1개씩 순차적으로 쪼개어 추가 연결을 붙임.
   - **대역폭 기준 분할 중단(Saturation Protection)**: 사용자의 인터넷 회선 속도(100M 광랜 약 11MB/s, 500M 기가슬림 약 55MB/s, 1G 기가인터넷 약 110MB/s, 2.5G+ 약 250MB/s)를 옵션으로 설정받고, 현재 측정 속도가 이미 해당 대역폭 임계치를 충족하여 충분히 빠를 경우 불필요한 청크 분할 및 연결 증설을 즉시 멈추어 공유기 과부하 및 서버 차단을 방어함.
6. **다운로드 중단/취소 시 작업 상태(`m_status`) 갱신 및 재시작(`Resume`) 무결성**:
   - **실제 실수**: 다운로드 도중 창에서 취소를 누를 때 스레드 중단 플래그(`m_abortFlag`)만 켜고 작업 상태(`m_status`)를 `Paused(중단됨)`로 명시적 갱신하지 않아, 스레드가 멈춘 뒤에도 내부 상태가 여전히 `Downloading`으로 잔존함. 이로 인해 창을 다시 열었을 때 이미 멈춘 작업임에도 `Downloading` 상태로 인식되어 '재시작' 대신 '일시정지'/'취소' 버튼이 그대로 노출되고, 재시작(`Resume`) 시도 시에도 가드에 걸려 다운로드가 멈춰버리는 치명적 결함 발생.
   - **방지 원칙**: 작업 취소(`Cancel`) 및 일시중단(`Pause`) 시에는 반드시 현재 다운로드 청크 진행 상태(`.egpart`)를 즉시 디스크에 저장(`SaveResumeState`)하고, 작업 상태를 명확히 `TaskStatus::Paused`로 갱신하며, `m_currentSpeed = 0` 및 작업 목록 영속화(`SaveTasks`)를 호출하여 상태 무결성을 100% 보장할 것. 또한 UI 버튼은 다운로드 중에는 `중단`, 중단 후에는 `재시작`으로 상호 전환되도록 구성할 것.
7. **이산적 TCP 버퍼 수신 버스트로 인한 다운로드 속도 요동(`0` ↔ `124 KB/s`) 방지 및 지수이동평균(EMA) 슬라이딩 윈도우 원칙**:
   - **실제 실수**: 다운로드 속도를 계산할 때 단순 500ms 구간 동안 유입된 바이트 수(`m_bytesSinceLastSpeedCalc`)를 측정한 직후 0으로 리셋함. WinHTTP TCP 소켓은 데이터를 버퍼 단위(예: 64KB)로 끊어서 전달하므로, 버퍼가 도착한 500ms 구간에는 124KB/s로 튀고, 버퍼를 기다리는 다음 500ms 구간에는 0바이트가 잡혀 `0 KB/s`가 표시됨. 결과적으로 UI 화면에 1초마다 `0`과 `124K`가 번갈아 깜빡거리며, 남은 시간(ETA)도 무한대와 특정 초를 오가는 심각한 표시 결함과 사용자 불만을 초래함. 또한 다중 워커 스레드가 비원자적 `m_bytesSinceLastSpeedCalc` 변수를 동시 가산하는 데이터 레이스 위험도 존재함.
   - **방지 원칙**:
     1. 단발성 구간 리셋 방식을 전면 배제하고, 원자적 누적 바이트(`m_downloadedBytes`)를 기반으로 하는 **2.0초 슬라이딩 타임 윈도우(Sliding Time Window)**를 구축할 것.
     2. 윈도우 내 평균 속도를 구한 뒤 **지수이동평균(EMA, $\alpha=0.4$)**을 적용하여 패킷 수신 간격(지터)이 발생하더라도 속도 표시가 0으로 급락하지 않고 안정적이고 부드러운 체감 속도를 유지할 것.
     3. 일시정지, 취소, 다운로드 완료 시에는 `SpeedTracker`를 즉시 리셋하여 잔여 속도 잔상이 남지 않도록 할 것.
     4. 네트워크 단절이나 장시간 정체(>= 1.5초) 시에는 즉시 0으로 뚝 떨어뜨리지 않고 점진적으로 감쇠(Decay)시켜 현실적인 네트워크 지연을 반영할 것.
8. **무료 파일 호스팅(Send, 1Fichier, Megaup 등) 단일 연결(Single Connection / parts=1) 강제 및 동적 분할 차단 원칙**:
   - **실제 문제/실수**: Send.now(`datatransfer.to` CDN), 1Fichier, Megaup 등 무료 파일 호스팅 서비스는 무료 사용자의 다중 IP 연결 및 멀티파트 청크 다운로드를 엄격히 제한(차단/429/503/강제 연결 종료)함. 그럼에도 불구하고 기본값 8개 분할(`actualParts = 8`) 및 적응형 가속(`adaptive = true`)으로 8~12개 연결을 시도하여, 다운로드가 시작되자마자 서버로부터 연결이 튕기거나 IP가 일시 차단되는 심각한 결함 유발.
   - **방지 원칙**:
     1. `HttpEngine::IsSingleConnectionHost` 헬퍼를 두어 URL 또는 원본 URL(OriginalUrl)이 `send.now`, `send.cm`, `datatransfer.to`, `1fichier.com`, `megaup.net`, `nswvault.com` 등에 해당하는지 통합 판별할 것.
     2. 대상 호스트 감지 시 `actualParts = 1`로 강제 고정하고, 적응형 가속(`adaptive = false`)을 원천 차단하여 단 1개의 연결 스트림으로 안전하게 파일 끝까지 수신할 것.
     3. `NewDownloadDialog` 및 `일괄 링크 추출기` 탭에서도 해당 호스트가 감지되면 자동으로 분할 수를 1로 강제 설정하고, 파트 간에는 `m_dependsOnId` 기반 1개씩 순차 다운로드를 결합하여 호스트 차단을 100% 방어할 것.

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
4. **유튜브 DASH 분리 스트림(Video+Audio) 진행률 단일 연속 게이지 매핑 및 프래그먼트 0번 오판 방지 원칙**:
   - 유튜브 DASH는 영상(Video Only)과 음성(Audio Only) 스트림이 개별 파일로 분리되어 전송된 후 FFmpeg로 조립됨.
   - 단순 파싱 시 비디오 100% 완료 후 오디오 수신 시 게이지가 0%로 리셋되어 "2번 다운로드" 혼선을 초래하므로, 비디오(0%~90%), 오디오(90%~98%), FFmpeg 조립(99%)으로 진행률을 선형 매핑하고 누적 용량을 연속 합산하여 게이지 역행(Regress) 없이 100%까지 매끄럽게 연결할 것.
   - **HLS/DASH 세그먼트 파싱 시 `frag 0/` 헤더 오판 방지**: yt-dlp가 HLS/쇼츠 다운로드 시 첫 0.1초 동안 712B짜리 초기화 헤더(`frag 0/N`)를 받고 `100.0%`를 출력할 때, 이를 전체 비디오 완료로 오판하여 게이지를 즉시 90%로 올리고 오디오 상태로 전환해 다운로드 시작부터 90%에 고정되는 치명적 버그를 원천 차단할 것 (`frag 0/` 라인은 진행률 계산에서 명시적 제외).
   - **오디오 스트림 판별은 퍼센트 급락 추정이 아닌 대상 파일 확장자로 엄격 판별**: `pct < 25%` 같은 임의의 수치 변화 추정을 배제하고, `Destination:` 라인의 확장자(`.m4a`, `.opus` 등)와 `Downloading 1 format(s): X+Y` 포맷을 통해 100% 확실하게 스트림 종류를 판별할 것.

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

### ⚠️ 인페이지 비디오 다운로드 트리거 버튼 폼 팩터 및 하드웨어 3D 금속 렌더링(Metallic Skeuomorphism) 원칙
- **실제 실수**: 인페이지 다운로드 버튼에 "동영상 다운로드"와 같은 긴 텍스트를 포함하여 바 형태로 렌더링하거나 플랫(Flat)한 원색으로 칠해 웹 플레이어 화면과 어색하게 겉돌고 시야를 가리는 답답한 레이아웃을 초래함.
- **방지 원칙**:
  1. 인페이지 다운로드 버튼은 불필요한 텍스트 라벨 없이 **30×30px 규격의 컴팩트한 정사각 📥 다운로드 아이콘 단독 버튼**으로 축소 배치하고, 상세 화질/용량 선택은 클릭 시 드롭다운 메뉴로 제공할 것.
  2. 단순 플랫 컬러 대신 **하드웨어 기계 가공 느낌의 3D 금속 질감(Skeuomorphic Metal)**을 적용할 것:
     - **다크 티타늄 (건메탈 3D)**: 깊이감 있는 흑연 스틸 그라디언트(`linear-gradient(145deg, #374151 0%, #1f2937 35%, #111827 75%, #0a0e17 100%)`) + 상단 하이라이트 림(`inset 0 1px 1px rgba(255, 255, 255, 0.4)`) + 하단 음영 림(`inset 0 -2px 3px rgba(0, 0, 0, 0.75)`).
     - **실버 크롬 (알루미늄 3D)**: 광택 알루미늄 그라디언트(`linear-gradient(145deg, #ffffff 0%, #e2e8f0 25%, #cbd5e1 55%, #94a3b8 85%, #64748b 100%)`) + 상단 반사 림(`inset 0 1.5px 1.5px #ffffff`) + 하단 금속 테두리(`inset 0 -2px 3px rgba(71, 85, 105, 0.65)`).
  3. **실시간 테마 동기화**: 브라우저 확장 팝업, 인라인 드롭다운 푸터, 데스크톱 환경설정창 어디에서든 테마를 변경하면 `chrome.storage.onChanged`를 통해 페이지 새로고침 없이 즉시 모든 탭의 비디오 버튼에 클래스가 실시간 교체 반영되도록 보장할 것.

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

### 🛡️ DPI 우회 엔진(WinDivert) 내부 이식 및 단일 바이너리 통합 원칙
- **배경**: 외부 실행 파일(`goodbyedpi.exe`)을 하위 프로세스로 실행하면 파일이 분리되어 배포가 번잡해지고 프로세스 생명주기 관리 및 재설치 파일 잠금 오류가 발생하기 쉬움.
- **방지 원칙**:
  1. **네이티브 인프로세스 스레드 통합 (외부 exe 완전 제거)**:
     - GoodbyeDPI의 핵심 패킷 프래그멘테이션 로직(TLS ClientHello 2바이트 분할, Host 헤더 변조, 위조 RST 패킷 드롭)을 `DpiBypassManager` 내부 C++ 작업 스레드로 100% 직접 구현하여 외부 `goodbyedpi.exe` 실행 파일을 완전히 제거하고 단일 프로그램(`easydownload.exe`)으로 통합할 것.
  2. **WinDivert 동적 로딩 및 안전한 생명주기**:
     - `WinDivert.dll`을 `LoadLibraryW` / `GetProcAddress`로 동적 바인딩하여 정적 라이브러리(.lib) 의존성을 없애고, 종료 시 `WinDivertClose()` 호출을 통해 즉각적인 수신 루프 탈출 및 드라이버 해제를 보장할 것.
  3. **관리자 권한 매니페스트 일원화**:
     - WinDivert 커널 드라이버 통신을 위해 `easydownload.exe`의 매니페스트 및 프로젝트 설정(`<UACExecutionLevel>RequireAdministrator</UACExecutionLevel>`)을 관리자 권한으로 통일하여 런타임 권한 충돌 및 2차 UAC 팝업을 원천 차단할 것.

### ⚠️ 재설치 시 커널 드라이버(`WinDivert64.sys`) 파일 잠금(오류 코드 32) 및 설치 중단 방지 원칙
- **실제 실수**: 기존 버전을 사용하던 사용자가 새 버전의 `EasyDownload_Setup.exe`를 설치할 때, 윈도우 커널이 이전 실행 세션의 `WinDivert1.4` 드라이버 서비스 핸들을 유지하고 있어 압축 해제 시 `CreateFileW(..., CREATE_ALWAYS)`가 `ERROR_SHARING_VIOLATION` (오류 코드 32)을 반환하며 설치가 실패함.
- **방지 원칙**:
  1. **인스톨러 압축 해제 전 드라이버 서비스 무조건 사전 정지(`stopDriverServices`)**: 실행 중인 프로세스 유무와 상관없이 설치 스레드 시작 직전에 `WinDivert1.4`, `WinDivert` 드라이버 서비스를 `OpenService` -> `ControlService(SERVICE_CONTROL_STOP)` -> `DeleteService`로 정리하고 `SERVICE_STOPPED` 전이까지 대기할 것.
  2. **동일 커널 드라이버 잠금 시 안전 재사용(Skip on Sharing Violation)**: `.sys` 드라이버나 `.dll` 파일 생성 시 오류 코드 32(공유 위반) 또는 5(액세스 거부)가 발생하더라도, 대상 위치에 이미 해당 파일이 온전히 존재한다면 전체 설치를 중단하지 않고 기존 드라이버 바이너리를 유지한 채 나머지 파일 설치를 정상 완결할 것.

### 🎬 HLS(M3U8) 비디오 다운로드 확장자 강제 교정(`.mp4`) 및 제네릭 스트림명(`index-f3-v1-a1`) 방지 원칙
- **실제 실수**: supjav.com 등 임베드 iframe 플레이어(`https://evsishere.xyz/.../index-f3-v1-a1.m3u8`)에서 영상을 다운로드했을 때:
  1. 브라우저 확장 및 데스크톱 엔진에서 파일명 확장자를 `.m3u8` 그대로 유지하여 최종 저장 파일이 `index-f3-v1-a1 (1).m3u8`이라는 재생 불가능한 플레이리스트 이름으로 저장됨 (실제 내용은 결합된 완전한 비디오임에도 확장자 오류로 일반 플레이어에서 바로 열리지 않음).
  2. iframe 내부의 `content.js`가 iframe 자체의 빈 제목이나 `웹 비디오`를 전송하고, `background.js`가 상위 탭의 제목(`tab.title`)을 상속해주지 않아 사이트 제목 대신 CDN의 내부 세그먼트 식별자(`index-f3-v1-a1`)가 파일명으로 박힘.
- **방지 원칙**:
  1. **M3U8 확장자 절대 보존 금지(`.mp4` 강제)**: HLS 스트림(`m_isHls`)은 청크 병합 시 단일 비디오 파일이 생성되므로, 브라우저 확장(`background.js`, `content.js`, `popup.js`) 및 C++ 엔진(`DownloadTask`, `HlsDownloader`, `MainWindow`, `DownloadManager`, `NewDownloadDialog`) 모든 경로에서 `.m3u8` 확장자를 즉시 제거하고 무조건 `.mp4`로 치환/강제할 것.
  2. **Iframe 미디어의 최상위 부모 탭 제목(`tab.title`) 자동 상속**: 서브프레임(iframe)은 자체 제목이 없거나 무의미하므로, 확장 프로그램 `background.js`에서 `videoDetected` 및 `downloadVideo` 수신 시 `sender.tab.id`를 통해 최상위 탭 제목(`cleanPageTitle(tab.title)`)을 우선 상속할 것.

### ⚠️ 비디오 제목 길이 제한(50자) 및 불필요한 URL 슬러그 덮어쓰기 원천 금지
- **실제 실수**:
  1. 확장 프로그램 다운로드 목록 UI에 정상 비디오 제목이 표시되어 있음에도 불구하고, 이전 패치에서 추가된 `ExtractTitleFromRefererUrl` 및 `extractSlugFromUrl`이 개입하여 사이트 도메인과 페이지 식별자를 조합한 임의의 슬러그(`supjav_469646`)로 제목을 덮어써버림.
  2. 비디오 제목 정돈 시 최대 길이 제한이 없어 불필요한 장문의 문구나 태그가 파일명 전체에 들어가 가독성을 떨어뜨림.
- **방지 원칙**:
  1. **UI 카드 타이틀 100% 보존**: 사용자가 다운로드 목록 UI 카드에서 확인하고 클릭한 제목(`titleDiv.textContent`)을 신성시하여, 데스크톱 코어(`WM_EXT_VIDEO`)나 백그라운드 스크립트에서 임의의 URL 슬러그(`supjav_469646`)로 절대 덮어쓰지 말 것. (URL 슬러그 추출은 제목이 완전히 비어있는 극단적인 경우에만 최후의 수단으로 사용할 것)
  2. **순수 기본 제목 최대 50자 제한 및 컴팩트 정리**:
     - 웹사이트 브랜딩 후미 접미사(` - SupJav`, ` | YouTube` 등)만 깔끔하게 제거하고, 화질 태그(`[1080p]`)를 분리한 순수 기본 제목의 길이를 최대 50자로 제한(`substring(0, 50).trim()`)할 것.
     - C++ `HttpEngine::SanitizeFilename(title, 50)` 및 JS `cleanPageTitle(title, 50)` 양쪽 모두에서 50자 절단을 보장하고, 최종 `[기본제목(최대50자)] + [화질태그] + [.mp4]` 규격으로 정갈하게 저장할 것.


### ⚠️ 다운로드 도중 동적 청크 재분할(`TryStealWork`) 및 스레드 무한 증설 금지 원칙 (프리징 및 중복 쓰기 방어)
- **실제 실수**: 
  1. 속도 향상을 위해 다운로드 진행 도중 2.5초마다 살아있는 청크의 `endByte`를 절반으로 쪼개고 새 청크를 등록하는 동적 Work-Stealing(`TryStealWork`) 및 워커 스레드 추가 증설 로직을 적용함.
  2. 이미 HTTP Range 요청으로 데이터를 수신 중인 기존 워커 스레드가 있는데 동일한 후반부 바이트 범위를 새 스레드가 중복 요청하여 동일 파일 오프셋에 동시 기록(`WriteFile`)하면서 파일 데이터 파손 및 서버 소켓 충돌/차단이 발생함.
  3. 에러 발생 시 죽은 스레드를 `join()`하지 않은 채 `m_workerThreads.emplace_back()`으로 새 스레드를 무한 증설하다가, UI 스레드에서 정지/취소/종료 시 `join()` 대기에 걸려 프로그램 전체가 완전히 뻗어버리는(Freezing) 치명적인 버그 유발.
  4. 또한 진행 대화상자 레이블이 사용자가 요구한 "동시 다운로드 개수" 대신 쪼개진 청크 개수인 `연결 분할: 10개`를 그대로 표시함.
- **방지 원칙**:
  1. **실행 중 동적 청크 분할(`TryStealWork`) 배제 및 고정 멀티스레드 분할 유지**: 다운로드 시작 시 대상 파일 크기에 맞춰 8개(설정값)의 정규 청크를 1회 분할한 뒤, 각 스레드가 전담하여 순차 소진하도록 아키텍처를 안정화할 것.
  2. **스레드 안전한 디스크 기록 (`m_fileWriteMutex`) 및 WinHTTP 소켓 타임아웃**:
     - 여러 워커 스레드가 단일 파일 핸들에 동시 `WriteFile`을 수행할 때 오프셋 충돌을 방지하기 위해 `std::mutex m_fileWriteMutex` 동기화 가드를 필수로 적용할 것.
     - `WinHttpSetTimeouts(hSession, 5000, 10000, 10000, 15000)`를 선언하여 서버 접속 지연 시 스레드가 영구 대기(Hang)하지 않고 15초 내에 안전하게 탈출하도록 보장할 것.
  3. **UI 표시 일원화 (`동시 다운로드: N개`)**:
     - 진행 대화상자에는 이미지(SegmentBar)로 이미 보이는 분할 개수 대신, 실제로 현재 작동 중인 워커 스레드 수(`m_activeWorkers`)를 반영하여 `동시 다운로드: N개` (연결 중, 일시정지, 완료 등)로 깔끔하게 표시할 것.


### ⚠️ 일반 다운로드 시 Referer 타이틀 추론(`ExtractTitleFromRefererUrl`) 오적용 및 단일 스트림 다운로드 즉시 종료 방지 원칙
- **실제 실수**:
  1. Chrome 확장 프로그램에서 일반 파일(ZIP, EXE, PDF 등)을 다운로드할 때 `onCreated` 시점에 파일명이 비어있는 채 전달되어 `MainWindow.cpp`의 fallback 로직인 `ExtractTitleFromRefererUrl`이 작동함. 이로 인해 사이트 도메인 뒤에 `_video`가 붙어 모든 일반 다운로드 파일명이 `sitename_video`로 둔갑하는 황당한 버그 유발.
  2. 로컬 브리지 서버(`LocalServer.cpp`)가 대용량 쿠키/헤더를 포함한 POST 요청을 단일 `recv()`로 읽어 JSON 바디가 잘려 URL이 누락되거나, 서버가 Range를 지원하지 않거나 파일 크기를 미리 알 수 없는 경우(`cp.endByte = -1`) 워커 스레드의 루프 조건(`currentOffset <= taskRange.endByte` -> `0 <= -1`이 거짓)으로 인해 단 1바이트도 받지 못하고 즉시 0바이트 완료 처리되는 치명적인 결함 발생.
- **방지 원칙**:
  1. **일반 파일 다운로드 시 비디오 타이틀 추론 배제**: `ExtractTitleFromRefererUrl`은 M3U8 또는 비디오 스트림(`isHls == true`)에만 엄격히 제한하고, 일반 파일 다운로드는 크롬이 확정한 파일명(`onDeterminingFilename`) 또는 URL 파일명, 서버 `Content-Disposition` 헤더를 최우선 적용할 것.
  2. **로컬 서버 `Content-Length` 기반 다중 TCP 패킷 완전 수신**: 로컬 브리지 서버는 `recv()` 1회 호출에 의존하지 말고, HTTP 헤더의 `Content-Length`를 파싱하여 전체 페이로드가 도착할 때까지 안전하게 누적 수신할 것.
  3. **단일 스트림/크기 미확정(`endByte < 0`) 다운로드 루프 무결성**: 서버가 Range를 지원하지 않거나 전송 인코딩(Chunked Transfer)으로 크기가 미정인 경우, `while (taskRange.endByte < 0 || currentOffset <= taskRange.endByte)`로 EOF까지 온전히 수신하고, 완료 후 `m_totalBytes = m_downloadedBytes`를 정상 동기화할 것.

### ⚠️ HLS 비디오 다운로드 8분할 구간 병렬화 및 실시간 동시 진행 렌더링 원칙
- **실제 실수**:
  - HLS 스트림 다운로드 시 워커 스레드 8개가 동시 다운로드를 수행하고 있었음에도, `DownloadTask.cpp`의 진행률 계산에서 `completedSegs / totalSegs` 전체 비율을 8개 블록에 곱해 순차적으로 인덱스를 매김 (`activeChunk = (int)(ratio * 8.0)`).
  - 이로 인해 8개 스레드가 병렬로 세그먼트를 받고 있는데도 SegmentBar 화면에서는 1번 블록이 100% 차오르고 난 뒤에야 2번 블록이 차오르는 기괴한 1개 블록 순차 진행 현상이 발생하여 사용자에게 심각한 혼란을 줌.
- **방지 원칙**:
  1. **재생목록 8분할 균등 슬라이스 배분**: `totalSegments`를 8개 워커 수에 맞춰 각 구간 `[w * N / 8, (w + 1) * N / 8)`으로 균등 분할하고, 각 워커가 자신의 전담 슬라이스를 우선 다운로드하도록 할 것.
  2. **동적 잔여 작업 스틸링(Work-Stealing)**: 자신의 슬라이스를 먼저 완수한 여유 워커는 아직 작업이 많이 남은 다른 슬라이스의 세그먼트를 실시간으로 인수하여 전체 다운로드 완료 시간을 극대화할 것.
  3. **청크별 실시간 병렬 콜백 렌더링**: 각 세그먼트 완료 시 해당 슬라이스 인덱스(`targetChunk`)의 진행률(`chunkDone / chunkTotal`)을 개별 업데이트하여, SegmentBar의 8개 블록이 화면상에서 동시에 함께 차오르는 완벽한 병렬 시각 피드백을 제공할 것.


### ⚠️ Chrome 확장 다운로드 인터셉트 시 onDeterminingFilename의 suggest() 누락 (60초 프리징) 및 디스크 사전 할당 FSCTL_SET_SPARSE 미지정 (30~60초 I/O 지연) 방지 원칙
- **실제 실수**:
  1. Chrome 확장 프로그램에서 `chrome.downloads.onDeterminingFilename` 리스너를 등록한 뒤 전달받은 `suggest()` 콜백 함수를 호출하지 않음. Chrome 브라우저 공식 규격상 `suggest()`가 호출되지 않으면 브라우저 다운로드 파이프라인 전체가 정확히 60초간 멈춰(Stall) 있다가 타임아웃 이후에야 풀림.
  2. 데스크톱 엔진(`DownloadTask.cpp`)에서 `SetFilePointerEx` + `SetEndOfFile`로 대용량 파일(수백 MB ~ 수 GB)의 크기를 사전 할당할 때, 대상 파일을 희소 파일(`FSCTL_SET_SPARSE`)로 지정하지 않아 Windows NTFS 드라이버가 전체 디스크 클러스터를 동기식으로 0(Zero)으로 채우는(Zero-Fill) 무거운 디스크 쓰기 I/O를 수행함 (30초 ~ 60초 이상 소요).
  3. 이 두 가지가 결합되어 사용자가 일반 파일 다운로드를 클릭했을 때 실제로 다운로드가 시작되기까지 총 1~2분의 극심한 대기 시간이 발생하는 치명적인 사용자 경험 저하를 유발함.
- **방지 원칙**:
  1. **onDeterminingFilename 리스너 내 suggest() 즉시 동기 호출**: `chrome.downloads.onDeterminingFilename.addListener((item, suggest) => { ... })` 진입 즉시 1행에서 `suggest({ filename: item.filename, conflictAction: "uniquify" })`를 동기식(0ms)으로 호출하여 Chrome의 60초 파이프라인 대기를 즉시 해제할 것.
  2. **디스크 사전 할당 시 DeviceIoControl(FSCTL_SET_SPARSE) 필수 선행**: 멀티파트 분할 다운로드를 위해 `SetEndOfFile`을 호출하기 직전에 반드시 `DeviceIoControl(hTargetFile, FSCTL_SET_SPARSE, ...)`를 호출하여 희소 파일로 지정할 것. 이를 통해 수 기가바이트 파일이라도 디스크를 0으로 채우는 낭비 없이 0.001초(즉각) 만에 파일 크기 할당을 완료해야 함.


### ⚠️ WinHttp Referer 중복 헤더 전송 금지 및 CDN 안티 핫링크(403 Forbidden) 대응 원칙
- **실제 실수**:
  1. `WinHttpOpenRequest`의 5번째 인자(`pwszReferrer`)에 `referer`를 전달하면서, 동시에 `WinHttpSendRequest`의 추가 헤더(`headers`)에도 `Referer: ...\r\n`을 함께 덧붙임.
  2. 이로 인해 WinHttp가 네트워크 패킷에 `Referer` 헤더를 2회 중복 전송함. Cloudflare WAF를 사용하는 비디오 스트리밍 CDN(`surrit.com` 등)은 중복 헤더를 비정상/공격 트래픽으로 간주하여 즉시 `403 Forbidden`을 반환하며 재생목록 및 세그먼트 수신을 전면 차단함.
  3. 또한 사용자가 CDN URL(`https://surrit.com/.../playlist.m3u8`)만 복사해 '새 다운로드'에 직접 입력하는 경우, `referer`가 비어있어 CDN의 도메인 검증을 통과하지 못하고 403 차단이 발생함.
- **방지 원칙**:
  1. **WinHttpOpenRequest pwszReferrer 중복 제거**: `WinHttpOpenRequest`의 5번째 인자는 무조건 `WINHTTP_NO_REFERER`로 지정하고, Referer는 `headers` 문자열(`Referer: ...\r\n`)을 통해서만 단 1회 정확하게 주입할 것.
  2. **HTTP/2 프로토콜 플래그 활성화**: Cloudflare/CDN WAF의 핑거프린팅 차단을 방지하기 위해 `WINHTTP_PROTOCOL_FLAG_HTTP2`를 기본 활성화할 것.
  3. **전용 비디오 CDN의 Origin Referer 자동 보정(Fallback)**: 사용자가 Referer 없이 전용 CDN URL(예: `surrit.com` 등)을 직접 입력한 경우에도, 엔진 레벨에서 원본 사이트 도메인(`https://missav123.com/`)을 `effectiveReferer`로 자동 보정하여 단 한 번의 실패도 없이 100% 정상 수신되도록 보장할 것.

### 🌐 '관리 브라우저(조직에서 관리함)' 환경에서의 보안 DNS (DoH) 우회 및 원클릭 제어 원칙
- **배경**: 인강 플레이어(EBS, 자격증 사이트 등)나 특정 보안 프로그램/확장 프로그램이 설치된 PC는 윈도우 레지스트리 정책(`SOFTWARE\Policies\Google\Chrome`)이 등록되어 Chrome이 스스로를 '조직에서 관리하는 브라우저'로 인식하고 브라우저 설정 내 [보안 DNS 사용] 옵션을 비활성화(회색 잠금) 처리함.
- **원인**: DPI 우회(포트 443 SNI 분할)가 켜져 있어도 도메인 질의(포트 53 DNS)가 통신사 검열 DNS로 전달되면 가짜 IP/타임아웃으로 인해 차단 사이트 접속 시 여러 번 새로고침을 반복해야만 열리는 치명적인 불편 유발.
- **방지 원칙**:
  1. **정책 레지스트리를 통한 DoH 강제 주입**: Chrome이 '관리 브라우저' 상태일 때 브라우저 UI로는 DoH를 켤 수 없으므로, 역으로 관리 정책 레지스트리(`HKCU` 및 `HKLM`의 `SOFTWARE\Policies\Google\Chrome`)에 `DnsOverHttpsMode="secure"`, `DnsOverHttpsTemplates="https://chrome.cloudflare-dns.com/dns-query"`를 등록하여 Chrome이 통신사 DNS를 거치지 않고 Cloudflare 1.1.1.1로 암호화 질의하도록 완벽 강제화할 것.
  2. **원클릭 복원 보장**: 사용자가 설정을 되돌리고 싶을 때 언제든지 동일 레지스트리 값을 원클릭으로 안전하게 삭제(`RegDeleteValueW`)하여 기본값 복원이 가능하도록 환경 설정 다이얼로그 및 메뉴에 상태 표시와 토글 액션을 제공할 것.

### 🛡️ DPI 패킷 분할(SNI Fragmentation)의 연속 전송 재조립 방어: 역순 전송(Out-of-Order Delivery) 및 1ms 미세 지연 원칙
- **실제 현상**:
  - `mrjav.net` 등 해외 단독 호스팅(고정 IP) 차단 사이트의 경우, 일반적인 TLS ClientHello 분할(step 0 ➡️ step 1)을 0ms 간격으로 연속 송출하면 국내 통신사 최신 DPI 검열 장비가 네트워크 패킷 큐에서 두 조각을 다시 결합(TCP Reassembly)하여 도메인을 읽어내고 즉시 위조 RST 패킷을 날려 연결을 차단(`Connection reset`)함.
  - 이로 인해 사용자가 5~10번 새로고침을 누르다가 우연히 회선 지연(Jitter)으로 패킷 간 간격이 벌어졌을 때만 어쩌다 접속되는 치명적인 연결 불안정이 발생함.
- **방지 원칙**:
  1. **역순 송출 (Out-of-Order Delivery)**: 2번째 뒷부분 조각(`SeqNum = Original + 2`)을 네트워크에 먼저 전송하고, 1번째 앞부분 조각(`SeqNum = Original`)을 뒤이어 송출할 것. 통신사 검열 장비는 역순으로 들어온 TCP 세그먼트를 재조립하지 못해 포기하지만, 목적지 웹서버(Linux/Nginx 등)는 표준 TCP Out-of-Order 버퍼를 통해 100% 정상 조립하여 첫 시도에 즉시 응답함.
  2. **1ms 미세 지연 (Micro-Sleep)**: 2번 조각과 1번 조각 사이에 `std::this_thread::sleep_for(1ms)` 지연을 두어 통신사 라우터의 버퍼 병합을 원천 무력화할 것.

### 🛡️ TLS 레코드 헤더 2바이트 분할의 치명적 맹점 방어: RFC 기반 SNI Host Split(도메인 중간 절단) 원칙
- **실제 실수/맹점**:
  - 기존 방식은 패킷 맨 앞의 TLS 레코드 헤더 2바이트(`0x16 0x03`)만 쪼개어 보냄 (`fragSize = 2`).
  - 그러나 실제 대상 도메인(`mrjav.net`)은 ClientHello 패킷 내부의 150~200바이트 부근(SNI 확장)에 위치함.
  - 이로 인해 2번 뒷조각 안에 도메인 이름 전체(`mrjav.net`)가 100% 온전하게 잔존하여, 통신사 검열 장비가 2번 조각을 열어보고 도메인을 온전히 읽어내어 즉시 가짜 RST 차단을 실행함. 사용자가 수십 번 클릭해야 우연히 뚫리는 치명적인 불완전 우회를 유발함.
- **방지 원칙**:
  1. **정밀 SNI 오프셋 파서 내장 (`FindSniOffset`)**:
     - ClientHello의 Session ID, Cipher Suites, Compression Methods를 정확히 건너뛰고 Extension Type `0x0000` (SNI) 내부의 실제 도메인 문자열 시작 바이트 오프셋을 바이트 단위로 정확히 추출할 것.
  2. **도메인 문자열 한가운데 절단 (`fragSize = sniOffset + 2`)**:
     - 레코드 헤더가 아닌 도메인의 2번째 글자 위치(예: `"mr"` 과 `"jav.net"` 사이)를 기준으로 패킷을 양분할 것.
     - 1번 조각: 도메인 앞 2글자만 포함 (`mr`)
     - 2번 조각: 도메인 나머지 글자부터 포함 (`jav.net`)
     - 양쪽 어느 조각에도 전체 도메인 완성형 문자열(`mrjav.net`)이 일절 존재하지 않으므로, 통신사 DPI 장비의 키워드 매칭 필터가 100% 원천 무력화됨.
  3. **조각 간 1ms 지연 및 순차 송출**:
     - 분할 조각 사이에 1ms 지연을 주어 라우터 레벨의 TCP 결합을 막고, 서버에는 정상 순서로 도달시켜 단 1밀리초의 타임아웃도 없이 즉각적인 첫 시도 `200 OK` 응답을 완벽 보장할 것.

### 🛡️ Chrome의 HTTP/3 QUIC (UDP 443) 우선 시도로 인한 첫 클릭 딜레이 방어: 아웃바운드 UDP 443 자동 차단(Drop) 원칙 (GoodbyeDPI -q 규격)
- **실제 현상**:
  - Chrome 브라우저는 최신 사이트에 접속할 때 기본적으로 HTTP/3 (QUIC / UDP 443) 프로토콜을 항상 먼저 시도함.
  - DPI 우회 엔진이 TCP 443 패킷만 가공하고 UDP 443을 방치할 경우, 암호화되지 않은 QUIC ClientHello 패킷이 통신사에 노출되어 첫 번째 시도가 먹통/타임아웃(수 초 지연)을 겪음.
  - 이로 인해 사용자가 1~2번 더 클릭하거나 새로고침을 해야 Chrome이 비로소 "UDP 접속 불가"를 인지하고 TCP 443으로 전환하여 뒤늦게 열리는 사용자 경험 저하를 유발함.
- **방지 원칙**:
  1. **WinDivert 필터에 UDP 443 포함**: `((outbound && !loopback && (tcp.DstPort == 80 || tcp.DstPort == 443 || udp.DstPort == 443)) || (inbound && !loopback && tcp.Rst))`
  2. **아웃바운드 UDP 443 즉시 폐기 (Drop)**:
     - `addr.Direction == WINDIVERT_DIRECTION_OUTBOUND && pUdpHdr && ntohs(pUdpHdr->DstPort) == 443` 조건 감지 시 패킷을 네트워크로 보내지 않고 즉시 드롭(`continue`).
     - 이를 통해 Chrome이 UDP 대기 타임아웃 없이 0밀리초(즉각) 만에 TCP 443(HTTPS)으로 전환하도록 강제하여, 첫 번째 클릭 한 번에 1초 만에 사이트가 즉시 로딩되도록 보장할 것.

### 🛡️ SNI 도메인 분할과 역순 전송(Reverse Sending)의 상호 결합 필수 원칙
- **실제 현상 및 실수**:
  - 도메인 중간을 잘랐더라도 순서대로(0 ➡️ 1ms 지연 ➡️ 1) 보내면, 통신사 감시 라우터가 1번 앞조각을 버퍼링했다가 뒤이어 오는 2번 뒷조각과 결합하여 도메인을 복원해버림.
  - 이로 인해 20번 이상 클릭해야 통신사 버퍼 타이머가 우연히 어긋나며 겨우 열리는 불완전 우회 현상 발생.
- **방지 원칙**:
  - **도메인 중간 절단(Host Split)과 역순 송출(2번 뒷조각 먼저 ➡️ 1ms 지연 ➡️ 1번 앞조각)을 반드시 결합할 것.**
  - 통신사는 2번 뒷조각을 먼저 받아 순서가 맞지 않아 조립을 완전히 포기하고, 1번 조각은 도메인의 앞 2글자만 들어있어 매칭되지 않음.
  - 목적지 웹서버는 Out-of-Order 큐를 통해 즉각 정상 조립하여 단 한 번의 클릭에 즉시 응답을 보장함.

### 🎬 HLS fMP4 / CMAF 규격의 `#EXT-X-MAP` 초기화 세그먼트(`ftyp`/`moov`) 누락으로 인한 재생 불가 방지 원칙
- **실제 현상 및 실수**:
  - `xhzone.life` 등 최신 HLS 스트리밍 사이트에서 다운로드한 비디오 파일이 팟플레이어, VLC, 윈도우 미디어 플레이어 등 어떤 플레이어에서도 재생되지 않고 `Invalid MP4 header / 손상된 포맷` 오류를 냄.
  - 최신 HLS v6 규격(fMP4/CMAF)은 `.ts`(MPEG-TS)가 아닌 `.m4s` 프래그먼트를 사용함.
  - 영상 해상도, AV1/HEVC/AAC 코덱 헤더, 트랙 정보(`ftyp`, `moov` 박스)는 M3U8 상단의 `#EXT-X-MAP:URI="init.mp4"`로 지정된 미디어 초기화 세그먼트에만 존재하며, 각 `.m4s` 세그먼트는 순수 프래그먼트 데이터(`moof`, `mdat`)만 담고 있음.
  - 기존 `HlsDownloader`가 `#EXT-X-MAP` 태그를 파싱하지 않고 오직 `.m4s` 조각들만 이어붙여, 생성된 `.mp4` 파일에 필수 파일 헤더가 누락되어 `moof`로 시작하는 비정상 파일이 생성됨.
- **방지 원칙**:
  1. **`#EXT-X-MAP:URI="..."` 필수 파싱**: M3U8 파서(`ParsePlaylist`)에서 `#EXT-X-MAP:` 라인을 감지하여 초기화 세그먼트의 상대 경로를 절대 URL(`ResolveUrl`)로 변환하고 `initSegmentUrl`에 반드시 저장할 것.
  2. **초기화 세그먼트 선행 수신 및 파일 최상단(오프셋 0번) 선두 결합**:
     - 워커 스레드가 `.m4s` 세그먼트를 받기 전, `initSegmentUrl`을 우선 다운로드(최대 3회 재시도).
     - 최종 파일 병합 단계에서 대상 타겟 파일의 0번 오프셋에 `init_segment.mp4`의 바이너리(`ftyp` + `moov`)를 먼저 기록한 뒤, 그 뒤로 `.m4s` 세그먼트들을 순차 이어붙일 것.
     - 이를 통해 다운로드 완료 즉시 모든 미디어 플레이어 및 브라우저에서 100% 오류 없이 매끄럽게 재생되는 완전한 ISO BMFF 표준 MP4 파일을 보장할 것.

### 🎬 YouTube / DASH 암호화 스트림(`googlevideo.com`) 다운로드 및 `yt-dlp` 자동 파이프라인 연동 원칙
- **실제 현상 및 원인**:
  - 유튜브(YouTube)는 M3U8이나 단순 직링크를 일절 제공하지 않고, 구글 전용 DASH 스트림을 사용함.
  - 비디오와 오디오 트랙이 완전히 분리되어 별도 전송되며, 자바스크립트 서명(Signature Cipher / `n` challenge)으로 보호되어 있어 일반 HTTP 요청 시 `watch.html` 웹페이지 코드만 다운로드되거나 403 차단이 발생함.
  - 구글 크롬 정책상 확장 프로그램의 유튜브 직접 스니핑이 차단되어 일반 M3U8 감지기로는 목록에 잡히지 않음.
- **방지 및 구현 원칙**:
  1. **`yt-dlp` 독립 바이너리 정식 내장 탑재**:
     - `tools/yt-dlp.exe`를 공식 포함하고, `.vcxproj`의 `PostBuildEvent` 및 EasyInstall 패키징 파이프라인에 포함시켜 배포 산출물에 기본 내장할 것.
  2. **유튜브 URL 감지 시 전용 스마트 파이프라인 자동 직결 (`DownloadTask`)**:
     - `youtube.com/watch`, `youtu.be/`, `youtube.com/shorts/` URL 수신 시, 일반 `QueryUrlInfo` 대신 `HlsDownloader::DownloadWithYtDlp`로 즉시 라우팅할 것.
     - 다운로드 전 `HlsDownloader::GetVideoTitleWithYtDlp(url)`로 실제 영상 제목을 획득하여 파일명을 자동 설정할 것.
     - `-f "bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/best" --merge-output-format mp4` 옵션을 강제하여 최고화질(1080p 60fps, 4K 등) 영상과 고음질 음향이 결합된 단일 `.mp4` 파일로 무손실 병합할 것.
  3. **브라우저 확장 프로그램 유튜브 탭 자동 연동 (`popup.js`, `content.js`)**:
     - 사용자가 유튜브 시청 중 확장 팝업을 열거나 페이지 내에서 `YouTube 최고 화질 (1080p/4K)` 항목을 감지하여 원클릭으로 EasyDownload에 다운로드를 큐잉할 수 있도록 할 것.

### ⚠️ yt-dlp 타이틀 추출 시 stderr 파이프 오염 및 0 B / 100 B 진행률 멈춤 착각 버그 방지
- **실제 실수 및 원인**:
  1. **타이틀 추출 시 stderr 경고 오염**:
     - `HlsDownloader::GetVideoTitleWithYtDlp`에서 `si.hStdOutput`과 `si.hStdError`를 동일 파이프에 연결하여 실행함.
     - 최신 yt-dlp가 첫 줄에 출력한 `WARNING: [youtube] No supported JavaScript runtime could be found...`를 영상 제목으로 잘못 인식하여 파일명이 깨지거나 비정상적인 경로 문제로 다운로드가 즉시 실패함.
  2. **가짜 크기/속도로 인한 다운로드 멈춤 착각**:
     - `DownloadTask`에서 유튜브 작업 시 `m_totalBytes = 100`, `m_downloadedBytes = percent`로 하드코딩하고 속도를 `0 B/s`로 방치함.
     - 화면에 `크기: 0 B / 100 B (0.0%)`, `속도: 0 B/s`로 고정 표시되어 사용자가 "다운로드가 전혀 안 되고 멈췄다"고 극도로 오해하게 만듦.
  3. **FFmpeg 경로 누락**:
     - 최고 화질 포맷 병합(`--merge-output-format mp4`) 시 시스템 또는 Chocolatey에 설치된 `ffmpeg.exe` 경로를 명시하지 않아 병합 단계에서 멈추거나 대기함.
- **방지 원칙**:
  1. **stdout/stderr 완전 분리 및 순수 타이틀 획득**:
     - `cmd`에 `--no-warnings --print title` 옵션을 적용하고, `si.hStdError = INVALID_HANDLE_VALUE`로 설정하여 stderr 경고가 파이프에 일절 섞이지 않도록 차단할 것.
     - 읽어들인 제목에 `WARNING:`이나 `ERROR:`가 포함된 경우 즉시 필터링(Fallback)할 것.
  2. **FFmpeg 자동 탐지 및 `--ffmpeg-location` 전달**:
     - 실행 파일 경로, `tools` 폴더, Chocolatey 기본 설치 경로(`C:\ProgramData\chocolatey\bin`), 시스템 PATH에서 `ffmpeg.exe`를 자동 탐지(`FindFfmpegLocation`)하여 옵션으로 연동할 것.
  3. **실제 다운로드 크기 및 전송 속도 정밀 파싱**:
     - yt-dlp 표준 출력의 `[download] X.X% of X.XXMiB at X.XXMiB/s` 라인에서 전체 용량, 현재 다운로드량, 실시간 속도(bps)를 `ParseByteSizeHelper`로 정확히 환산하여 `m_downloadedBytes`, `m_totalBytes`, `m_currentSpeed`에 실시간 반영할 것.
     - 화면에 `크기: 15.2 MB / 128.5 MB (11.8%)`, `속도: 5.42 MB/s`, `남은 시간: 21초`가 정확하게 표시되도록 보장할 것.

### ⚡ yt-dlp 프로그램 구동 시 백그라운드 자동 업데이트 원칙
- **철칙**:
  - 유튜브는 스트리밍 추출 로직을 수시로 변경하므로 yt-dlp 버전이 구버전일 경우 다운로드가 차단될 수 있음.
  - 메인 프로그램 시작 시(`MainWindow::Create`) 분리된 백그라운드 스레드(`HlsDownloader::UpdateYtDlpInBackground`)를 통해 `yt-dlp.exe -U --no-warnings`를 조용히 실행(`SW_HIDE`, `CREATE_NO_WINDOW`)하여, 메인 UI 멈춤 없이 항상 최신 바이너리를 유지하도록 할 것.

### 🧹 메뉴 UI 간소화 및 환경설정 일원화 원칙
- 메인 메뉴바의 "도구(&T)" 메뉴는 불필요한 토글 항목(DPI 우회 활성화, Chrome 보안 DNS 설정)을 지우고 "크롬 확장 프로그램 설치 / 연동"과 "환경 설정..."만 깔끔하게 유지할 것.
- 모든 네트워크 및 우회 설정은 "환경 설정(SettingsDialog)"에서 일원화하여 관리하도록 유도할 것.

### 🗂️ 프로젝트 루트 디렉터리 무결성 유지 (테스트 파일 격리)
- 임시 테스트 코드(`test_*.cpp`, `dump_*.cpp`), 테스트 바이너리(`*.exe`), 잔여 오브젝트 파일(`*.obj`)은 절대 프로젝트 루트에 방치하지 말고 반드시 전용 임시 디렉터리(`tests/temp_scripts/` 등)로 격리하여 프로젝트 루트를 정갈하게 유지할 것.

### 🔤 Windows CLI 파이프 입출력 시 한글 깨짐(모지바케) 방지 (`--encoding utf-8`)
- **실제 실수 및 원인**:
  - `yt-dlp` 등 Python 기반 외부 CLI 도구를 C++ `CreateProcess` 파이프(`hStdOutput`)로 실행할 때, Windows 환경에서는 표준 출력이 기본적으로 시스템 OEM 코드페이지(CP949)로 출력되거나 뒤섞여 출력됨.
  - C++ 코드가 이를 `MultiByteToWideChar(CP_UTF8, ...)`로 디코딩하면서 한글 문자열이 모지바케(`BTS (źҳ) 'FAKE LOVE'`)로 깨져 비정상 파일명이 생성됨.
- **방지 원칙**:
  - 외부 도구 실행 인자에 **`--encoding utf-8`**을 필수로 명시하여 Windows 파이프에서도 표준 출력이 100% 온전한 UTF-8로 전송되도록 강제할 것.
  - 웹/브라우저 연동 JSON 파서(`ExtractJsonString`)에서도 `\uXXXX` 유니코드 이스케이프 시퀀스를 `wchar_t` 코드포인트로 완벽 디코딩 지원할 것.

### 🚫 크롬 확장 프로그램 스트림 스니퍼의 유튜브/구글비디오 청크 폭주(`videoplayback`) 배제 원칙
- **실제 현상 및 원인**:
  - `background.js`의 `onResponseStarted` 리스너에서 `url.includes("videoplayback")`을 비디오 스트림으로 감지함.
  - 유튜브 재생 중 브라우저가 수초마다 수백 번씩 요청하는 재생 버퍼 청크 조각(`googlevideo.com/videoplayback?range=...`)들이 개별 비디오로 오인되어 배지 카운트와 목록에 끊임없이 누적됨 (1, 2, 3... 50, 60 폭증).
  - 이 조각들은 단편 세그먼트이거나 토큰 만료로 다운로드도 되지 않는 무의미한 쓰레기 스트림임.
- **방지 원칙**:
  - `googlevideo.com`, `youtube.com`, `videoplayback`, `range=`, `sq=` 및 미디어 세그먼트 조각(`.ts`, `.m4s`) 요청은 백그라운드 스니퍼에서 **즉시 무시(`return`)**할 것.
  - 유튜브는 오직 탭 감지("YouTube 최고 화질 (1080p/4K)") 1개로만 단정하게 표시하여, 아무리 오래 켜두어도 감지 카운트가 폭증하지 않도록 보장할 것.

### 🏷️ 프로그램 윈도우 타이틀 간소화 원칙
- 메인 윈도우 제목 및 트레이 팁에 번잡한 부제("고속 분할 다운로드 가속기")를 중복 기재하지 않고, 공식 프로그램명인 **`EasyDownload`**로만 깔끔하고 단정하게 유지할 것.

### 🔄 유튜브/SPA 비디오 전환 시 화면 내 다운로드 주소 미갱신(캐시 잔존) 방지 원칙
- **실제 현상 및 원인**:
  - 유튜브는 다른 클립을 클릭할 때 페이지 전체를 새로고침하지 않고 자바스크립트(`pushState`, `yt-navigate-finish`)로 비디오 플레이어와 URL만 비동기 교체(SPA)함.
  - 브라우저 상단 확장 메뉴(팝업)는 열릴 때마다 브라우저 탭의 현재 주소(`activeTab.url`)를 직접 읽어오므로 갱신되었으나, 웹페이지 내에 삽입된 다운로드 아이콘(`content.js`)은 처음에 열었던 영상의 URL을 로컬 캐시(`detectedStreams[0]`)에 그대로 유지하고 있었음.
  - 다른 클립으로 이동해도 이전 영상 목록을 비우지 않아, 다운로드 아이콘을 눌렀을 때 이전 클립의 주소가 계속 다운로드 대상으로 전송되며 새로고침(F5)을 눌러야만 갱신되는 치명적인 사용자 경험 저하를 유발함.
- **방지 원칙**:
  1. **SPA 네비게이션 실시간 다중 감지 (이벤트 + 500ms 폴링)**:
     - `yt-navigate-finish`, `yt-page-data-updated`, `popstate` 이벤트와 함께 0.5초 주기 URL 폴링(`location.href !== lastYouTubeUrl`)을 상시 가동하여 비동기 클립 전환을 100% 포착할 것.
  2. **새 클립 전환 시 이전 영상 캐시 완전 리셋**:
     - 클립 변경 감지 즉시 `detectedStreams = []`로 이전 영상 주소를 완전히 비우고, 현재 활성 비디오의 `location.href`와 제목으로만 단독 재등록할 것.
     - 백그라운드(`background.js`)에도 `resetTabVideos` 메시지를 보내 탭 스트림 캐시를 상호 동기화할 것.
  3. **다운로드 아이콘 클릭(`openDropdown`) 시 실시간 URL 강제 동기화**:
     - 영상 위의 다운로드 아이콘을 클릭하는 순간, 현재 브라우저 주소(`location.href`)와 캐시된 URL을 즉시 대조하여 현재 재생 중인 클립의 최신 주소로 강제 갱신 후 다운로드 메뉴를 렌더링할 것.

### 🛡️ 관리자 권한(`requireAdministrator`) 프로그램의 Windows 자동 시작 차단 방지 원칙 (작업 스케줄러 필수)
- **실제 현상 및 원인**:
  - `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 레지스트리에 정상 등록되어 있음에도 불구하고, PC 재부팅(로그온) 시 프로그램이 전혀 실행되지 않음.
  - 원인: EasyDownload 등 커널 패킷 필터링 드라이버(WinDivert)를 사용하는 앱은 매니페스트에 **`requireAdministrator`**가 필수임. 그러나 Windows 보안 정책(UAC)상 일반 `Run` 레지스트리는 로그인 시 사용자 승인(UAC) 창을 띄울 수 없으므로, 관리자 권한이 선언된 바이너리의 실행을 100% 조용히 차단(무시)함.
- **방지 원칙**:
  1. **Windows 작업 스케줄러(`schtasks.exe`) 최고 권한 등록 필수**:
     - 관리자 권한 앱은 레지스트리 `Run` 키에만 의존하지 말고, 반드시 **작업 스케줄러에 최고 권한(`highest`) 및 로그온 시(`onlogon`) 옵션으로 등록**해야 함:
       `schtasks /create /tn "<프로그램명>AutoStart" /tr "\"<실행파일경로>\" --minimized" /sc onlogon /rl highest /f`
     - 이를 통해 부팅/로그온 시 UAC 팝업 없이 최고 권한으로 조용히 백그라운드/트레이 자동 시작을 완벽 보장할 것.
  2. **자동 시작 해제 시 양방향 삭제**:
     - `schtasks /delete /tn "<프로그램명>AutoStart" /f` 및 레지스트리 `Run` 키 동시 삭제.
  3. **앱 구동 시 자체 자동 보정**:
     - 프로그램 시작 시(`MainWindow::Create` / `Settings::SetAutoStartWithWindows`) 관리자 권한을 가진 메인 프로세스가 스케줄러 등록 상태를 검사하고 미등록 시 자체적으로 스케줄러 작업을 자동 생성하도록 구성할 것.### ⚠️ YouTube HLS/DASH 프래그먼트 파싱 시 0번 헤더(frag 0/N 100%) 오판 방지 및 진행률 왜곡 차단
- **실제 현상 및 실수**:
  - YouTube 쇼츠나 고화질 스트림 다운로드 시 yt-dlp가 첫 0.1초 동안 712바이트짜리 미디어 초기화 헤더(`[download] 100.0% of ~ 712.00B (frag 0/11)`)를 출력함.
  - 파서가 이 `100.0%`를 보고 비디오 전체가 끝난 것으로 오판하여 게이지를 즉시 90%로 올려버렸고, 다음 프래그먼트(`frag 1/11`)에서 수치가 `4.5%`로 급락하자 `lastRawPct >= 80 && pct < 25%` 조건에 걸려 오디오 스트림으로 영구 전환됨.
  - 이로 인해 다운로드가 시작되자마자 진행 창이 90%에 고정되어 끝날 때까지 90%에서만 머무는 치명적인 UI 버그 발생.
- **방지 원칙**:
  1. **초기화 프래그먼트(`frag 0/`) 명시적 무시**: `line.find("frag 0/") != std::string::npos`인 라인은 미디어 헤더이므로 진행률 및 누적 용량 계산에서 완전히 제외하여 0%부터 정상 출발하도록 보장할 것.
  2. **오디오/비디오 스트림 판별은 수치 급락 추정이 아닌 대상 파일 확장자로 엄격 판별**: `Destination:` 라인의 확장자(`.m4a`, `.opus` 등)를 통해 100% 명확히 판별하고, 임의의 퍼센트 하강 감지 로직을 절대 사용하지 말 것.
  3. **HLS 예상 용량 물결표(`~`) 스킵**: `of ~ 23.61MiB`에서 `~` 문자를 스킵하여 HLS 전체 용량 파싱 실패(0바이트 반환)를 방지할 것.

### ⚠️ UI 용어 충돌 및 불필요한 레거시 설정으로 인한 사용자 혼선 방지 원칙 (동시 다운로드 vs 분할 가속 연결)
- **실제 현상 및 실수**:
  - 엔진 설정(Tab 2)에 실제 작동하지 않는 레거시 항목인 `동시 다운로드 파일 수 제한: 3`이 그대로 남아 있었고, 진행 창(`DownloadProgressDialog`)에서도 청크 연결 수를 `동시 다운로드: N개`로 표기함.
  - 사용자는 "최대 12개까지 동적으로 가속된다더니 왜 설정에는 3개라고 적혀 있지?"라며 동적 스마트 가속 기능과 설정 항목 간에 심각한 혼선과 불신을 겪음.
- **방지 원칙**:
  1. 실제 기능과 부합하지 않거나 작동하지 않는 레거시 설정 항목은 UI에 남겨두지 말고 즉시 완전히 제거할 것.
  2. '동시 실행 파일 수'와 '단일 파일의 분할 청크 연결 수'는 용어가 겹치지 않도록 **`분할 연결: N개 (최대 12개)`**로 명확하게 차별화 표기할 것.

### ⚠️ 적응형 연결 증설 시 새 연결의 유효 수신 검증 및 서버 동시 접속 차단 감지 원칙
- **실제 현상 및 원인**:
  - 서버가 IP당 동시 접속을 1~2개로 제한하는 환경에서, 3번째 연결 시도 시 서버가 `HTTP 429 / 403 / 503` 또는 소켓 리셋으로 거부하여 새 연결이 0바이트 상태로 실패함.
  - 엔진이 이 속도 미증가(정체)를 "현재 속도가 부족하니 연결을 더 늘려야 한다"고 잘못 해석하여, 실제 다운로드를 못 받는데도 12개까지 무의미하게 실패 연결을 계속 쪼개며 증설하는 치명적 피드백 루프 발생.
- **방지 원칙**:
  1. **절대 속도(+300KB/s) 하한선 배제 및 새 워커의 실제 수신 여부(`bytesRead > 0`) 검증**:
     - 10KB/s~50KB/s로 연결당 속도가 제한된 저속/해외 서버에서는 12개 다중 연결 증설이 필수적이므로 임의의 절대 속도 증가 기준을 두지 말 것.
     - 오직 **새로 투입된 연결이 실제로 데이터를 1바이트라도 정상 수신하고 있는지**를 평가하여, 10KB/s라도 정상 수신 중이면 12개까지 거침없이 가속을 허용할 것.
  2. **서버 동시 접속 제한(`HTTP 429/403/503`) 또는 0바이트 수신 시 즉시 증설 영구 중단**:
     - 새 워커가 평가 기간(4초) 동안 데이터를 전혀 받지 못하거나 서버 차단 코드를 수신하면, 서버의 동시 접속 한계에 도달한 것으로 확정하고 **추가 연결 증설 시도를 즉시 영구 중단(`splitHalted = true`)**할 것.
  3. **청크 동적 분할 시 기존 활성 워커의 실시간 종료 지점 동기화 (`DynamicEndByte`)**:
     - 청크를 반으로 쪼갤 때 기존 워커의 종료 오프셋을 `std::atomic<int64_t>`로 실시간 갱신하여, 기존 워커가 `splitPoint - 1`에서 즉시 다운로드를 멈추고 새 워커에게 뒷부분을 넘겨주도록 하여 중복 데이터 수신 및 서버 연결 충돌을 원천 차단할 것.

### 🌐 통신사 DNS 오염(DNS Poisoning) 우회 및 GoodbyeDPI -5 규격 완벽 일치 원칙
- **실제 현상 및 원인**:
  - `mrjav.net` 등 KCSC 차단 사이트 접속 시, 오리지널 GoodbyeDPI(`-5 --dns-addr 77.88.8.8 --dns-port 1253`)는 첫 클릭 시 0초 만에 바로 열리는 반면, EasyDownload의 DPI 우회 기능은 사이트가 바로 열리지 않고 수차례 새로고침(F5)을 해야만 열리는 심각한 성능 격차가 발생함.
  - **원인 분석**:
    1. **UDP 53 DNS 리다이렉션 누락**: 국내 통신사(KT/SK/LG)는 차단 사이트에 대해 UDP 53 DNS 질의를 가로채어 가짜 IP를 반환하거나 응답을 유실시킴. GoodbyeDPI는 WinDivert로 UDP 53 패킷을 가로채어 비표준 포트인 `77.88.8.8:1253`으로 전송하여 감시를 100% 회피함. 반면 EasyDownload는 UDP 53을 처리하지 않고 크롬 DoH 레지스트리에만 의존하여 윈도우 기본 DNS가 오염된 채 방치됨.
    2. **Windows DNS 캐시 플러시 부재**: 프로그램 시작 전 접속 시도로 인해 PC의 Windows DNS 리졸버에 저장된 오염/실패 캐시를 비우지 않아, TTL이 만료될 때까지 F5를 반복해야 했음.
    3. **비표준 분할 오프셋 및 1ms 지연**: 도메인 중간(`sniOffset + 2`) 분할 및 `sleep_for(1ms)`로 인해 DPI 장비가 TLS 헤더를 포착하거나 패킷 루프에 지연이 발생함.
- **방지 원칙**:
  1. **WinDivert 기반 UDP 53 DNS 양방향 리다이렉션 필수 구현**:
     - WinDivert 필터에 `(outbound && udp.DstPort == 53)` 및 `(inbound && udp.SrcPort == 1253)`를 반드시 포함할 것.
     - 아웃바운드 UDP 53 질의 인입 시 클라이언트 주소/포트를 키로 원본 DNS 목적지를 트래커에 기록하고, 패킷의 목적지를 안전 비표준 DNS(`77.88.8.8:1253` / IPv6 `2a02:6b8::feed:0ff:1253`)로 변조 후 체크섬을 재계산하여 전송할 것.
     - 인바운드 UDP 1253 응답 인입 시 트래커를 조회하여 소스 IP를 원래의 DNS 주소로, 소스 포트를 53으로 완벽 복원하여 체크섬 재계산 후 윈도우에 전달할 것.
  2. **DPI 엔진 기동 시 Windows DNS 캐시 즉시 플러시 (`DnsFlushResolverCache`)**:
     - `DpiBypassManager::Start()` 호출 즉시 `dnsapi.dll`의 `DnsFlushResolverCache()`를 호출하여 PC에 남아있던 오염된 DNS 캐시를 0초 만에 완전히 날려버릴 것.
  3. **GoodbyeDPI -5 표준 규격 준수 (TLS 2바이트 역순 분할 & 0ms 지연)**:
     - TLS ClientHello 분할 시 고정 2바이트(`fragSize = 2`)로 앞부분(`0x16 0x03`)과 뒷부분을 분할하고, 뒷부분 조각을 먼저 전송(Reverse Fragmentation)하여 통신사 DPI의 TLS 핸드셰이크 인식을 원천 무력화할 것.
     - 패킷 송출 사이에 임의의 `sleep_for(1ms)`를 일절 두지 말고 즉시 연속 전송하여 레이턴시를 0ms로 유지할 것.

### 🛑 DPI 패킷 엔진의 '자체 반쪽짜리 재구현' 절대 금지 및 GoodbyeDPI 원본 C 코어 네이티브 인프로세스(In-Process) 탑재 원칙
- **실제 실수 및 안일한 태도**:
  - 공식 소스코드가 완벽하게 검증되어 있는 GoodbyeDPI의 핵심 엔진을 프로젝트에 네이티브로 직접 이식하지 않고, 복잡한 커널 패킷 조작 로직(WinDivert 필터 분리, `WINDIVERT_FLAG_DROP` 전용 핸들 분리, Inbound SYN+ACK TTL 추적 및 삭제 타이밍, `max-payload` 파싱 필터 등)을 자체 C++ 루프로 어설프게 흉내 내어 구현함.
  - 그 결과 최초 접속만 성공하고 후속 연결에서는 커널 소켓 리셋(`curl: (35) Recv failure: Connection was reset`)이 반복 발생하여 사용자가 브라우저를 수차례 새로고침해야만 사이트가 열리는 치명적인 결함과 사용자 불만을 유발함.
  - 사용자가 "goodbye dpi 코드가 있는거 아니야? 코드 똑같이 복사한거 아니야?", "실행 파일을 그대로 가져온다는 말이야? 소스를 가져오는게 아니고? 왜 자꾸 이래?"라고 엄중히 질타한 뒤에야 공식 C 소스를 이식하는 수동적인 태도를 보임.
- **방지 원칙**:
  1. **검증된 고도화 커널 엔진의 원본 C 소스 직접 통합(In-Process Native Integration)**:
     - WinDivert 기반 패킷 조작, DPI 차단 우회 등 미세한 프로토콜 타이밍과 상태 관리가 생명인 영역은 어설픈 자체 재구현을 철저히 배제하고, 공식 검증된 원본 C 소스코드(`goodbyedpi_orig.c`, `ttltrack.c`, `fakepackets.c`, `dnsredir.c`, `blackwhitelist.c` 등)를 그대로 프로젝트 소스 트리에 편입하여 컴파일할 것.
  2. **외부 보조 프로세스 스폰 배제 및 스레드 래퍼(`goodbyedpi_runner.c`) 구축**:
     - 외부 `goodbyedpi.exe`를 보조 프로세스로 띄우는 꼼수를 쓰지 않고, 원본 C 소스를 인프로세스 워커 스레드용 래퍼 함수(`RunGoodbyeDPI(argc, argv, &m_stopFlag)`)로 변환하여 메인 바이너리(`easydownload.exe`) 내에서 네이티브로 단일 실행되도록 할 것.
     - 종료 시에는 `StopGoodbyeDPI()`를 통해 모든 활성 WinDivert 핸들에 `WinDivertShutdown(h, WINDIVERT_SHUTDOWN_BOTH)`을 호출하여, 블로킹되어 있던 `WinDivertRecv`를 0초 만에 깨우고 스레드를 안전하게 조인(`join()`)시킬 것.
  3. **MSVC 빌드 환경 완결성 보장**:
     - MSVC에서는 `<winsock2.h>`를 `<windows.h>`보다 무조건 먼저 인클루드하여 Windows SDK 충돌을 방어할 것.
     - POSIX 종속적인 `<unistd.h>`, `<getopt.h>`는 프로젝트 내 경량 `getopt_long` 구현(`utils/getopt.c`)으로 대체하고, `/sdl` 검사를 통과하도록 로컬 포인터(`host_addr = NULL` 등)를 철저히 초기화할 것.
     - `_strdup` 사용 및 `blackwhitelist.c` 내 `_CRT_SECURE_NO_WARNINGS` 선언으로 MSVC C4996 경고를 원천 차단하여 **경고 0개, 오류 0개**의 무결점 네이티브 바이너리를 완성할 것.





### 🎬 위장된 HLS 스트림(.txt 플레이리스트, .png 세그먼트) 및 동적 플레이어(XOR 암호화) 자동 추출/다운로드 원칙
- **실제 현상 및 실수**:
  - `sextb.net` 등 일부 스트리밍 사이트는 광고 차단 및 비디오 스니퍼를 회피하기 위해 다음과 같은 고도화된 위장 기법을 사용함:
    1. 메인 페이지에서 iframe(`/e/<slug>`)을 로드하고, 내부에서 `/ajax/player` 호출 후 응답(`player_enc`)을 클라이언트 단에서 Base64 디코딩 및 XOR 복호화(`pk` 키)하여 3rd-party 플레이어(`f4scdn.com`, `hglink.to`, `streamtape.net` 등)를 동적 주입함.
    2. 플레이어는 `/api/play/<id>?r=...` 엔드포인트를 호출하여 HLS 플레이리스트를 받는데, URL 확장자를 `.m3u8` 대신 `.txt`(`playlist.txt`, `index.txt`, `master.txt`)로 속이고 `Content-Type: text/plain`으로 반환함.
    3. HLS 세그먼트 청크들 역시 확장자를 `.png`(`seg_00000.png`)로 위장함 (실제 데이터는 `0x47` 싱크 바이트로 시작하는 순수 MPEG-TS 스트림).
    4. CDN 서버는 `Referer` 헤더(`https://f4scdn.com/` 또는 `https://sextb.net/`)가 없으면 `403 Forbidden`을 반환함.
  - 기존 확장 프로그램과 데스크톱 앱은 오직 `.m3u8` 확장자나 `application/x-mpegurl` MIME 타입만을 HLS 스트림으로 간주하여 감지 및 다운로드가 전혀 동작하지 않음.
- **방지 원칙**:
  1. **확장 프로그램 스니퍼의 위장 플레이리스트 및 플레이어 API 감지 확장**:
     - `background.js`의 `webRequest` 감시 대상에 `/api/play/`, `/player/api/` 및 `.txt` 기반 플레이리스트(`playlist.txt`, `index.txt`, `master.txt`)를 포함할 것.
     - 본문 분석 시 `#EXTM3U` 및 `#EXT-X-STREAM-INF` 또는 `#EXTINF:`가 포함된 경우 Content-Type과 확장자에 상관없이 HLS 스트림으로 정확히 인식할 것.
     - 캡처된 스트림 정보에 `Referer`와 `Origin`을 반드시 바인딩하여 데스크톱 앱에 전달할 것.
  2. **데스크톱 네이티브 Extractor (`SextbExtractor`) 내장**:
     - 사용자가 브라우저 확장 없이 데스크톱 앱의 'URL 추가' 창에 웹페이지 주소(`https://sextb.net/...`)를 직접 입력하더라도, C++ 네이티브 `SextbExtractor`가 자동으로 `/e/<slug>` 파싱 ➡️ `/ajax/player` 호출 ➡️ XOR 복호화 ➡️ 플레이어 iframe 추출을 원스톱으로 수행할 것.
     - 특히 최신 `hglink.to` / `hgcloud.to` 플레이어로 전환된 경우, 목적지 플레이어인 `hanerix.com` 등으로 전환하여 Dean Edwards 압축 패킹 JS(`eval(function(p,a,c,k,e,d)...)`)를 C++ 네이티브 Base36/62 토큰 역치환 알고리즘으로 직접 언패킹한 뒤, `master.m3u8` 스트림 URL과 영상 제목을 0.3초 만에 무광고 자동 추출하도록 보장할 것.
  3. **HLS 다운로더의 위장 세그먼트(.png) 및 Referer 무결성 보장**:
     - 위장 플레이리스트 URL(`playlist.txt` 등)이 주입되더라도 `m_isHls = true`로 강제 지정하고, 세그먼트 확장자가 `.png`이더라도 MPEG-TS 바이너리 그대로 병합하도록 구성할 것.
     - WinHTTP 다운로드 요청 시 `Referer` 헤더를 반드시 동봉하여 CDN 403 차단을 원천 방어할 것.


### 📺 동영상 스트림 감지 시 플레이타임(재생 시간) 및 영상 크기(해상도) 실시간 보강 및 UI 렌더링 패턴
- **배경 및 원칙**:
  - 웹 비디오 스니퍼(`background.js`)는 네트워크 패킷(M3U8 플레이리스트 헤더 등)만으로는 정확한 영상 길이(초)나 비디오 태그의 렌더링 해상도를 온전히 파악하기 어렵거나 지연될 수 있음.
  - **HLS 스트림 재생 시간 산출**:
    - `#EXTINF` 세그먼트 항목의 부동소수점 길이 값을 합산(`totalDuration`)하여 초 단위 총 재생 시간을 계산하고, `formatDuration`(시:분:초 / 분:초)으로 변환하여 메타데이터에 보존할 것.
  - **DOM 비디오 엘리먼트 실시간 보강 (Enrichment)**:
    - 웹페이지 내 `<video>` 태그의 `duration`, `videoWidth`, `videoHeight` 및 `loadedmetadata`, `canplay`, `playing`, `timeupdate` 미디어 이벤트를 감지하여, 감지된 스트림 URL과 실시간 매칭해 메타데이터를 즉시 보강할 것.
    - 다운로드 팝업(`openDropdown`)을 여는 시점에도 DOM 스캔(`enrichStreamsFromDOM`)을 즉각 수행하여 사용자가 팝업을 클릭했을 때 항상 최신의 정확한 재생 시간과 해상도가 노출되도록 할 것.
  - **UI 메타 뱃지(Pill) 정갈한 배치**:
    - 단순 텍스트 나열 대신 화질 뱃지(`[1080p]`, `[720p]`), 해상도(`📐 1920×1080`), 재생 시간(`⏱ 12:34`), 파일 크기(`💾 250 MB`)를 세련된 뱃지 태그 형태로 배치하고, 정보가 없는 스트림은 존재하는 항목만 자연스럽게 표시되도록 예외 처리할 것.

### 🎬 토큰 인증형 fMP4 HLS 스트림(av01.media 등) 다운로드 실패(403 Forbidden) 및 SPA 동적 토큰 상속 원칙
- **실제 현상 및 원인**:
  - `av01.media` / `av01.tv` 등 최신 SPA 스트리밍 사이트는 클라이언트 사이드에서 Geo IP 검증(`geo.js`) ➡️ CDN Access API(`cdn-access`)를 거쳐 획득한 단기 유효 JWT `access_token`을 플레이어 커스텀 로더가 동적으로 주입함.
  - 플레이리스트 내부의 세그먼트 URL(`.m4s`) 및 초기화 세그먼트(`#EXT-X-MAP:URI="...init.mp4"`)에는 토큰 쿼리 스트링이 노출되어 있지 않아, 다운로더가 플레이리스트 원문 URL 그대로 요청할 경우 `403 Forbidden` 차단 발생.
  - 또한 Chrome 확장에서 Master M3U8로부터 서브 플레이리스트 상대 경로(Relative URL)를 해석할 때 브라우저 `new URL()` 표준 동작상 베이스 URL의 쿼리 스트링(`?access_token=...`)이 탈락(Drop)되어 세부 화질 요청 시 403 차단이 발생하는 결함 유발.
- **방지 원칙**:
  1. **M3U8 파서의 쿼리 파라미터 자동 상속(Query Inheritance)**:
     - `HlsDownloader`에서 플레이리스트 URL에 인증 토큰(`access_token`, `token`, `sig`, `auth`, `expires`)이 포함되어 있고, 파싱된 초기화 세그먼트(`#EXT-X-MAP`)나 개별 미디어 세그먼트(`.m4s`, `.ts`) URL에 토큰이 누락되어 있다면 부모 플레이리스트의 인증 쿼리 문자열을 자동으로 덧붙여(`AppendAuthQuery`) 전송할 것.
     - fMP4 스트림은 `init_segment.mp4`를 가장 먼저 기록한 후 순차적으로 `.m4s` 조각들을 이어붙여 완벽한 `.mp4` 단일 영상으로 완성할 것.
  2. **Chrome 확장의 상대 경로 해석 시 Auth 쿼리 보존 및 화질 프로빙**:
     - `new URL(variantRelative, masterUrl)` 후 `masterUrl`에 쿼리가 존재할 경우 자식 URL의 searchParams로 자동 복제/전달하여 화질 프로빙 및 다운로드 요청 시 토큰 유실을 방지할 것.
     - 서브 화질명 패턴(`-sv`, `-f`) 감지 시 상위 `master.m3u8` 프로빙으로 1080p, 720p, 360p 전 화질 목록을 수집하도록 연동할 것.
  3. **C++ 네이티브 `Av01Extractor` 구축**:
     - 데스크톱 앱 '새 다운로드'에 `av01.media` / `av01.tv` 주소를 직접 붙여넣었을 때도, C++ 네이티브로 비디오 ID 추출 ➡️ 메타데이터 API 조회(`title_translations.ko` 한국어 번역 제목 + `dvd_id` 품번 조합: `[품번] 제목.mp4`) ➡️ `geo.js` ➡️ `cdn-access` JWT 획득 ➡️ `master.m3u8` 최상위 화질(1080p) 자동 선택 및 스트림 생성 파이프라인을 원스톱으로 완결할 것.

### 🛑 `HttpEngine::DownloadBuffer` 매개변수 순서 혼동으로 인한 기형 HTTP 헤더(`Cookie: User-Agent: ...`) 및 전송 거부 방지
- **실제 실수**:
  - `HttpEngine::DownloadBuffer(url, referer, cookies, outData, abortFlag, customHeaders)`의 시그니처에서 3번째 인자가 `cookies`이고 6번째 인자가 `customHeaders`임에도, 신규 `Av01Extractor.cpp` 구현 시 `customHeaders`를 3번째 인자 자리에 넘겨 호출함 (`DownloadBuffer(geoUrl, defaultReferer, customHeaders, geoJson, abortFlag)`).
  - 이로 인해 내부적으로 `Cookie: User-Agent: Mozilla/5.0 ...\r\n\r\n`이라는 치명적인 이중 개행 기형 HTTP 헤더가 조합되었고, WinHTTP가 `ERROR_WINHTTP_INVALID_HEADER`로 전송을 거부하거나 대상 CDN 서버가 즉시 `400 Bad Request`를 반환하여 사용자가 앱에서 URL을 입력했을 때 `av01.media 동영상 스트림 추출 실패` 오류를 발생시킴.
- **방지 원칙**:
  1. `HttpEngine::DownloadBuffer` 호출 시 쿠키가 없는 일반 API 호출은 반드시 3번째 인자에 명시적 빈 문자열(`L""`)을 넘길 것 (`DownloadBuffer(url, referer, L"", outData, abortFlag)`).
  2. `DEFAULT_USER_AGENT`는 이미 WinHTTP 세션 레벨에서 최신 Chrome UA로 자동 설정되므로, 불필요하게 커스텀 User-Agent 헤더를 개별 호출마다 중복으로 만들어 넘기지 말 것.
  3. `HttpEngine::DownloadBuffer` 내부에서 `av01` / `iw01.xyz` 요청 시 CDN 검증에 필요한 `Origin: https://www.av01.media` 헤더를 기본 장착하여 인프라 레벨에서 호출 누락을 원천 방어할 것.
  4. 웹페이지 URL 파싱 시 취약한 `std::wregex` 대신 직관적이고 결함 없는 문자열 인덱스 탐색(`/video/` 및 `/videos/`)을 사용하여 파싱 실패 예외를 사전 차단할 것.

### 🛑 HLS 스트림 다운로드 도중 중단 방지: 임시 파일 삭제 금지, 세그먼트 디스크 캐시 기반 무손실 이어받기(`Resume`) 및 WinHTTP 수신 타임아웃(60s) 원칙
- **실제 실수**:
  - HLS 스트림 다운로드 도중 네트워크 지연이나 CDN 일시 버퍼링으로 단 1개의 세그먼트가 3회 재시도(0.4초 간격) 내에 실패하면, `HlsDownloader`가 `workerFailed = true`를 띄우고 지금까지 수신한 수백 개의 세그먼트 파일(`.tmp`)을 `DeleteFileW`로 모두 강제 삭제함.
  - 이로 인해 사용자가 수 기가바이트의 영상을 다운로드받다가 중단되었을 때, 지금까지 받은 데이터가 전부 유실되고 'HLS 스트림 다운로드 실패' 에러와 함께 처음부터 다시 다운로드해야 하는 치명적인 낭비와 결함을 초래함.
  - 또한 `HttpEngine::DownloadBuffer`의 수신 타임아웃이 15초(`15,000ms`)로 너무 짧아, 1080p 고화질 5MB 대용량 세그먼트 다운로드 시 8개 스레드가 회선을 공유하는 환경에서 15초를 초과하여 타임아웃으로 세그먼트가 드롭되는 현상이 발생함.
- **방지 원칙**:
  1. **HLS 세그먼트 캐시 디렉터리(`targetFilePath + L".hls_parts"`) 및 무손실 이어받기 보장**:
     - 임의의 임시 폴더 대신 대상 파일명 기반 고정 캐시 디렉터리를 생성하고, 이미 디스크에 정상 수신된 세그먼트(`fileSize > 0`)는 실행 시 사전 스캔하여 즉시 완료 처리(`Skip`)할 것.
     - 다운로드 중단이나 일시정지, 오류 발생 시 **이미 수신된 세그먼트 파일을 절대로 삭제하지 말고 보존**할 것.
     - 사용자가 '재시작'을 누르면 즉시 이전 진행 지점부터 세그먼트를 이어서 다운로드하도록 보장할 것.
     - 캐시 디렉터리는 오직 최종 타겟 `.mp4` 파일로 100% 무손실 병합이 완료되었을 때에만 안전하게 삭제할 것.
  2. **WinHTTP 수신 타임아웃 상향(60초) 및 버퍼 확대(64KB)**:
     - 대용량 세그먼트 수신 시 네트워크 변동을 견딜 수 있도록 `WinHttpSetTimeouts`의 수신 타임아웃을 최소 60초(`60,000ms`)로 상향할 것.
     - `WinHttpReadData`의 읽기 버퍼를 64KB로 확대하고, 읽기 중 소켓 오류(`readError`)를 정확히 감지하여 재시도 루프로 넘길 것.
  3. **충분한 재시도 횟수(6회) 및 지수 백오프**:
     - 개별 세그먼트 수신 실패 시 최소 6회 재시도 루프와 1초, 2초, 3초, 4초... 지수 백오프 대기를 부여하여 일시적인 CDN 혼잡을 100% 자가 복구하도록 구성할 것.




### 🛑 대용량 HLS 멀티스레드 세그먼트 다운로드 무중단 지속 및 실시간 토큰 핫 리프레시(Hot-Reload) 원칙
- **실제 실수 및 문제**:
  - `av01.media` / `iw01.xyz`와 같이 Cloudflare CDN 기반의 대용량 fMP4(약 3.5GB, 1065개 세그먼트)를 다운로드하는 도중, 약 695MB(19.3%, 약 210개 세그먼트) 지점에서 단 1개의 세그먼트가 Cloudflare 연결 끊김(`connection: close`)이나 네트워크 지연으로 6회 시도 실패하자마자 전역 플래그 `workerFailed = true`를 켜버림.
  - 이로 인해 **정상적으로 50MB/s 이상으로 고속 다운로드 중이던 나머지 7개 워커까지 전부 즉시 루프를 탈출하고 다운로드가 중단**되는 치명적인 조기 전역 폭사가 발생함.
  - 또한 다운로드 도중 단기 JWT `access_token`이 만료되거나 401/403이 발생했을 때 다운로더가 새 토큰을 갱신하지 않고 만료된 URL로만 반복 요청하다 실패함.
  - 더욱이 첫 실행 후 `m_url`이 `customers.iw01.xyz/...`로 치환되었는데, `Av01Extractor::IsSupported`가 `iw01.xyz` 도메인을 인식하지 않아 사용자가 "다시 시도" 버튼을 눌렀을 때 새 토큰 재발급 없이 이전의 만료된 URL로 다시 시작되어 즉시 멈추는 결함을 유발함.
  - 캐시 재개 시에도 첫 번째 워커(0번)만 진행률을 보고하여 UI 세그먼트 바의 1, 2번 블록만 파랗게 차고 3~8번 블록이 0으로 노출되는 시각적 불일치가 발생함.
- **방지 원칙**:
  1. **단일 세그먼트 실패 시 전역 워커 종료(`workerFailed`) 절대 금지 및 격리 재시도 큐(`retryQueue`) 운용**:
     - 1개 세그먼트가 일시 실패하더라도 절대 전체 워커를 종료시키지 말 것.
     - 실패한 세그먼트 인덱스를 스레드 안전한 `retryQueue`에 격리 등록하고, 해당 워커는 즉시 다음 세그먼트로 이동하여 다운로드를 계속 진행할 것.
     - 8개 워커가 기본 할당 슬라이스를 소진한 후, `retryQueue`에 남은 조각들을 순차적으로 분담하여 최대 8회 지수 백오프(1초..8초)로 끝까지 회수하도록 보장할 것.
  2. **실시간 토큰 핫 리프레시(TokenRefreshCallback) 연동**:
     - `HlsDownloader`에 토큰 갱신 콜백(`tokenRefresher`)을 주입하여, 세그먼트 요청 중 HTTP 401/403 감지 또는 세그먼트 반복 실패 시 `Av01Extractor::FetchFreshAccessToken`을 호출해 즉각 새로운 JWT를 발급받을 것.
     - 새 토큰 발급 즉시 스레드 동기화 락(`tokenMutex`)을 통해 현재 작업 중인 모든 세그먼트의 요청 URL 쿼리를 실시간 치환(`ReplaceOrAppendToken`)하여 1바이트의 끊김도 없이 무중단 연속 수신을 완결할 것.
  3. **`iw01.xyz` 도메인 지원 및 `DownloadTask` 원본 URL(`m_originalUrl`) 영속화**:
     - `Av01Extractor::IsSupported`에 `iw01.xyz`를 필수 포함하여 스트림 URL 상태에서도 AV01 영상임을 즉시 식별할 것.
     - `DownloadTask`에 `m_originalUrl`을 보존하여 언제든 "다시 시도"를 누르면 신선한 스트림 URL을 재추출하여 이어받기 완결.
  4. **M3U8 쿼리 파라미터 무조건 상속**:
     - 특정 키워드 필터링을 제거하고 플레이리스트 URL의 모든 쿼리 파라미터(`hb=`, `access_token=`, `sign=` 등)를 하위 세그먼트로 100% 안전하게 전파할 것.
  5. **캐시 재개 시 8개 워커 전체 세그먼트 블록 진행률 초기화 콜백 완결**:
     - `.hls_parts` 캐시 재개 시 8개 워커 전체에 대해 `sliceCompleted[w]`를 순회 보고하여 UI 세그먼트 바에 이미 받은 진행률이 즉시 정확하게 시각화되도록 할 것.


### 🛑 작업 영속화(tasks.dat) 파일 경로 내 공백(Whitespace) 분할 버그로 인한 재시작 시 작업 목록 증발 방지 원칙
- **실제 실수 및 문제**:
  - `DownloadManager::LoadTasks()`에서 `tasks.dat`로부터 다운로드 작업 정보를 복원할 때 `in >> urlStr >> pathStr >> parts >> statusInt` 스트림 추출 연산자를 사용함.
  - C++ 스트림 추출기(`>>`)는 공백(스페이스)을 단어 구분자로 처리하므로, 사용자의 저장 폴더명이나 파일명에 띄어쓰기가 포함된 경우(`새 폴더 (2)\SNOS-050-lada 【LADA 모자이크 파괴】...`) 경로의 첫 단어(`F:\downloads\새`)까지만 읽고 중단됨.
  - 뒤이어 오는 `in >> parts`가 숫자가 아닌 문자열(`폴더`)을 읽으려다 `std::ios::failbit` 에러를 발생시켜 파싱이 즉시 중단되었고, 그 결과 작업 목록이 메모리에 하나도 적재되지 못해 UI에서 다운로드 목록이 통째로 증발(빈 화면)함.
  - 나아가 앱 종료 시 `Shutdown()`이 빈 메모리 목록(`0`)으로 `tasks.dat`를 덮어써 사용자의 다운로드 작업이 완전히 유실되는 치명적 사고를 초래함.
- **방지 원칙**:
  1. **한 줄 전체 읽기(`std::getline`) 강제**:
     - 파일 경로, URL, 옵션 등 텍스트 기반 영속화 파일 읽기에는 절대 `operator>>`를 쓰지 말고 반드시 `std::getline(in, line)`을 사용하여 개행(`\n`) 기준으로 한 줄 전체를 온전히 읽어들일 것.
     - 읽어들인 문자열은 `\r`, trailing whitespace를 안전하게 trim한 후 변환할 것.
  2. **재시작 복원 작업의 디스크 캐시(`.hls_parts`) 자동 연동(`SetLoadedState`)**:
     - 재시작 후 복원된 작업에 대해 저장 경로에 `.hls_parts` 캐시 디렉터리가 존재하는지 검사하고, 이미 받아둔 세그먼트 파일들의 바이트 합계를 즉시 산출하여 `m_downloadedBytes`에 반영할 것.
     - 상태를 `TaskStatus::Paused(중단됨)`로 설정하여 사용자가 메인 창에서 더블클릭하거나 "시작/재시작" 버튼을 누르면 즉시 이전 진행 지점부터 이어받도록 완벽히 복원할 것.

---

### 🛑 다운로드 작업 삭제 시 UI 스레드 동기 대기(`m_mainThread.join()`)로 인한 UI 프리징 방지 원칙 (0ms 무지연 백그라운드 삭제)
- **실제 실수 및 문제**:
  - 사용자가 다운로드 진행 중인 작업을 리스트에서 삭제할 때, `MainWindow::OnDeleteSelected()`(UI 메인 스레드) ➡️ `DownloadManager::DeleteTask()` ➡️ `target->Cancel()` ➡️ `m_mainThread.join()` ➡️ `for (auto& t : m_workerThreads) t.join();`이 동기식으로 호출됨.
  - 최대 12개의 워커 스레드가 WinHTTP 네트워크 소켓 함수(`WinHttpReceiveResponse`, `WinHttpReadData`) 내부에서 타임아웃(15~60초)이나 응답 대기 중인 상태에서 UI 스레드가 `join()`으로 묶여버림.
  - 이로 인해 프로그램 창 전체가 수 초간 먹통(응답 없음, 윈도우 버벅임)이 되며 삭제 반응이 극도로 지연되는 치명적 UX 결함 발생.
- **방지 원칙 (0ms 비동기 분리 소멸 파이프라인)**:
  1. **UI 및 작업 목록 즉시 분리**: `DeleteTask(id)` 호출 즉시 작업 목록(`m_tasks`)에서 해당 작업을 분리(`erase`)하고, 변경 이벤트를 발생시켜 UI 리스트뷰에서 0ms 만에 해당 행이 즉각 사라지도록 처리할 것.
  2. **백그라운드 스레드 위임(`std::thread(...).detach()`)**: 실제 작업 중단 플래그 설정(`target->Cancel()`), 소켓 및 스레드 종료 대기(`target->WaitForExit()`), 물리 디스크 파일 삭제는 UI 스레드가 아닌 **독립 백그라운드 스레드로 넘겨 뒤에서 조용히 정리**되도록 전면 비동기화할 것.

---

### 🔒 멀티스레드 환경에서 UI 주기 갱신(`WM_TIMER`)과 워커 스레드 간 문자열 재할당 경합으로 인한 간헐적 크래시(`0xC0000005 Access Violation`) 방지 원칙
- **실제 실수 및 문제**:
  - UI 메인 스레드는 `WM_TIMER`에 의해 250ms마다 모든 작업의 `t->GetFilename()`, `t->GetUrl()`을 호출하여 리스트뷰를 갱신함.
  - 동시에 백그라운드 워커 스레드는 리디렉션이나 스트림 추출 완료 시 `m_filename = realTitle + L".mp4"`, `m_url = finalUrl`과 같이 문자열을 수정함.
  - `std::wstring`은 문자열 길이가 늘어날 때 내부 힙 버퍼를 재할당(Free 후 New)하는데, UI 스레드가 이전 버퍼 포인터를 읽는 순간 메모리가 해제되면서 **`0xC0000005 (Access Violation)` 메모리 참조 위반으로 프로세스가 간헐적으로 의문사**하는 현상 발생.
- **방지 원칙**:
  1. `DownloadTask`의 모든 문자열(`m_url`, `m_filename`, `m_savePath`, `m_errorMessage`, `m_completedAt`, `m_dependsOnId` 등) 접근은 전용 메타데이터 뮤텍스(`m_metaMutex`)를 통해 상호 배제(Lock) 보호하고 복사본을 반환할 것.
  2. `m_status`와 `m_totalBytes`는 `std::atomic<TaskStatus>`, `std::atomic<int64_t>`로 선언하여 스레드 간 동시 읽기/쓰기 시 원자성을 100% 보장할 것.
  3. `main.cpp` 최상단에 `SetUnhandledExceptionFilter`를 등록하여 예기치 못한 비정상 종료를 보호 모드로 안전하게 방어할 것.

---

### 🔗 대량 본문 텍스트 일괄 링크 자동 추출 및 순차 다운로드 큐잉(`m_dependsOnId`) 패턴
- **요구사항**: 사용자가 웹페이지나 커뮤니티에서 복사한 마크다운/일반 텍스트(예: `Send:[Part.1](URL)... 1Fichier:[Part.1]...`)를 붙여넣으면, 경유 페이지(nswvault)의 숨겨진 직링크를 자동 파싱하고 최적 호스트(Send.now)의 CDN 고속 직링크를 획득하여 순차적으로 다운로드하도록 지원.
- **핵심 패턴**:
  1. **네이티브 C++ 텍스트 파서 (`LinkExtractor::ParseRawText`)**:
     - 마크다운 링크 `[Part.X](URL) (Size)` 및 평문 URL 정규식 추출
     - 호스트 블록 구분자(`Send:`, `1Fichier:`, `Megaup:`) 위치를 자동 분석하여 각 링크에 호스트 태그 자동 매핑
  2. **경유 페이지 및 호스트 직링크 자동 해석 (`LinkExtractor::ResolveLink`)**:
     - `nswvault.com/.../download/X` 접속 시 7초 카운트다운을 스킵하고 HTML 소스 내 `<a id="dl-link" href="...">`를 즉시 추출
     - `send.now` (Sendcm) 호스트 감지 시 `op=download2` 폼을 백그라운드 자동 POST하여 `302 Found` `Location` 헤더로부터 6.0 GB 초고속 CDN 직링크(`https://d8815.datatransfer.to/...`) 획득
  3. **순차 다운로드 의존성 큐잉 (`m_dependsOnId`)**:
     - 대용량 분할 파일들을 동시 다운로드할 경우 호스트의 IP당 동시접속 제한(IP Throttling)에 걸리는 문제를 방지하기 위해, Part 1만 기동하고 Part 2~5는 이전 파트에 대한 의존성 ID(`SetDependsOnId(prevId)`)를 부여하여 대기열(`Queued`)에 등록.
     - `DownloadManager::CheckQueue()`에서 이전 파트가 `Completed` 상태가 되는 즉시 다음 파트를 자동으로 연속 기동하여 사용자 개입 없이 전 파트가 순차 다운로드 완료되도록 보장.
