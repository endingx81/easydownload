#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>

namespace EasyDownload {

enum class DpiPresetMode {
    KoreaStandard = 0,  // GoodbyeDPI -5 + Yandex SafeDNS (77.88.8.8:1253) 국내 통신사 표준 완벽 우회 (권장)
    Quick = 1,          // GoodbyeDPI -5 (DNS 리다이렉트 제외, DoH 사용자용)
    Aggressive = 2      // GoodbyeDPI -5 + DNS 리다이렉트 + 패시브 RST 차단
};

class DpiBypassManager {
public:
    static DpiBypassManager& Instance();

    bool Start(int presetMode = -1);
    void Stop();
    bool IsRunning() const { return m_bRunning.load(); }
    DWORD GetLastErrorCode() const { return m_lastError; }
    void Toggle();

    std::wstring GetPresetName(int mode) const;
    std::wstring GetStatusString() const;

    static void CleanupWinDivert();
    static void FlushDnsCache();

    // Chrome 보안 DNS (DNS-over-HTTPS / Cloudflare 1.1.1.1) 관리
    static bool IsChromeDohConfigured();
    static bool SetChromeDohEnabled(bool enable);

private:
    DpiBypassManager();
    ~DpiBypassManager();

    std::atomic<bool> m_bRunning{ false };
    volatile int m_stopFlag{ 0 };
    std::thread m_workerThread;
    int m_activeMode{ 0 };
    DWORD m_lastError{ 0 };
};

} // namespace EasyDownload
