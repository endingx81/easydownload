#include <winsock2.h>
#include "DpiBypassManager.h"
#include "Settings.h"
#include <shlwapi.h>
#include <vector>
#include <cstring>
#include <chrono>

namespace EasyDownload {

// TLS ClientHello 패킷에서 SNI (Server Name Indication) 도메인 문자열의 시작 오프셋 탐색
static size_t FindSniOffset(const uint8_t* data, size_t len) {
    if (len < 44 || data[0] != 0x16) return 0;
    if (data[5] != 0x01) return 0; // Handshake Type == 1 (ClientHello)

    size_t pos = 5 + 4; // Handshake Header (Type:1, Len:3) 건너뛰기
    pos += 2;           // Client Version 건너뛰기
    pos += 32;          // Random (32 bytes) 건너뛰기

    if (pos >= len) return 0;
    uint8_t sessIdLen = data[pos];
    pos += 1 + sessIdLen; // Session ID 건너뛰기

    if (pos + 2 > len) return 0;
    uint16_t cipherSuitesLen = (data[pos] << 8) | data[pos + 1];
    pos += 2 + cipherSuitesLen; // Cipher Suites 건너뛰기

    if (pos + 1 > len) return 0;
    uint8_t compMethodsLen = data[pos];
    pos += 1 + compMethodsLen; // Compression Methods 건너뛰기

    if (pos + 2 > len) return 0;
    uint16_t extensionsLen = (data[pos] << 8) | data[pos + 1];
    pos += 2; // Extensions 목록 시작

    size_t extensionsEnd = pos + extensionsLen;
    if (extensionsEnd > len) extensionsEnd = len;

    while (pos + 4 <= extensionsEnd) {
        uint16_t extType = (data[pos] << 8) | data[pos + 1];
        uint16_t extLen = (data[pos + 2] << 8) | data[pos + 3];
        pos += 4;

        if (extType == 0x0000) { // Server Name Indication (SNI) 확장
            // SNI 구조: ServerNameListLen(2) + ServerNameType(1) + ServerNameLen(2) + HostName
            if (pos + 5 <= extensionsEnd) {
                uint16_t nameLen = (data[pos + 3] << 8) | data[pos + 4];
                size_t hostNamePos = pos + 5;
                if (hostNamePos + nameLen <= extensionsEnd && nameLen >= 3) {
                    return hostNamePos; // 실제 도메인 문자열 시작 오프셋 반환
                }
            }
        }
        pos += extLen;
    }

    return 0;
}

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
        return L"고속 모드 (SNI 분할)";
    case 2:
        return L"강력 모드 (SNI+HTTP+RST 방어)";
    case 0:
    default:
        return L"한국 통신사 표준 (권장)";
    }
}

std::wstring DpiBypassManager::GetStatusString() const {
    if (m_bRunning.load()) {
        return L"DPI 우회: 켜짐 🟢 (" + GetPresetName(m_activeMode) + L")";
    }
    return L"DPI 우회: 꺼짐 ⚪";
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
                for (int w = 0; w < 15; ++w) {
                    if (QueryServiceStatus(hSvc, &status)) {
                        if (status.dwCurrentState == SERVICE_STOPPED) break;
                    }
                    Sleep(100);
                }
                CloseServiceHandle(hSvc);
            }
        }
        CloseServiceHandle(hSCM);
    }
}

bool DpiBypassManager::LoadWinDivertFunctions() {
    if (m_hDivertDll && m_fnOpen && m_fnRecv && m_fnSend && m_fnClose && m_fnParsePacket && m_fnCalcChecksums) {
        return true;
    }

    WCHAR modulePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    std::wstring baseDir = modulePath;

    std::vector<std::wstring> candidates = {
        baseDir + L"\\WinDivert.dll",
        baseDir + L"\\tools\\windivert\\WinDivert.dll",
        baseDir + L"\\..\\tools\\windivert\\WinDivert.dll",
        L"WinDivert.dll"
    };

    for (const auto& path : candidates) {
        m_hDivertDll = LoadLibraryW(path.c_str());
        if (m_hDivertDll) break;
    }

    if (!m_hDivertDll) return false;

    m_fnOpen = (PFN_WinDivertOpen)GetProcAddress(m_hDivertDll, "WinDivertOpen");
    m_fnRecv = (PFN_WinDivertRecv)GetProcAddress(m_hDivertDll, "WinDivertRecv");
    m_fnSend = (PFN_WinDivertSend)GetProcAddress(m_hDivertDll, "WinDivertSend");
    m_fnClose = (PFN_WinDivertClose)GetProcAddress(m_hDivertDll, "WinDivertClose");
    m_fnParsePacket = (PFN_WinDivertHelperParsePacket)GetProcAddress(m_hDivertDll, "WinDivertHelperParsePacket");
    m_fnCalcChecksums = (PFN_WinDivertHelperCalcChecksums)GetProcAddress(m_hDivertDll, "WinDivertHelperCalcChecksums");

    return (m_fnOpen && m_fnRecv && m_fnSend && m_fnClose && m_fnParsePacket && m_fnCalcChecksums);
}

