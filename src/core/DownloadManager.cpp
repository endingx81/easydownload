#include "DownloadManager.h"
#include "Settings.h"
#include "HttpEngine.h"
#include "SextbExtractor.h"
#include "Av01Extractor.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>

namespace EasyDownload {

DownloadManager& DownloadManager::Instance() {
    static DownloadManager s_instance;
    return s_instance;
}

std::wstring DownloadManager::GetTasksFilePath() const {
    WCHAR appData[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData))) {
        std::wstring dir = appData;
        dir += L"\\EasyDownload";
        SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
        return dir + L"\\tasks.dat";
    }
    return L"tasks.dat";
}

void DownloadManager::Initialize() {
    LoadTasks();
}

void DownloadManager::Shutdown() {
    SaveTasks();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& task : m_tasks) {
        if (task->GetStatus() == TaskStatus::Downloading || task->GetStatus() == TaskStatus::Connecting) {
            task->Pause();
        }
    }
}

std::shared_ptr<DownloadTask> DownloadManager::AddTask(
    const std::wstring& url,
    const std::wstring& savePath,
    const std::wstring& referer,
    const std::wstring& cookies,
    int parts,
    bool startImmediately,
    bool isHls,
    const std::wstring& dependsOnId
) {
    if (url.empty()) return nullptr;

    int actualParts = parts > 0 ? parts : Settings::Instance().GetInitialParts();
    std::wstring actualPath = savePath;
    bool isHlsResolved = isHls ||
                         SextbExtractor::IsSupported(url) ||
                         Av01Extractor::IsSupported(url) ||
                         (url.find(L".m3u8") != std::wstring::npos) ||
                         (url.find(L"playlist.txt") != std::wstring::npos) ||
                         (url.find(L"index.txt") != std::wstring::npos) ||
                         (url.find(L"master.txt") != std::wstring::npos);

    if (actualPath.empty()) {
        std::wstring fname = HttpEngine::ExtractFilenameFromUrl(url);
        if (HttpEngine::IsGenericFilename(fname) && !referer.empty()) {
            std::wstring refTitle = HttpEngine::ExtractTitleFromRefererUrl(referer);
            if (!refTitle.empty()) fname = refTitle + (isHlsResolved ? L".mp4" : L"");
        }
        if (isHlsResolved) {
            if (fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
                fname = fname.substr(0, fname.length() - 5) + L".mp4";
            } else if (fname.length() >= 4 && _wcsicmp(fname.c_str() + fname.length() - 4, L".txt") == 0) {
                fname = fname.substr(0, fname.length() - 4) + L".mp4";
            } else if (fname.find(L'.') == std::wstring::npos) {
                fname += L".mp4";
            }
        }
        std::wstring baseDir = Settings::Instance().GetTargetFolderForFilename(fname);
        WCHAR combined[MAX_PATH] = { 0 };
        PathCombineW(combined, baseDir.c_str(), fname.c_str());
        actualPath = combined;
    } else if (PathIsDirectoryW(actualPath.c_str())) {
        std::wstring fname = HttpEngine::ExtractFilenameFromUrl(url);
        if (HttpEngine::IsGenericFilename(fname) && !referer.empty()) {
            std::wstring refTitle = HttpEngine::ExtractTitleFromRefererUrl(referer);
            if (!refTitle.empty()) fname = refTitle + (isHlsResolved ? L".mp4" : L"");
        }
        if (isHlsResolved) {
            if (fname.length() >= 5 && _wcsicmp(fname.c_str() + fname.length() - 5, L".m3u8") == 0) {
                fname = fname.substr(0, fname.length() - 5) + L".mp4";
            } else if (fname.length() >= 4 && _wcsicmp(fname.c_str() + fname.length() - 4, L".txt") == 0) {
                fname = fname.substr(0, fname.length() - 4) + L".mp4";
            } else if (fname.find(L'.') == std::wstring::npos) {
                fname += L".mp4";
            }
        }
        WCHAR combined[MAX_PATH] = { 0 };
        PathCombineW(combined, actualPath.c_str(), fname.c_str());
        actualPath = combined;
    }

    if (isHlsResolved) {
        if (actualPath.length() >= 5 && _wcsicmp(actualPath.c_str() + actualPath.length() - 5, L".m3u8") == 0) {
            actualPath = actualPath.substr(0, actualPath.length() - 5) + L".mp4";
        } else if (actualPath.length() >= 4 && _wcsicmp(actualPath.c_str() + actualPath.length() - 4, L".txt") == 0) {
            actualPath = actualPath.substr(0, actualPath.length() - 4) + L".mp4";
        }
    }

    // Automatically ensure filename uniqueness: filename (1).ext, (2).ext, etc.
    actualPath = GenerateUniqueSavePath(actualPath);

    auto task = std::make_shared<DownloadTask>(url, actualPath, referer, cookies, actualParts);
    if (!dependsOnId.empty()) {
        task->SetDependsOnId(dependsOnId);
    }
    if (isHlsResolved) {
        task->SetIsHls(true);
    }
    task->SetOnStatusChanged([this](DownloadTask*) {
        CheckQueue();
        if (m_onTasksUpdated) m_onTasksUpdated();
    });

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.push_back(task);
    }

    if (startImmediately) {
        if (!dependsOnId.empty()) {
            auto dep = GetTask(dependsOnId);
            if (!dep || dep->GetStatus() == TaskStatus::Completed) {
                task->Start();
            }
        } else {
            task->Start();
        }
    }

    SaveTasks();
    if (m_onTasksUpdated) m_onTasksUpdated();
    return task;
}

