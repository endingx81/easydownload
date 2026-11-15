#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace EasyDownload {

enum class LinkStatus {
    Parsed,       // URL parsed from text
    Resolving,    // Resolving intermediate / host links
    Resolved,     // Direct link resolved and ready
    Failed        // Resolution failed
};

struct ExtractedLink {
    int partIndex{ 0 };
    std::wstring label;       // e.g. "Part.1 (6 GB)"
    std::wstring hostName;    // "Send.now", "1Fichier", "Megaup", "Direct"
    std::wstring originalUrl; // e.g. "https://nswvault.com/.../download/1"
    std::wstring directUrl;   // e.g. "https://d8815.datatransfer.to/.../Fire Emblem...zip.001"
    std::wstring filename;    // e.g. "Fire Emblem- Fortune's Weave...zip.001"
    std::wstring sizeStr;     // e.g. "6 GB", "3.08 GB"
    int64_t fileSize{ 0 };
    LinkStatus status{ LinkStatus::Parsed };
    std::wstring statusMessage{ L"대기 중" };
};

class LinkExtractor {
public:
    // Parse bulk text (markdown links, raw URLs, host sections)
    static std::vector<ExtractedLink> ParseRawText(const std::wstring& rawText, const std::wstring& hostFilter = L"");

    // Resolve intermediate redirects (e.g. nswvault -> send.now -> direct datatransfer.to URL)
    static bool ResolveLink(ExtractedLink& link, std::atomic<bool>& abortFlag);

private:
    static bool ResolveNswvault(const std::wstring& nswUrl, std::wstring& outTargetUrl, std::wstring& outSize, std::atomic<bool>& abortFlag);
    static bool ResolveSendNow(const std::wstring& sendUrl, std::wstring& outDirectUrl, std::wstring& outFilename, int64_t& outFileSize, std::atomic<bool>& abortFlag);
};

} // namespace EasyDownload