void DpiBypassManager::SendFragment(HANDLE hDivert, WINDIVERT_ADDRESS& addr,
    uint8_t* packet, UINT packetLen, void* payload, UINT payloadLen,
    bool isV4, bool isV6,
    PWINDIVERT_IPHDR pIpHdr, PWINDIVERT_IPV6HDR pIpV6Hdr, PWINDIVERT_TCPHDR pTcpHdr,
    unsigned int fragSize, int step)
{
    std::vector<uint8_t> packetBak(packet, packet + packetLen);
    UINT origPacketLen = packetLen;

    if (fragSize >= payloadLen) {
        if (step == 1) fragSize = 0;
        else return;
    }

    if (step == 0) {
        // 첫 번째 조각: 처음 fragSize 바이트만 포함
        if (isV4 && pIpHdr) {
            pIpHdr->Length = htons(ntohs(pIpHdr->Length) - (USHORT)payloadLen + (USHORT)fragSize);
        } else if (isV6 && pIpV6Hdr) {
            pIpV6Hdr->Length = htons(ntohs(pIpV6Hdr->Length) - (USHORT)payloadLen + (USHORT)fragSize);
        }
        packetLen = packetLen - payloadLen + fragSize;
    } else if (step == 1) {
        // 두 번째 조각: 나머지 바이트 포함 (SeqNum 증가)
        if (isV4 && pIpHdr) {
            pIpHdr->Length = htons(ntohs(pIpHdr->Length) - (USHORT)fragSize);
        } else if (isV6 && pIpV6Hdr) {
            pIpV6Hdr->Length = htons(ntohs(pIpV6Hdr->Length) - (USHORT)fragSize);
        }
        memmove(payload, (char*)payload + fragSize, payloadLen - fragSize);
        packetLen -= fragSize;
        pTcpHdr->SeqNum = htonl(ntohl(pTcpHdr->SeqNum) + fragSize);
    }

    addr.PseudoIPChecksum = 0;
    addr.PseudoTCPChecksum = 0;
    m_fnCalcChecksums(packet, packetLen, &addr, 0);
    m_fnSend(hDivert, packet, packetLen, &addr, nullptr);

    // 원본 패킷 버퍼 복구
    memcpy(packet, packetBak.data(), origPacketLen);
}

