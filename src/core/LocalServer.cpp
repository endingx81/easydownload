#include "LocalServer.h"
#include <sstream>
#include <vector>
#include <iostream>

namespace EasyDownload {

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    if (sizeNeeded <= 0) return L"";
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &result[0], sizeNeeded);
    return result;
}

static std::wstring ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t kPos = json.find(needle);
    if (kPos == std::string::npos) return L"";

    size_t colon = json.find(':', kPos + needle.length());
    if (colon == std::string::npos) return L"";

    size_t quoteStart = json.find('\"', colon + 1);
    if (quoteStart == std::string::npos) return L"";

    std::string value;
    bool escaped = false;
    for (size_t i = quoteStart + 1; i < json.length(); ++i) {
        char c = json[i];
        if (escaped) {
            if (c == 'n') value += '\n';
            else if (c == 'r') value += '\r';
            else if (c == 't') value += '\t';
            else value += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '\"') {
            break;
        } else {
            value += c;
        }
    }
    return Utf8ToWide(value);
}

LocalServer& LocalServer::Instance() {
    static LocalServer s_instance;
    return s_instance;
}

LocalServer::LocalServer() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

LocalServer::~LocalServer() {
    Stop();
    WSACleanup();
}

bool LocalServer::Start(int port) {
    if (m_running) return true;

    m_port = port;
    m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSock == INVALID_SOCKET) return false;

    // Set SO_REUSEADDR
    int opt = 1;
    setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)m_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return false;
    }

    if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    m_serverThread = std::thread(&LocalServer::ServerLoop, this);
    return true;
}

void LocalServer::Stop() {
    if (!m_running) return;
    m_running = false;

    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

void LocalServer::ServerLoop() {
    while (m_running) {
        sockaddr_in clientAddr = { 0 };
        int clientLen = sizeof(clientAddr);
        SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &clientLen);

        if (clientSock == INVALID_SOCKET) {
            if (!m_running) break;
            continue;
        }

        std::thread([this, clientSock]() {
            HandleClient(clientSock);
        }).detach();
    }
}

void LocalServer::HandleClient(SOCKET clientSock) {
    std::vector<char> reqBuf(16 * 1024);
    int bytesRecv = recv(clientSock, reqBuf.data(), (int)reqBuf.size() - 1, 0);

    if (bytesRecv > 0) {
        reqBuf[bytesRecv] = 0;
        std::string request(reqBuf.data(), bytesRecv);

        std::string line;
        std::istringstream stream(request);
        std::getline(stream, line);

        // CORS preflight or response header
        std::string corsHeaders =
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";

        if (line.rfind("OPTIONS", 0) == 0) {
            std::string res = "HTTP/1.1 200 OK\r\n" + corsHeaders + "Content-Length: 0\r\n\r\n";
            send(clientSock, res.data(), (int)res.size(), 0);
        } else if (line.find("GET /ping") != std::string::npos || line.find("GET /status") != std::string::npos) {
            std::string json = "{\"status\":\"ok\",\"app\":\"EasyDownload\",\"version\":\"1.0.0\"}";
            std::string res = "HTTP/1.1 200 OK\r\n" + corsHeaders +
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + std::to_string(json.length()) + "\r\n\r\n" + json;
            send(clientSock, res.data(), (int)res.size(), 0);
        } else if (line.find("POST /download") != std::string::npos) {
            size_t bodyPos = request.find("\r\n\r\n");
            std::string body = (bodyPos != std::string::npos) ? request.substr(bodyPos + 4) : "";

            DownloadRequest req;
            req.url = ExtractJsonString(body, "url");
            req.filename = ExtractJsonString(body, "filename");
            req.referer = ExtractJsonString(body, "referer");
            req.cookies = ExtractJsonString(body, "cookies");
            req.isVideo = (body.find("\"isVideo\":true") != std::string::npos);

            if (!req.url.empty() && m_onDownloadRequest) {
                m_onDownloadRequest(req);
            }

            std::string json = "{\"status\":\"ok\",\"message\":\"Download queued\"}";
            std::string res = "HTTP/1.1 200 OK\r\n" + corsHeaders +
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + std::to_string(json.length()) + "\r\n\r\n" + json;
            send(clientSock, res.data(), (int)res.size(), 0);
        } else if (line.find("POST /video") != std::string::npos) {
            size_t bodyPos = request.find("\r\n\r\n");
            std::string body = (bodyPos != std::string::npos) ? request.substr(bodyPos + 4) : "";

            VideoDetectedInfo info;
            info.pageUrl = ExtractJsonString(body, "pageUrl");
            info.videoUrl = ExtractJsonString(body, "videoUrl");
            info.title = ExtractJsonString(body, "title");
            info.type = ExtractJsonString(body, "type");
            info.referer = ExtractJsonString(body, "referer");
            if (info.referer.empty()) info.referer = info.pageUrl;
            info.cookies = ExtractJsonString(body, "cookies");

            if (!info.videoUrl.empty() && m_onVideoDetected) {
                m_onVideoDetected(info);
            }

            std::string json = "{\"status\":\"ok\",\"message\":\"Video stream detected\"}";
            std::string res = "HTTP/1.1 200 OK\r\n" + corsHeaders +
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + std::to_string(json.length()) + "\r\n\r\n" + json;
            send(clientSock, res.data(), (int)res.size(), 0);
        } else {
            std::string res = "HTTP/1.1 404 Not Found\r\n" + corsHeaders + "Content-Length: 0\r\n\r\n";
            send(clientSock, res.data(), (int)res.size(), 0);
        }
    }

    closesocket(clientSock);
}

} // namespace EasyDownload
