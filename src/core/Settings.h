#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace EasyDownload {

struct CategoryRule {
    std::wstring id;            // e.g. "video", "music", "archive", "document", "program"
    std::wstring displayName;   // e.g. L"동영상", L"음악", L"압축파일", L"문서", L"프로그램"
    std::wstring extensions;    // e.g. L"mp4, mkv, avi, mov, webm, flv, ts, m4v, wmv"
    std::wstring folderPath;    // Custom designated folder (empty = use default download dir)
};

class Settings {
public:
    static Settings& Instance();

    void Load();
    void Save();

    std::wstring GetDefaultDownloadDir() const { return m_downloadDir; }
    void SetDefaultDownloadDir(const std::wstring& dir) { m_downloadDir = dir; }

    int GetInitialParts() const { return m_initialParts; }
    void SetInitialParts(int parts) { m_initialParts = parts; }

    int GetMaxParts() const { return m_maxParts; }
    void SetMaxParts(int parts) { m_maxParts = parts; }

    bool GetAdaptiveSpeedSplitting() const { return m_adaptiveSpeedSplitting; }
    void SetAdaptiveSpeedSplitting(bool enable) { m_adaptiveSpeedSplitting = enable; }

    int GetMaxConcurrentTasks() const { return m_maxConcurrentTasks; }
    void SetMaxConcurrentTasks(int maxTasks) { m_maxConcurrentTasks = maxTasks; }

    bool GetAutoStartWithWindows() const { return m_autoStartWithWindows; }
    void SetAutoStartWithWindows(bool enable);

    bool GetStartMinimized() const { return m_startMinimized; }
    void SetStartMinimized(bool val) { m_startMinimized = val; }

    int GetBridgePort() const { return m_bridgePort; }
    void SetBridgePort(int port) { m_bridgePort = port; }

    const std::vector<CategoryRule>& GetCategoryRules() const { return m_categories; }
    void SetCategoryRules(const std::vector<CategoryRule>& rules) { m_categories = rules; }

    std::wstring GetTargetFolderForFilename(const std::wstring& filename) const;

    std::wstring GetConfigFilePath() const;
    std::wstring GetDefaultChromeDownloadDir() const;

private:
    Settings();
    ~Settings() = default;

    void InitDefaultCategories();

    std::wstring m_downloadDir;
    int m_initialParts{ 1 };
    int m_maxParts{ 12 };
    bool m_adaptiveSpeedSplitting{ true };
    int m_maxConcurrentTasks{ 3 };
    bool m_autoStartWithWindows{ false };
    bool m_startMinimized{ false };
    int m_bridgePort{ 18989 };

    std::vector<CategoryRule> m_categories;
};

} // namespace EasyDownload
