#include "SextbExtractor.h"
#include "HttpEngine.h"
#include <vector>
#include <algorithm>

namespace EasyDownload {

static std::string FindAttr(const std::string& html, const std::string& prefix, const std::string& suffix) {
    size_t p = html.find(prefix);
    if (p == std::string::npos) return "";
    p += prefix.length();
    size_t end = html.find(suffix, p);
    if (end == std::string::npos) return "";
    return html.substr(p, end - p);
}

static std::string Base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    const std::string b = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[(unsigned char)b[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    if (size <= 0) return L"";
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &out[0], size);
    return out;
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &out[0], size, NULL, NULL);
    return out;
}

bool SextbExtractor::IsSupported(const std::wstring& url) {
    return (url.find(L"sextb.net/") != std::wstring::npos);
}

bool SextbExtractor::ExtractStream(
    const std::wstring& pageUrl,
    std::wstring& outStreamUrl,
    std::wstring& outTitle,
    std::wstring& outReferer,
    std::atomic<bool>& abortFlag
) {
    outStreamUrl.clear();
    outTitle.clear();
    outReferer.clear();

    // 1. Extract slug from URL
    std::wstring cleanUrl = pageUrl;
    size_t qPos = cleanUrl.find(L'?');
    if (qPos != std::wstring::npos) cleanUrl = cleanUrl.substr(0, qPos);
    while (!cleanUrl.empty() && (cleanUrl.back() == L'/' || cleanUrl.back() == L' ')) {
        cleanUrl.pop_back();
    }

    size_t lastSlash = cleanUrl.find_last_of(L'/');
    if (lastSlash == std::wstring::npos) return false;
    std::wstring slug = cleanUrl.substr(lastSlash + 1);
    if (slug.empty() || slug == L"sextb.net") return false;

    std::wstring embedUrl = L"https://sextb.net/e/" + slug;
    std::wstring embedRef = L"https://sextb.net/" + slug;

    // 2. Fetch embed page
    std::string embedHtml;
    if (!HttpEngine::DownloadBuffer(embedUrl, embedRef, L"", embedHtml, abortFlag)) {
        return false;
    }

    // 3. Extract pt, pk, data-film, data-episode
    std::string pt = FindAttr(embedHtml, "pt = \"", "\"");
    if (pt.empty()) pt = FindAttr(embedHtml, "pt=\"", "\"");
    std::string pk = FindAttr(embedHtml, "pk = \"", "\"");
    if (pk.empty()) pk = FindAttr(embedHtml, "pk=\"", "\"");
    std::string filmId = FindAttr(embedHtml, "data-film=\"", "\"");
    std::string epId = FindAttr(embedHtml, "data-episode=\"", "\"");

    std::string pageTitle = FindAttr(embedHtml, "<title>", "</title>");
    if (!pageTitle.empty()) {
        outTitle = HttpEngine::SanitizeFilename(Utf8ToWide(pageTitle), 50);
    }
    if (outTitle.empty()) {
        outTitle = slug;
    }

    if (pt.empty() || pk.empty() || filmId.empty() || epId.empty()) {
        return false;
    }

    // 4. Request /ajax/player
    std::string postData = "episode=" + epId + "&filmId=" + filmId + "&pt=" + pt;
    std::wstring ajaxHeaders = L"X-Requested-With: XMLHttpRequest\r\n";
    std::string ajaxResp;

    if (!HttpEngine::PostBuffer(
        L"https://sextb.net/ajax/player",
        embedUrl,
        L"",
        postData,
        L"application/x-www-form-urlencoded",
        ajaxResp,
        abortFlag,
        ajaxHeaders
    )) {
        return false;
    }

    // 5. Decrypt player_enc using pk
    std::string enc = FindAttr(ajaxResp, "\"player_enc\":\"", "\"");
    if (enc.empty()) return false;

    // Handle escaped slashes if any
    std::string cleanEnc;
    cleanEnc.reserve(enc.size());
    for (size_t i = 0; i < enc.size(); ++i) {
        if (enc[i] == '\\' && i + 1 < enc.size() && enc[i + 1] == '/') {
            cleanEnc.push_back('/');
            ++i;
        } else {
            cleanEnc.push_back(enc[i]);
        }
    }

    std::string rawBytes = Base64Decode(cleanEnc);
    if (rawBytes.empty() || pk.empty()) return false;

    std::string decHtml;
    decHtml.resize(rawBytes.size());
    for (size_t i = 0; i < rawBytes.size(); ++i) {
        decHtml[i] = rawBytes[i] ^ pk[i % pk.length()];
    }

    // 6. Extract player iframe src
    std::string iframeSrc = FindAttr(decHtml, "src=\"", "\"");
    if (iframeSrc.empty()) iframeSrc = FindAttr(decHtml, "src='", "'");
    if (iframeSrc.empty()) return false;

    std::wstring iframeSrcWide = Utf8ToWide(iframeSrc);

    // 7. Handle F4 stream (f4scdn.com / f4stream)
    if (iframeSrc.find("f4scdn") != std::string::npos || iframeSrc.find("f4s") != std::string::npos) {
        std::string f4Html;
        if (HttpEngine::DownloadBuffer(iframeSrcWide, L"https://sextb.net/", L"", f4Html, abortFlag)) {
            std::string apiPath = FindAttr(f4Html, "data-api=\"", "\"");
            std::string f4Title = FindAttr(f4Html, "<title>", "</title>");
            if (!f4Title.empty()) {
                outTitle = HttpEngine::SanitizeFilename(Utf8ToWide(f4Title), 50);
            }

            if (!apiPath.empty()) {
                std::wstring apiUrl = L"https://f4scdn.com" + Utf8ToWide(apiPath) + L"?ab=0&r=https%3A%2F%2Fsextb.net%2F";
                std::wstring f4ApiHeaders = L"Accept: application/json\r\nOrigin: https://f4scdn.com\r\nX-Requested-With: XMLHttpRequest\r\n";
                std::string apiResp;
                if (HttpEngine::DownloadBuffer(apiUrl, iframeSrcWide, L"", apiResp, abortFlag, f4ApiHeaders)) {
                    std::string streamUrl = FindAttr(apiResp, "\"src\":\"", "\"");
                    if (streamUrl.empty()) streamUrl = FindAttr(apiResp, "\"url\":\"", "\"");
                    if (!streamUrl.empty()) {
                        outStreamUrl = Utf8ToWide(streamUrl);
                        outReferer = iframeSrcWide;
                        return true;
                    }
                }
            }
        }
    }

    // 8. Handle StreamTape (streamtape.net)
    if (iframeSrc.find("streamtape.net") != std::string::npos) {
        std::string stHtml;
        if (HttpEngine::DownloadBuffer(iframeSrcWide, L"https://sextb.net/", L"", stHtml, abortFlag)) {
            std::string botLink = FindAttr(stHtml, "id=\"robotlink\" style=\"display:none;\">", "<");
            if (botLink.empty()) botLink = FindAttr(stHtml, "id=\"botlink\" style=\"display:none;\">", "<");
            if (!botLink.empty()) {
                outStreamUrl = L"https:" + Utf8ToWide(botLink) + L"&stream=1";
                outReferer = iframeSrcWide;
                return true;
            }
        }
    }

    return false;
}

} // namespace EasyDownload

