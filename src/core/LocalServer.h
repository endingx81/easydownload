#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

namespace EasyDownload {

struct DownloadRequest {
    std::wstring url;
    std::wstring filename;
    std::wstring referer;
    std::wstring cookies;
    std::wstring userAgent;
    bool isVideo{ false };
};

struct VideoDetectedInfo {
    std::wstring pageUrl;
    std::wstring videoUrl;
    std::wstring title;
    std::wstring type;
    std::wstring referer;
    std::wstring cookies;
};

class LocalServer {
public:
    static LocalServer& Instance();

    bool Start(int port = 18989);
    void Stop();

    void SetOnDownloadRequest(std::function<void(const DownloadRequest&)> cb) {
        m_onDownloadRequest = cb;
    }

    void SetOnVideoDetected(std::function<void(const VideoDetectedInfo&)> cb) {
        m_onVideoDetected = cb;
    }

    bool IsRunning() const { return m_running; }
    int GetPort() const { return m_port; }

private:
    LocalServer();
    ~LocalServer();

    void ServerLoop();
    void HandleClient(SOCKET clientSock);

    int m_port{ 18989 };
    std::atomic<bool> m_running{ false };
    SOCKET m_listenSock{ INVALID_SOCKET };
    std::thread m_serverThread;

    std::function<void(const DownloadRequest&)> m_onDownloadRequest;
    std::function<void(const VideoDetectedInfo&)> m_onVideoDetected;
};

} // namespace EasyDownload
