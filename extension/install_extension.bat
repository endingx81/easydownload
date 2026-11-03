@echo off
title EasyDownload Chrome 확장 프로그램 등록 안내

echo ========================================================
echo    EasyDownload Chrome 확장 프로그램 등록 안내
echo ========================================================
echo.
echo 1. Chrome 브라우저의 확장 프로그램 관리 페이지로 이동합니다.
echo    (chrome://extensions)
echo.
echo 2. 페이지 우측 상단의 [개발자 모드] 토글 스위치를 켭니다.
echo.
echo 3. 좌측 상단의 [압축해제된 확장 프로그램을 로드합니다] 버튼을 누릅니다.
echo.
echo 4. 현재 열려있는 다음 폴더를 선택하세요:
echo    %~dp0
echo.
echo ========================================================
pause

start "" "chrome://extensions"
explorer "%~dp0"
