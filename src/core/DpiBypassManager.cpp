#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <vector>
#include <string>

#include "DpiBypassManager.h"
#include "Settings.h"
#include "goodbyedpi/goodbyedpi_runner.h"
#include "goodbyedpi/dnsredir.h"

namespace EasyDownload {

DpiBypassManager& DpiBypassManager::Instance() {
    static DpiBypassManager s_instance;
    return s_instance;
}

DpiBypassManager::DpiBypassManager() {}

DpiBypassManager::~DpiBypassManager() {
    Stop();
}

std::wstring DpiBypassManager::GetPresetName(int mode) const {
    switch (mode) {
    case 1:
        return L"고속 모드 (SNI 단편화)";
    case 2:
        return L"강력 모드 (GoodbyeDPI -5 + 패시브 RST 차단)";
    case 0:
    default:
        return L"한국 통신사 표준 (GoodbyeDPI -5 + Yandex SafeDNS / 권장)";
    }
}

std::wstring DpiBypassManager::GetStatusString() const {
    if (m_bRunning.load()) {
        return L"DPI 우회: 켜짐 🟢 (" + GetPresetName(m_activeMode) + L")";
    }
    return L"DPI 우회: 꺼짐 ⚪";
}

void DpiBypassManager::FlushDnsCache() {
    flush_dns_cache();
}

void DpiBypassManager::CleanupWinDivert() {
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) {
        hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    }
    if (hSCM) {
        const wchar_t* serviceNames[] = { L"WinDivert1.4", L"WinDivert" };
        for (const wchar_t* name : serviceNames) {
            SC_HANDLE hSvc = OpenServiceW(hSCM, name, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
            if (hSvc) {
                SERVICE_STATUS status = { 0 };
                ControlService(hSvc, SERVICE_CONTROL_STOP, &status);
                DeleteService(hSvc);
                for (int w = 0; w < 10; ++w) {
                    if (QueryServiceStatus(hSvc, &status)) {
                        if (status.dwCurrentState == SERVICE_STOPPED) break;
                    }
                    Sleep(50);
                }
                CloseServiceHandle(hSvc);
            }
        }
        CloseServiceHandle(hSCM);
    }
}

bool DpiBypassManager::Start(int presetMode) {
    if (presetMode < 0) {
        presetMode = Settings::Instance().GetDpiBypassMode();
    }

    if (m_bRunning.load()) {
        if (m_activeMode == presetMode) {
            return true;
        }
        Stop();
    }

    m_activeMode = presetMode;

    // Windows 로컬 DNS 캐시 플러시
    flush_dns_cache();

    std::vector<std::string> args;
    args.push_back("goodbyedpi.exe");
    args.push_back("-5");

    if (m_activeMode == 0 || m_activeMode == 2) {
        // Yandex SafeDNS 우회 리다이렉션 (GoodbyeDPI 한국 공식 권장값)
        args.push_back("--dns-addr");
        args.push_back("77.88.8.8");
        args.push_back("--dns-port");
        args.push_back("1253");
        args.push_back("--dnsv6-addr");
        args.push_back("2a02:6b8::feed:0ff");
        args.push_back("--dnsv6-port");
        args.push_back("1253");
    }

    if (m_activeMode == 2) {
        // Aggressive: 패시브 RST 차단 (-p)
        args.push_back("-p");
    }

    m_stopFlag = 0;
    m_lastError = 0;
    m_bRunning.store(true);

    m_workerThread = std::thread([this, args]() {
        std::vector<char*> argvPtrs;
        for (const auto& s : args) {
            argvPtrs.push_back(const_cast<char*>(s.c_str()));
        }
        RunGoodbyeDPI((int)argvPtrs.size(), argvPtrs.data(), &m_stopFlag);
        m_bRunning.store(false);
    });

    // 스레드 및 WinDivert 필터가 정상 기동할 때까지 잠시 대기
    Sleep(100);

    return true;
}

void DpiBypassManager::Stop() {
    if (!m_bRunning.load() && !m_workerThread.joinable()) {
        return;
    }

    m_stopFlag = 1;
    m_bRunning.store(false);

    StopGoodbyeDPI();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    flush_dns_cache();
}

void DpiBypassManager::Toggle() {
    if (m_bRunning.load()) {
        Stop();
        Settings::Instance().SetDpiBypassEnabled(false);
        Settings::Instance().Save();
    } else {
        bool ok = Start(Settings::Instance().GetDpiBypassMode());
        if (ok) {
            Settings::Instance().SetDpiBypassEnabled(true);
            Settings::Instance().Save();
        }
    }
}

bool DpiBypassManager::IsChromeDohConfigured() {
    const wchar_t* subKey = L"SOFTWARE\\Policies\\Google\\Chrome";
    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };

    for (HKEY root : roots) {
        HKEY hKey = NULL;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            WCHAR modeBuf[64] = { 0 };
            DWORD dwType = 0;
            DWORD dwSize = sizeof(modeBuf);
            LONG res = RegQueryValueExW(hKey, L"DnsOverHttpsMode", NULL, &dwType, (LPBYTE)modeBuf, &dwSize);
            RegCloseKey(hKey);

            if (res == ERROR_SUCCESS && dwType == REG_SZ && wcslen(modeBuf) > 0) {
                if (_wcsicmp(modeBuf, L"off") != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool DpiBypassManager::SetChromeDohEnabled(bool enable) {
    const wchar_t* subKey = L"SOFTWARE\\Policies\\Google\\Chrome";
    const wchar_t* modeVal = L"secure";
    const wchar_t* templateVal = L"https://chrome.cloudflare-dns.com/dns-query";

    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    bool anySuccess = false;

    for (HKEY root : roots) {
        if (enable) {
            HKEY hKey = NULL;
            if (RegCreateKeyExW(root, subKey, 0, NULL, 0, KEY_SET_VALUE | KEY_READ, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                RegSetValueExW(hKey, L"DnsOverHttpsMode", 0, REG_SZ, (const BYTE*)modeVal, (DWORD)((wcslen(modeVal) + 1) * sizeof(wchar_t)));
                RegSetValueExW(hKey, L"DnsOverHttpsTemplates", 0, REG_SZ, (const BYTE*)templateVal, (DWORD)((wcslen(templateVal) + 1) * sizeof(wchar_t)));
                RegCloseKey(hKey);
                anySuccess = true;
            }
        } else {
            HKEY hKey = NULL;
            if (RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, L"DnsOverHttpsMode");
                RegDeleteValueW(hKey, L"DnsOverHttpsTemplates");
                RegCloseKey(hKey);
                anySuccess = true;
            }
        }
    }

    return anySuccess;
}

} // namespace EasyDownload
