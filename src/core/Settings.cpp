#include "Settings.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace EasyDownload {

Settings& Settings::Instance() {
    static Settings s_instance;
    return s_instance;
}

Settings::Settings() {
    m_downloadDir = GetDefaultChromeDownloadDir();
    InitDefaultCategories();
}

void Settings::InitDefaultCategories() {
    m_categories = {
        { L"video", L"동영상", L"mp4, mkv, avi, mov, webm, flv, ts, m4v, wmv", L"" },
        { L"music", L"음악", L"mp3, wav, flac, aac, m4a, ogg, wma", L"" },
        { L"archive", L"압축파일", L"zip, rar, 7z, tar, gz, bz2, iso", L"" },
        { L"document", L"문서", L"pdf, doc, docx, xls, xlsx, ppt, pptx, hwp, txt", L"" },
        { L"program", L"프로그램", L"exe, msi, bat, cmd", L"" }
    };
}

std::wstring Settings::GetDefaultChromeDownloadDir() const {
    WCHAR path[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        std::wstring p = path;
        p += L"\\Downloads";
        return p;
    }
    return L"C:\\Downloads";
}

std::wstring Settings::GetConfigFilePath() const {
    WCHAR appData[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = appData;
        dir += L"\\EasyDownload";
        SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
        return dir + L"\\config.ini";
    }
    return L"config.ini";
}

std::wstring Settings::GetTargetFolderForFilename(const std::wstring& filename) const {
    size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= filename.length()) {
        return m_downloadDir;
    }

    std::wstring ext = filename.substr(dot + 1);
    for (auto& ch : ext) ch = towlower(ch);

    for (const auto& cat : m_categories) {
        std::wstringstream ss(cat.extensions);
        std::wstring item;
        while (std::getline(ss, item, L',')) {
            size_t f = item.find_first_not_of(L" .");
            size_t l = item.find_last_not_of(L" .");
            if (f != std::wstring::npos && l != std::wstring::npos) {
                std::wstring cleanExt = item.substr(f, l - f + 1);
                for (auto& c : cleanExt) c = towlower(c);
                if (cleanExt == ext) {
                    if (!cat.folderPath.empty()) {
                        SHCreateDirectoryExW(NULL, cat.folderPath.c_str(), NULL);
                        return cat.folderPath;
                    }
                    return m_downloadDir;
                }
            }
        }
    }

    return m_downloadDir;
}

void Settings::Load() {
    std::wstring iniPath = GetConfigFilePath();
    WCHAR buf[MAX_PATH] = { 0 };

    GetPrivateProfileStringW(L"General", L"DownloadDir", m_downloadDir.c_str(), buf, MAX_PATH, iniPath.c_str());
    m_downloadDir = buf;

    m_initialParts = GetPrivateProfileIntW(L"Engine", L"InitialParts", 1, iniPath.c_str());
    if (m_initialParts < 1) m_initialParts = 1;
    if (m_initialParts > 4) m_initialParts = 4;

    m_maxParts = GetPrivateProfileIntW(L"Engine", L"MaxParts", 12, iniPath.c_str());
    if (m_maxParts < 1) m_maxParts = 1;
    if (m_maxParts > 12) m_maxParts = 12;

    m_adaptiveSpeedSplitting = GetPrivateProfileIntW(L"Engine", L"AdaptiveSplitting", 1, iniPath.c_str()) != 0;

    m_maxConcurrentTasks = GetPrivateProfileIntW(L"Engine", L"MaxTasks", 3, iniPath.c_str());
    if (m_maxConcurrentTasks < 1) m_maxConcurrentTasks = 1;

    m_bridgePort = GetPrivateProfileIntW(L"Bridge", L"Port", 18989, iniPath.c_str());
    m_startMinimized = GetPrivateProfileIntW(L"General", L"StartMinimized", 0, iniPath.c_str()) != 0;

    // Load category rules
    for (auto& cat : m_categories) {
        std::wstring section = L"Category_" + cat.id;

        WCHAR extBuf[1024] = { 0 };
        GetPrivateProfileStringW(section.c_str(), L"Extensions", cat.extensions.c_str(), extBuf, 1024, iniPath.c_str());
        cat.extensions = extBuf;

        WCHAR dirBuf[MAX_PATH] = { 0 };
        GetPrivateProfileStringW(section.c_str(), L"FolderPath", cat.folderPath.c_str(), dirBuf, MAX_PATH, iniPath.c_str());
        cat.folderPath = dirBuf;
    }

    // Check Run registry key
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR runVal[MAX_PATH] = { 0 };
        DWORD dwSize = sizeof(runVal);
        if (RegQueryValueExW(hKey, L"EasyDownload", NULL, NULL, (LPBYTE)runVal, &dwSize) == ERROR_SUCCESS) {
            m_autoStartWithWindows = true;
        } else {
            m_autoStartWithWindows = false;
        }
        RegCloseKey(hKey);
    }
}

void Settings::Save() {
    std::wstring iniPath = GetConfigFilePath();

    WritePrivateProfileStringW(L"General", L"DownloadDir", m_downloadDir.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Engine", L"InitialParts", std::to_wstring(m_initialParts).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Engine", L"MaxParts", std::to_wstring(m_maxParts).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Engine", L"AdaptiveSplitting", m_adaptiveSpeedSplitting ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Engine", L"MaxTasks", std::to_wstring(m_maxConcurrentTasks).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Bridge", L"Port", std::to_wstring(m_bridgePort).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"General", L"StartMinimized", m_startMinimized ? L"1" : L"0", iniPath.c_str());

    for (const auto& cat : m_categories) {
        std::wstring section = L"Category_" + cat.id;
        WritePrivateProfileStringW(section.c_str(), L"Extensions", cat.extensions.c_str(), iniPath.c_str());
        WritePrivateProfileStringW(section.c_str(), L"FolderPath", cat.folderPath.c_str(), iniPath.c_str());
    }

    SetAutoStartWithWindows(m_autoStartWithWindows);
}

void Settings::SetAutoStartWithWindows(bool enable) {
    m_autoStartWithWindows = enable;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH] = { 0 };
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
            RegSetValueExW(hKey, L"EasyDownload", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, L"EasyDownload");
        }
        RegCloseKey(hKey);
    }
}

} // namespace EasyDownload
