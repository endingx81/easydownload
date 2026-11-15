#include "SextbExtractor.h"
#include "HttpEngine.h"
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace EasyDownload {

static std::string FindAttr(const std::string& html, const std::string& prefix, const std::string& suffix) {
    size_t p = html.find(prefix);
    if (p == std::string::npos) return "";
    p += prefix.length();
    size_t end = html.find(suffix, p);
    if (end == std::string::npos) return "";
    return html.substr(p, end - p);
}

// Dean Edwards JavaScript Packer unpacker
static std::string UnpackDeanEdwards(const std::string& packed) {
    size_t fnPos = packed.find("function(p,a,c,k,e,d)");
    if (fnPos == std::string::npos) return "";

    size_t splitPos = packed.find(".split('|')", fnPos);
    if (splitPos == std::string::npos) return "";

    size_t dictEnd = packed.rfind('\'', splitPos);
    if (dictEnd == std::string::npos) return "";
    size_t dictStart = packed.rfind('\'', dictEnd - 1);
    if (dictStart == std::string::npos) return "";

    std::string dictStr = packed.substr(dictStart + 1, dictEnd - dictStart - 1);
    std::vector<std::string> symTab;
    size_t dPos = 0;
    while (dPos < dictStr.size()) {
        size_t pipe = dictStr.find('|', dPos);
        if (pipe == std::string::npos) {
            symTab.push_back(dictStr.substr(dPos));
            break;
        }
        symTab.push_back(dictStr.substr(dPos, pipe - dPos));
        dPos = pipe + 1;
    }

    size_t openParen = packed.find("}(", fnPos);
    if (openParen == std::string::npos) return "";
    size_t payloadStart = packed.find('\'', openParen);
    if (payloadStart == std::string::npos) return "";

    size_t payloadEnd = payloadStart + 1;
    while (payloadEnd < dictStart) {
        if (packed[payloadEnd] == '\'' && packed[payloadEnd - 1] != '\\') {
            break;
        }
        payloadEnd++;
    }
    if (payloadEnd >= dictStart) return "";

    std::string payload = packed.substr(payloadStart + 1, payloadEnd - payloadStart - 1);

    size_t comma1 = packed.find(',', payloadEnd);
    if (comma1 == std::string::npos || comma1 >= dictStart) return "";
    size_t comma2 = packed.find(',', comma1 + 1);
    if (comma2 == std::string::npos || comma2 >= dictStart) return "";

    int radix = std::atoi(packed.c_str() + comma1 + 1);
    if (radix <= 0 || radix > 62) radix = 36;

    auto decodeBaseN = [radix](const std::string& token) -> int {
        static const char* digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        int val = 0;
        for (char ch : token) {
            const char* p = strchr(digits, ch);
            if (!p) return -1;
            int d = static_cast<int>(p - digits);
            if (d >= radix) return -1;
            val = val * radix + d;
        }
        return val;
    };

    std::string unpacked;
    unpacked.reserve(payload.size() * 2);

    for (size_t i = 0; i < payload.size(); ) {
        if (isalnum(static_cast<unsigned char>(payload[i]))) {
            size_t start = i;
            while (i < payload.size() && isalnum(static_cast<unsigned char>(payload[i]))) {
                i++;
            }
            std::string token = payload.substr(start, i - start);
            int val = decodeBaseN(token);
            if (val >= 0 && val < static_cast<int>(symTab.size()) && !symTab[val].empty()) {
                unpacked += symTab[val];
            } else {
                unpacked += token;
            }
        } else if (payload[i] == '\\' && i + 1 < payload.size()) {
            unpacked += payload[i + 1];
            i += 2;
        } else {
            unpacked += payload[i++];
        }
    }
    return unpacked;
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

    // 9. Handle HGLink / Hanerix (hglink.to, hgcloud.to, hanerix.com, audinifer.com, vibuxer.com)
    if (iframeSrc.find("hglink") != std::string::npos || iframeSrc.find("hgcloud") != std::string::npos ||
        iframeSrc.find("hanerix") != std::string::npos || iframeSrc.find("audinifer") != std::string::npos ||
        iframeSrc.find("vibuxer") != std::string::npos) {

        std::string vidId;
        size_t ePos = iframeSrc.find("/e/");
        if (ePos != std::string::npos) {
            size_t idStart = ePos + 3;
            size_t idEnd = iframeSrc.find_first_of("?/#& \t", idStart);
            vidId = iframeSrc.substr(idStart, idEnd == std::string::npos ? std::string::npos : idEnd - idStart);
        }

        if (!vidId.empty()) {
            static const char* kHosts[] = { "hanerix.com", "audinifer.com", "vibuxer.com" };
            for (const char* host : kHosts) {
                if (abortFlag.load()) return false;
                std::string playerUrl = std::string("https://") + host + "/e/" + vidId;
                std::wstring playerUrlWide = Utf8ToWide(playerUrl);
                std::string playerHtml;

                if (HttpEngine::DownloadBuffer(playerUrlWide, L"https://sextb.net/", L"", playerHtml, abortFlag)) {
                    std::string hTitle = FindAttr(playerHtml, "<title>", "</title>");
                    if (!hTitle.empty()) {
                        outTitle = HttpEngine::SanitizeFilename(Utf8ToWide(hTitle), 50);
                    }

                    size_t evalPos = playerHtml.find("function(p,a,c,k,e,d)");
                    if (evalPos != std::string::npos) {
                        size_t scriptStart = playerHtml.rfind("<script", evalPos);
                        size_t scriptEnd = playerHtml.find("</script>", evalPos);
                        std::string scriptBlock = (scriptStart != std::string::npos && scriptEnd != std::string::npos)
                            ? playerHtml.substr(scriptStart, scriptEnd - scriptStart)
                            : playerHtml.substr(evalPos - 10, 15000);

                        std::string unpacked = UnpackDeanEdwards(scriptBlock);
                        if (!unpacked.empty()) {
                            size_t m3u8Pos = unpacked.find("https://");
                            while (m3u8Pos != std::string::npos) {
                                size_t m3u8End = unpacked.find_first_of("\"' \t\r\n", m3u8Pos);
                                std::string foundUrl = unpacked.substr(m3u8Pos, m3u8End - m3u8Pos);
                                if (foundUrl.find(".m3u8") != std::string::npos) {
                                    outStreamUrl = Utf8ToWide(foundUrl);
                                    outReferer = Utf8ToWide(std::string("https://") + host + "/");
                                    return true;
                                }
                                m3u8Pos = unpacked.find("https://", m3u8End);
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

} // namespace EasyDownload

