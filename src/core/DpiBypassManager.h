#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include "windivert.h"

namespace EasyDownload {

enum class DpiPresetMode {
    KoreaStandard = 0,  // TLS SNI 2바이트 분할 + HTTP Host 변조 (KT/SK/LG 국내 통신사 표준)
    Quick = 1,          // TLS SNI 분할
    Aggressive = 2      // TLS SNI 분할 + HTTP Host 변조 + 패시브 RST 차단
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

    // Chrome 보안 DNS (DNS-over-HTTPS / Cloudflare 1.1.1.1) 관리
    static bool IsChromeDohConfigured();
    static bool SetChromeDohEnabled(bool enable);

private:
    DpiBypassManager();
    ~DpiBypassManager();

    bool LoadWinDivertFunctions();
    void WorkerLoop();
    void SendFragment(HANDLE hDivert, WINDIVERT_ADDRESS& addr,
                      uint8_t* packet, UINT packetLen, void* payload, UINT payloadLen,
                      bool isV4, bool isV6,
                      PWINDIVERT_IPHDR pIpHdr, PWINDIVERT_IPV6HDR pIpV6Hdr, PWINDIVERT_TCPHDR pTcpHdr,
                      unsigned int fragSize, int step);

    HMODULE m_hDivertDll{ NULL };
    HANDLE m_hDivert{ INVALID_HANDLE_VALUE };
    std::atomic<bool> m_bRunning{ false };
    std::thread m_workerThread;
    int m_activeMode{ 0 };
    DWORD m_lastError{ 0 };

    typedef HANDLE (WINAPI *PFN_WinDivertOpen)(const char *filter, WINDIVERT_LAYER layer, INT16 priority, UINT64 flags);
    typedef BOOL   (WINAPI *PFN_WinDivertRecv)(HANDLE handle, PVOID pPacket, UINT packetLen, PWINDIVERT_ADDRESS pAddr, UINT *readLen);
    typedef BOOL   (WINAPI *PFN_WinDivertSend)(HANDLE handle, PVOID pPacket, UINT packetLen, PWINDIVERT_ADDRESS pAddr, UINT *writeLen);
    typedef BOOL   (WINAPI *PFN_WinDivertClose)(HANDLE handle);
    typedef BOOL   (WINAPI *PFN_WinDivertHelperParsePacket)(PVOID pPacket, UINT packetLen, PWINDIVERT_IPHDR *ppIpHdr, PWINDIVERT_IPV6HDR *ppIpv6Hdr, PWINDIVERT_ICMPHDR *ppIcmpHdr, PWINDIVERT_ICMPV6HDR *ppIcmpv6Hdr, PWINDIVERT_TCPHDR *ppTcpHdr, PWINDIVERT_UDPHDR *ppUdpHdr, PVOID *ppData, UINT *pDataLen);
    typedef UINT   (WINAPI *PFN_WinDivertHelperCalcChecksums)(PVOID pPacket, UINT packetLen, PWINDIVERT_ADDRESS pAddr, UINT64 flags);

    PFN_WinDivertOpen m_fnOpen{ nullptr };
    PFN_WinDivertRecv m_fnRecv{ nullptr };
    PFN_WinDivertSend m_fnSend{ nullptr };
    PFN_WinDivertClose m_fnClose{ nullptr };
    PFN_WinDivertHelperParsePacket m_fnParsePacket{ nullptr };
    PFN_WinDivertHelperCalcChecksums m_fnCalcChecksums{ nullptr };
};

} // namespace EasyDownload