bool DownloadManager::StartTask(const std::wstring& id) {
    auto task = GetTask(id);
    if (!task) return false;
    task->Start();
    SaveTasks();
    if (m_onTasksUpdated) m_onTasksUpdated();
    return true;
}

bool DownloadManager::PauseTask(const std::wstring& id) {
    auto task = GetTask(id);
    if (!task) return false;
    task->Pause();
    SaveTasks();
    if (m_onTasksUpdated) m_onTasksUpdated();
    return true;
}

bool DownloadManager::ResumeTask(const std::wstring& id) {
    auto task = GetTask(id);
    if (!task) return false;
    task->Resume();
    SaveTasks();
    if (m_onTasksUpdated) m_onTasksUpdated();
    return true;
}

bool DownloadManager::DeleteTask(const std::wstring& id, bool deleteFileOnDisk) {
    std::shared_ptr<DownloadTask> target;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
            if ((*it)->GetId() == id) {
                target = *it;
                m_tasks.erase(it);
                break;
            }
        }
    }

    if (target) {
        SaveTasks();
        if (m_onTasksUpdated) m_onTasksUpdated();

        // Perform cancellation, thread wait, and file deletion asynchronously in a detached background thread!
        // The UI thread returns in 0ms without ANY freeze!
        std::thread([target, deleteFileOnDisk]() {
            target->Cancel();
            target->WaitForExit();
            if (deleteFileOnDisk) {
                DeleteFileW(target->GetSavePath().c_str());
                std::wstring partFile = target->GetSavePath() + L".egpart";
                DeleteFileW(partFile.c_str());
            }
        }).detach();

        return true;
    }
    return false;
}

std::shared_ptr<DownloadTask> DownloadManager::GetTask(const std::wstring& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& task : m_tasks) {
        if (task->GetId() == id) {
            return task;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<DownloadTask>> DownloadManager::GetAllTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks;
}

void DownloadManager::CheckQueue() {
    int maxTasks = Settings::Instance().GetMaxConcurrentTasks();
    int activeCount = 0;
    std::vector<std::shared_ptr<DownloadTask>> queued;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& t : m_tasks) {
            if (t->GetStatus() == TaskStatus::Downloading || t->GetStatus() == TaskStatus::Connecting) {
                activeCount++;
            } else if (t->GetStatus() == TaskStatus::Queued) {
                queued.push_back(t);
            }
        }
    }

    for (auto& qTask : queued) {
        if (activeCount >= maxTasks) break;

        // Sequential download support: wait until dependent task completes
        std::wstring depId = qTask->GetDependsOnId();
        if (!depId.empty()) {
            auto dep = GetTask(depId);
            if (dep && dep->GetStatus() != TaskStatus::Completed) {
                continue; // Wait until dep task finishes!
            }
        }

        qTask->Start();
        activeCount++;
    }
}

