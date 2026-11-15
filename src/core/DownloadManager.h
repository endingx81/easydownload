#pragma once
#include "DownloadTask.h"
#include <memory>
#include <vector>
#include <mutex>

namespace EasyDownload {

class DownloadManager {
public:
    static DownloadManager& Instance();

    void Initialize();
    void Shutdown();

    std::shared_ptr<DownloadTask> AddTask(
        const std::wstring& url,
        const std::wstring& savePath = L"",
        const std::wstring& referer = L"",
        const std::wstring& cookies = L"",
        int parts = 0,
        bool startImmediately = true,
        bool isHls = false,
        const std::wstring& dependsOnId = L""
    );

    bool StartTask(const std::wstring& id);
    bool PauseTask(const std::wstring& id);
    bool ResumeTask(const std::wstring& id);
    bool DeleteTask(const std::wstring& id, bool deleteFileOnDisk = false);

    std::shared_ptr<DownloadTask> GetTask(const std::wstring& id);
    std::vector<std::shared_ptr<DownloadTask>> GetAllTasks();

    void CheckQueue();

    void SetOnTasksUpdated(std::function<void()> cb) { m_onTasksUpdated = cb; }

    void SaveTasks();
    void LoadTasks();

    std::wstring GenerateUniqueSavePath(const std::wstring& targetPath) const;
    bool IsSavePathInUse(const std::wstring& path) const;

private:
    DownloadManager() = default;
    ~DownloadManager() = default;

    std::vector<std::shared_ptr<DownloadTask>> m_tasks;
    std::mutex m_mutex;
    std::function<void()> m_onTasksUpdated;

    std::wstring GetTasksFilePath() const;
};

} // namespace EasyDownload