void DpiBypassManager::WorkerLoop() {
    std::vector<uint8_t> packet(65535);
    WINDIVERT_ADDRESS addr;
    UINT packetLen = 0;

    while (m_bRunning.load() && m_hDivert != INVALID_HANDLE_VALUE) {
        if (!m_fnRecv(m_hDivert, packet.data(), (UINT)packet.size(), &addr, &packetLen)) {
            // Stop() 등에 의해 핸들이 닫히면 Recv 루프 정상 탈출
            break;
        }

        PWINDIVERT_IPHDR pIpHdr = nullptr;
        PWINDIVERT_IPV6HDR pIpv6Hdr = nullptr;
        PWINDIVERT_TCPHDR pTcpHdr = nullptr;
        PWINDIVERT_UDPHDR pUdpHdr = nullptr;
        PVOID pPayload = nullptr;
        UINT payloadLen = 0;

        if (!m_fnParsePacket(packet.data(), packetLen, &pIpHdr, &pIpv6Hdr, nullptr, nullptr, &pTcpHdr, &pUdpHdr, &pPayload, &payloadLen)) {
            m_fnSend(m_hDivert, packet.data(), packetLen, &addr, nullptr);
            continue;
        }

        // 0. 아웃바운드 UDP 443 (QUIC / HTTP3) 즉시 차단 (GoodbyeDPI -q 표준 규격)
        // Chrome이 QUIC(UDP 443)으로 먼저 연결을 시도하다 타임아웃/차단되는 현상을 원천 방지하여,
        // 브라우저가 0밀리초 만에 대기 없이 즉각 TCP 443(HTTPS)으로 직행하도록 강제 (첫 클릭 100% 즉시 로딩 보장)
        if (addr.Direction == WINDIVERT_DIRECTION_OUTBOUND && pUdpHdr && ntohs(pUdpHdr->DstPort) == 443) {
            continue; // QUIC 패킷 즉시 폐기 -> Chrome이 즉시 TCP HTTPS로 Fallback
        }

        // 1. 인바운드 위조 RST 차단 (통신사 감시 장비의 연결 강제 끊김 방어)
        if (addr.Direction == WINDIVERT_DIRECTION_INBOUND && pTcpHdr && pTcpHdr->Rst) {
            // DPI 차단 장비가 주입한 거짓 RST 패킷 폐기 (스루패스)
            continue;
        }

        // 2. 아웃바운드 TCP 데이터 패킷 가공 (SNI 분할 & HTTP Host 우회)
        if (addr.Direction == WINDIVERT_DIRECTION_OUTBOUND && pTcpHdr && pPayload && payloadLen > 0) {
            uint16_t dstPort = ntohs(pTcpHdr->DstPort);

            // 2-A. HTTPS (Port 443) - TLS ClientHello SNI 도메인 정밀 분할 우회 (Host Split)
            if (dstPort == 443) {
                const uint8_t* pData = (const uint8_t*)pPayload;
                // TLS Handshake (0x16) 및 Version 3.x (0x03 0x01/0x02/0x03)
                if (payloadLen >= 3 && pData[0] == 0x16 && pData[1] == 0x03) {
                    unsigned int fragSize = 2; // 기본 폴백: TLS 레코드 헤더 경계 (2바이트)

                    // [핵심] SNI 도메인 문자열의 중간을 정확히 두 동강 절단 (Host Split)
                    size_t sniOffset = FindSniOffset(pData, payloadLen);
                    if (sniOffset > 0 && sniOffset + 2 < payloadLen) {
                        // 도메인의 2번째 글자 위치(예: "mr" 과 "jav.net" 사이)에서 분할!
                        // 조각 1: 도메인 앞 2글자까지만 포함 ("mrjav.net" 단어 없음)
                        // 조각 2: 도메인 3번째 글자부터 포함 ("mrjav.net" 단어 없음)
                        // -> 통신사 감시 장비의 도메인 매칭 필터가 100% 무력화됨
                        fragSize = (unsigned int)(sniOffset + 2);
                    }

                    if (fragSize < payloadLen) {
                        // [핵심] 역순 전송 (Out-of-Order Delivery) + 도메인 중간 절단 (Host Split)
                        // 2번 뒷부분 조각(도메인 나머지, Seq = Original + fragSize)을 먼저 송출하여 통신사 재조립 버퍼링 무력화
                        SendFragment(m_hDivert, addr, packet.data(), packetLen, pPayload, payloadLen,
                                     pIpHdr != nullptr, pIpv6Hdr != nullptr, pIpHdr, pIpv6Hdr, pTcpHdr, fragSize, 1);

                        // 통신사 장비의 패킷 결합 방지를 위한 1ms 미세 지연
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));

                        // 1번 앞부분 조각(도메인 앞 2글자까지, Seq = Original) 송출 -> 목적지 웹서버가 온전히 합쳐 즉시 200 OK 응답
                        SendFragment(m_hDivert, addr, packet.data(), packetLen, pPayload, payloadLen,
                                     pIpHdr != nullptr, pIpv6Hdr != nullptr, pIpHdr, pIpv6Hdr, pTcpHdr, fragSize, 0);

                        // 원본 미분할 패킷은 드롭
                        continue;
                    }
                }
            }
            // 2-B. HTTP (Port 80) - Host 헤더 대소문자 변조 및 분할
            else if (dstPort == 80) {
                char* pData = (char*)pPayload;
                if (payloadLen >= 4 && (memcmp(pData, "GET ", 4) == 0 || memcmp(pData, "POST", 4) == 0 || memcmp(pData, "HEAD", 4) == 0)) {
                    // Host 헤더 검색 및 hoSt 변조
                    for (UINT i = 0; i + 5 < payloadLen; ++i) {
                        if (_strnicmp(pData + i, "Host:", 5) == 0) {
                            pData[i] = 'h';
                            pData[i + 1] = 'o';
                            pData[i + 2] = 'S';
                            pData[i + 3] = 't';
                            break;
                        }
                    }

                    unsigned int fragSize = 2;
                    if (fragSize < payloadLen) {
                        SendFragment(m_hDivert, addr, packet.data(), packetLen, pPayload, payloadLen,
                                     pIpHdr != nullptr, pIpv6Hdr != nullptr, pIpHdr, pIpv6Hdr, pTcpHdr, fragSize, 1);
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        SendFragment(m_hDivert, addr, packet.data(), packetLen, pPayload, payloadLen,
                                     pIpHdr != nullptr, pIpv6Hdr != nullptr, pIpHdr, pIpv6Hdr, pTcpHdr, fragSize, 0);
                        continue;
                    }
                }
            }
        }

        // 일반 정상 패킷 통과
        m_fnSend(m_hDivert, packet.data(), packetLen, &addr, nullptr);
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

    if (!LoadWinDivertFunctions()) {
        m_lastError = GetLastError();
        return false;
    }

    // 아웃바운드 HTTP(80)/HTTPS(443)/QUIC(UDP 443) 트래픽 및 인바운드 RST 패킷 필터링
    const char* filter = "((outbound && !loopback && (tcp.DstPort == 80 || tcp.DstPort == 443 || udp.DstPort == 443)) || (inbound && !loopback && tcp.Rst))";

    m_hDivert = m_fnOpen(filter, WINDIVERT_LAYER_NETWORK, 0, 0);
    if (m_hDivert == INVALID_HANDLE_VALUE) {
        m_lastError = GetLastError();
        return false;
    }

    m_lastError = 0;

    m_bRunning.store(true);
    m_workerThread = std::thread(&DpiBypassManager::WorkerLoop, this);
    return true;
}

void DpiBypassManager::Stop() {
    m_bRunning.store(false);

    if (m_hDivert != INVALID_HANDLE_VALUE) {
        if (m_fnClose) {
            m_fnClose(m_hDivert); // Recv 루프 즉시 블록 해제
        }
        m_hDivert = INVALID_HANDLE_VALUE;
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    CleanupWinDivert();
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