void DownloadManager::SaveTasks() {
    std::wstring path = GetTasksFilePath();
    std::vector<std::shared_ptr<DownloadTask>> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_tasks;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return;

    out << snapshot.size() << "\n";
    for (const auto& t : snapshot) {
        // Output UTF-8 string lines
        auto w2s = [](const std::wstring& w) {
            if (w.empty()) return std::string("-");
            int sz = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
            std::string s(sz, 0);
            WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], sz, NULL, NULL);
            return s;
        };

        out << w2s(t->GetOriginalUrl()) << "\n";
        out << w2s(t->GetSavePath()) << "\n";
        out << t->GetNumParts() << "\n";
        out << (int)t->GetStatus() << "\n";
    }
}

void DownloadManager::LoadTasks() {
    std::wstring path = GetTasksFilePath();
    std::ifstream in(path, std::ios::binary);
    if (!in) return;

    std::string countLine;
    if (!std::getline(in, countLine)) return;
    while (!countLine.empty() && (countLine.back() == '\r' || countLine.back() == ' ')) countLine.pop_back();
    if (countLine.empty()) return;

    size_t count = 0;
    try {
        count = std::stoull(countLine);
    } catch (...) {
        return;
    }

    auto s2w = [](const std::string& s) {
        if (s == "-" || s.empty()) return std::wstring(L"");
        int sz = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
        std::wstring w(sz, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], sz);
        return w;
    };

    auto trim = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    };

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.clear();

    for (size_t i = 0; i < count; ++i) {
        std::string urlStr, pathStr, partsStr, statusStr;
        if (!std::getline(in, urlStr)) break;
        if (!std::getline(in, pathStr)) break;
        if (!std::getline(in, partsStr)) break;
        if (!std::getline(in, statusStr)) break;

        trim(urlStr);
        trim(pathStr);
        trim(partsStr);
        trim(statusStr);

        int parts = 8;
        int statusInt = 2; // default Paused
        try { parts = std::stoi(partsStr); } catch (...) {}
        try { statusInt = std::stoi(statusStr); } catch (...) {}

        std::wstring url = s2w(urlStr);
        std::wstring savePath = s2w(pathStr);
        if (!url.empty()) {
            auto task = std::make_shared<DownloadTask>(url, savePath, L"", L"", parts);
            TaskStatus st = (TaskStatus)statusInt;
            if (st == TaskStatus::Downloading || st == TaskStatus::Connecting) {
                st = TaskStatus::Paused;
            }
            task->SetLoadedState(st);

            task->SetOnStatusChanged([this](DownloadTask*) {
                CheckQueue();
                if (m_onTasksUpdated) m_onTasksUpdated();
            });
            m_tasks.push_back(task);
        }
    }
}

bool DownloadManager::IsSavePathInUse(const std::wstring& path) const {
    for (const auto& t : m_tasks) {
        if (_wcsicmp(t->GetSavePath().c_str(), path.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

std::wstring DownloadManager::GenerateUniqueSavePath(const std::wstring& targetPath) const {
    if (targetPath.empty()) return targetPath;

    if (!PathFileExistsW(targetPath.c_str()) && !IsSavePathInUse(targetPath)) {
        return targetPath;
    }

    WCHAR dir[MAX_PATH] = { 0 };
    wcscpy_s(dir, targetPath.c_str());
    PathRemoveFileSpecW(dir);

    WCHAR file[MAX_PATH] = { 0 };
    wcscpy_s(file, targetPath.c_str());
    PathStripPathW(file);

    std::wstring filename = file;
    size_t dot = filename.find_last_of(L'.');
    std::wstring nameWithoutExt = (dot != std::wstring::npos) ? filename.substr(0, dot) : filename;
    std::wstring ext = (dot != std::wstring::npos) ? filename.substr(dot) : L"";

    int counter = 1;
    while (counter < 10000) {
        std::wstring candidateName = nameWithoutExt + L" (" + std::to_wstring(counter) + L")" + ext;
        WCHAR combined[MAX_PATH] = { 0 };
        PathCombineW(combined, dir, candidateName.c_str());
        std::wstring candidatePath = combined;

        if (!PathFileExistsW(candidatePath.c_str()) && !IsSavePathInUse(candidatePath)) {
            return candidatePath;
        }
        counter++;
    }

    return targetPath;
}

} // namespace EasyDownload
