#pragma once
#include <string>
#include <atomic>

namespace EasyDownload {

class Av01Extractor {
public:
    // Checks if the given URL is an av01 video page (av01.media, av01.tv, or cdn iw01.xyz)
    static bool IsSupported(const std::wstring& url);

    // Extracts video numeric ID from page or manifest URL
    static std::wstring ExtractVideoId(const std::wstring& url);

    // Fetches a fresh CDN JWT access_token using files.iw01.xyz edge geo token
    static bool FetchFreshAccessToken(
        const std::wstring& videoId,
        std::wstring& outToken,
        std::atomic<bool>& abortFlag
    );

    // Resolves real video stream URL (.m3u8), title, and required Referer
    static bool ExtractStream(
        const std::wstring& pageUrl,
        std::wstring& outStreamUrl,
        std::wstring& outTitle,
        std::wstring& outReferer,
        std::atomic<bool>& abortFlag
    );
};

} // namespace EasyDownload

