#pragma once
#include <string>
#include <atomic>

namespace EasyDownload {

class SextbExtractor {
public:
    // Checks if the given URL is a sextb video page
    static bool IsSupported(const std::wstring& url);

    // Resolves real video stream URL (.m3u8 / .txt playlist), title, and required Referer
    static bool ExtractStream(
        const std::wstring& pageUrl,
        std::wstring& outStreamUrl,
        std::wstring& outTitle,
        std::wstring& outReferer,
        std::atomic<bool>& abortFlag
    );
};

} // namespace EasyDownload

