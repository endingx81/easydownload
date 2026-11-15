const BRIDGE_URL = "http://127.0.0.1:18989";
const tabVideos = new Map(); // tabId -> Array of detected videos
const recentDownloads = new Map(); // url -> timestamp

function formatBytes(bytes) {
  if (!bytes || bytes <= 0) return "";
  const KB = 1024;
  const MB = KB * 1024;
  const GB = MB * 1024;
  if (bytes >= GB) return (bytes / GB).toFixed(1) + " GB";
  if (bytes >= MB) return (bytes / MB).toFixed(1) + " MB";
  if (bytes >= KB) return (bytes / KB).toFixed(1) + " KB";
  return bytes + " B";
}

function formatDuration(sec) {
  if (!sec || isNaN(sec) || !isFinite(sec) || sec <= 0) return "";
  sec = Math.round(sec);
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;
  if (h > 0) {
    return `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
  }
  return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
}

function cleanPageTitle(rawTitle, maxLen = 50) {
  if (!rawTitle || typeof rawTitle !== "string") return "";
  let title = rawTitle.trim();
  // Strip trailing extension if any (.html, .m3u8, .mp4, .ts)
  title = title.replace(/\.(html?|m3u8|mp4|ts|mkv|webm)$/i, "");
  // Strip site branding suffixes only at the end (e.g. " - SupJav", " | YouTube")
  title = title.replace(/\s*[-|_|–—|\|]\s*(SupJav|JavDB|MissAV|YouTube|FC2|Dailymotion|Vimeo|Twitch|Bilibili|XVIDEOS|Pornhub|SpankBang|AV01\.tv|AV01\.media|AV01)\s*$/i, "");
  title = title.replace(/^(AV01\.tv|AV01\.media|AV01)\s*[-|_|–—|\|]\s*/i, "");
  // Replace illegal filename characters
  title = title.replace(/[\\/:*?"<>|]/g, "_");
  // Collapse whitespace
  title = title.replace(/\s+/g, " ").trim();
  // Limit title length to maxLen (default 50)
  if (title.length > maxLen) {
    title = title.substring(0, maxLen).trim();
  }
  return title;
}

function isGenericStreamTitle(title) {
  if (!title || typeof title !== "string") return true;
  let t = title.trim().toLowerCase();
  t = t.replace(/\[[^\]]*\]/g, "").trim();
  t = t.replace(/\.(m3u8|mp4|ts)$/i, "").trim();
  if (!t) return true;
  const genericList = [
    "웹 비디오", "웹 동영상", "동영상 스트림", "video", "video_stream",
    "video_download", "stream", "player", "embed", "index", "master",
    "playlist", "chunklist", "download_file", "동영상", "download",
    "高品質な無料av動画のストリーミング"
  ];
  if (genericList.includes(t)) return true;
  if (/^(index|master|playlist|chunklist|stream|seg|segment)[-_0-9a-z]*/i.test(t)) {
    return true;
  }
  return false;
}

function extractSlugFromUrl(urlStr) {
  if (!urlStr) return "";
  try {
    const u = new URL(urlStr);
    let host = u.hostname.replace(/^www\./, "");
    let siteName = host.split(".")[0];
    let pathname = u.pathname.replace(/\/$/, "");
    let lastSegment = pathname.split("/").pop() || "";
    lastSegment = lastSegment.replace(/\.(html?|php|jsp|asp|aspx)$/i, "");
    if (lastSegment && !/^(index|watch|video|play)$/i.test(lastSegment)) {
      return `${siteName}_${lastSegment}`.replace(/[\\/:*?"<>|]/g, "_");
    }
    return siteName ? `${siteName}_video` : "";
  } catch (e) {
    return "";
  }
}

async function getBestTitleForTab(tabId, fallbackTitle, streamUrl) {
  let title = "";
  let tabUrl = "";
  if (tabId && tabId > 0) {
    try {
      const tab = await chrome.tabs.get(tabId);
      if (tab) {
        if (tab.title) title = cleanPageTitle(tab.title);
        if (tab.url) tabUrl = tab.url;
      }
    } catch (e) {}
  }
  if (!title || isGenericStreamTitle(title)) {
    if (fallbackTitle && !isGenericStreamTitle(fallbackTitle)) {
      title = cleanPageTitle(fallbackTitle);
    }
  }
  if (!title || isGenericStreamTitle(title)) {
    const slug = extractSlugFromUrl(tabUrl);
    if (slug) title = slug;
  }
  if (!title && streamUrl) {
    const slug = extractSlugFromUrl(streamUrl);
    if (slug && !isGenericStreamTitle(slug)) title = slug;
  }
  if (!title) {
    title = "웹_동영상";
  }
  return title;
}

// Initialize settings
chrome.runtime.onInstalled.addListener(() => {
  chrome.storage.local.set({
    interceptDownloads: true,
    videoSnifferEnabled: true
  });

  // Create context menu
  chrome.contextMenus.create({
    id: "easydownload-link",
    title: "EasyDownload로 다운로드",
    contexts: ["link", "video", "audio"]
  });
});

// Check if desktop app is running
async function isAppRunning() {
  try {
    const res = await fetch(`${BRIDGE_URL}/ping`, { method: "GET", signal: AbortSignal.timeout(1000) });
    return res.ok;
  } catch (e) {
    return false;
  }
}

// Intercept Chrome Downloads accurately using onDeterminingFilename
async function handleDownloadIntercept(item) {
  const settings = await chrome.storage.local.get(["interceptDownloads"]);
  if (settings.interceptDownloads === false) return;

  // Ignore downloads initiated by extensions or internal
  if (item.byExtensionId) return;

  // Only handle http / https
  const targetUrl = item.finalUrl || item.url;
  if (!targetUrl || (!targetUrl.startsWith("http://") && !targetUrl.startsWith("https://"))) {
    return;
  }

  // Deduplicate: ignore if same url intercepted within 5s
  const now = Date.now();
  if (recentDownloads.has(targetUrl) && (now - recentDownloads.get(targetUrl) < 5000)) {
    return;
  }
  recentDownloads.set(targetUrl, now);
  if (recentDownloads.size > 100) {
    for (const [k, v] of recentDownloads) {
      if (now - v > 30000) recentDownloads.delete(k);
    }
  }

  // Check if EasyDownload desktop app is running
  const running = await isAppRunning();
  if (!running) {
    console.log("[EasyDownload] Desktop app not running, allowing Chrome download.");
    return;
  }

  // Cancel Chrome's slow single-thread download
  try {
    await chrome.downloads.cancel(item.id);
    await chrome.downloads.erase({ id: item.id });
  } catch (e) {
    console.warn("[EasyDownload] Cancel error:", e);
  }

  // Extract all matching cookies (target URL, parent root domain, referrer URL)
  let cookiesStr = "";
  try {
    const urlObj = new URL(targetUrl);
    const hostParts = urlObj.hostname.split(".");
    let baseDomain = hostParts.length >= 2 ? hostParts.slice(-2).join(".") : urlObj.hostname;

    const cookiePromises = [
      chrome.cookies.getAll({ url: targetUrl }).catch(() => []),
      chrome.cookies.getAll({ domain: baseDomain }).catch(() => [])
    ];
    if (item.referrer) {
      cookiePromises.push(chrome.cookies.getAll({ url: item.referrer }).catch(() => []));
    }

    const cookieLists = await Promise.all(cookiePromises);
    const cookieMap = new Map();
    cookieLists.flat().forEach(c => {
      if (c && c.name) cookieMap.set(c.name, c.value);
    });
    cookiesStr = Array.from(cookieMap.entries()).map(([k, v]) => `${k}=${v}`).join("; ");
  } catch (e) {
    console.warn("[EasyDownload] Cookie extraction warning:", e);
  }

  // Ensure Referer is present: fallback to root domain if referrer was missing
  let referer = item.referrer || "";
  if (!referer) {
    try {
      const u = new URL(targetUrl);
      referer = `${u.protocol}//${u.hostname}/`;
    } catch (e) {}
  }

  let resolvedFilename = item.filename ? item.filename.split(/[\\/]/).pop() : "";

  // Detect M3U8 video stream and resolve proper filename and extension (.mp4)
  const isM3u8 = targetUrl.toLowerCase().includes(".m3u8") ||
                 (resolvedFilename && resolvedFilename.toLowerCase().endsWith(".m3u8"));

  if (isM3u8) {
    let topTitle = "";
    try {
      const [activeTab] = await chrome.tabs.query({ active: true, currentWindow: true });
      if (activeTab && activeTab.title) {
        topTitle = cleanPageTitle(activeTab.title, 50);
      }
    } catch (e) {}

    if ((!resolvedFilename || isGenericStreamTitle(resolvedFilename)) && topTitle) {
      resolvedFilename = topTitle;
    }

    if (resolvedFilename.toLowerCase().endsWith(".m3u8")) {
      resolvedFilename = resolvedFilename.substring(0, resolvedFilename.length - 5);
    }
    if (!resolvedFilename.toLowerCase().endsWith(".mp4") && !resolvedFilename.toLowerCase().endsWith(".ts")) {
      resolvedFilename += ".mp4";
    }
  }

  // Forward to EasyDownload desktop core
  try {
    const payload = {
      url: targetUrl,
      filename: resolvedFilename,
      referer: referer,
      cookies: cookiesStr,
      userAgent: navigator.userAgent,
      isVideo: isM3u8
    };

    await fetch(`${BRIDGE_URL}/download`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
    console.log("[EasyDownload] Download forwarded successfully:", payload.url, "Filename:", payload.filename);
  } catch (err) {
    console.error("[EasyDownload] Forward failed:", err);
  }
}

chrome.downloads.onDeterminingFilename.addListener((item, suggest) => {
  // 1. Immediately invoke suggest synchronously so Chrome's 60-second wait is ELIMINATED!
  if (typeof suggest === "function") {
    try {
      if (item.filename) {
        suggest({ filename: item.filename, conflictAction: "uniquify" });
      } else {
        suggest();
      }
    } catch (e) {
      console.warn("[EasyDownload] suggest error:", e);
    }
  }

  // 2. Intercept and forward download immediately to desktop core
  handleDownloadIntercept(item);
});

// Parse M3U8 / Disguised Master Playlist into multiple quality choices (1080p, 720p, etc.)
async function parseM3u8Variants(url, baseTitle, tabId, refererUrl) {
  try {
    const headers = {};
    if (refererUrl) {
      headers["Referer"] = refererUrl;
    }
    if (url.includes("f4s")) {
      headers["Origin"] = "https://f4scdn.com";
      if (!headers["Referer"]) headers["Referer"] = "https://f4scdn.com/";
    }
    if (url.includes("iw01.xyz") || (refererUrl && refererUrl.includes("av01."))) {
      headers["Origin"] = "https://www.av01.media";
      if (!headers["Referer"]) headers["Referer"] = "https://www.av01.media/";
    }
    const res = await fetch(url, { headers });
    if (!res.ok) return;
    const text = await res.text();
    if (!text.includes("#EXT-X-STREAM-INF")) {
      // If direct single-variant media playlist with media segments (#EXTINF)
      if (text.includes("#EXTINF:")) {
        let cleanBase = cleanPageTitle(baseTitle, 50) || "동영상_스트림";
        let totalDuration = 0;
        const vLines = text.split("\n");
        for (const vl of vLines) {
          if (vl.startsWith("#EXTINF:")) {
            const dur = parseFloat(vl.substring(8));
            if (!isNaN(dur)) totalDuration += dur;
          }
        }
        let estSize = "";
        let durationStr = "";
        if (totalDuration > 0) {
          durationStr = formatDuration(totalDuration);
          estSize = "~" + formatBytes(Math.round((2500000 * totalDuration) / 8));
        }
        const singleVariant = {
          url: url,
          referer: refererUrl,
          type: "HLS Stream",
          title: cleanBase,
          quality: "HLS 고화질",
          resolution: "HD",
          duration: totalDuration,
          durationStr: durationStr,
          bandwidth: 2500000,
          bitrate: "2.5 Mbps",
          size: estSize,
          detectedAt: new Date().toLocaleTimeString()
        };
        if (!tabVideos.has(tabId)) tabVideos.set(tabId, []);
        const list = tabVideos.get(tabId);
        if (!list.some(existing => existing.url === url)) {
          list.push(singleVariant);
          chrome.tabs.sendMessage(tabId, {
            action: "streamDetected",
            stream: singleVariant,
            count: list.length
          }).catch(() => {});
          chrome.action.setBadgeText({ text: String(list.length), tabId: tabId });
          chrome.action.setBadgeBackgroundColor({ color: "#0284C7", tabId: tabId });
        }
        return;
      }

      // If sub-variant like index-f3-v1-a1.m3u8 or index90-sv3-v1-a1.m3u8, probe parent master (master.m3u8 or index.m3u8)
      if ((url.includes("-f") || url.includes("-sv")) && !url.includes("master.m3u8")) {
        const baseSlash = url.lastIndexOf("/");
        if (baseSlash !== -1) {
          const prefix = url.substring(0, baseSlash + 1);
          const qIdx = url.indexOf("?");
          const query = qIdx !== -1 ? url.substring(qIdx) : "";
          return parseM3u8Variants(prefix + "master.m3u8" + query, baseTitle, tabId, refererUrl);
        }
      }
      return;
    }

    const lines = text.split("\n");
    const variants = [];
    let firstVariantUrl = "";

    for (let i = 0; i < lines.length; ++i) {
      const line = lines[i].trim();
      if (line.startsWith("#EXT-X-STREAM-INF:")) {
        let resMatch = line.match(/RESOLUTION=(\d+x\d+)/i);
        let bwMatch = line.match(/BANDWIDTH=(\d+)/i);
        let resolution = resMatch ? resMatch[1] : "";
        let bandwidth = bwMatch ? parseInt(bwMatch[1]) : 0;

        let height = "";
        if (resolution) {
          const parts = resolution.split("x");
          height = parts[1] + "p";
        }

        let variantUrl = "";
        for (let j = i + 1; j < lines.length; ++j) {
          const next = lines[j].trim();
          if (next && !next.startsWith("#")) {
            variantUrl = new URL(next, url).href;
            break;
          }
        }

        if (variantUrl) {
          // If parent playlist had auth query params (e.g. access_token), propagate them to variantUrl
          try {
            const parentObj = new URL(url);
            if (parentObj.search) {
              const childObj = new URL(variantUrl);
              parentObj.searchParams.forEach((val, key) => {
                if (!childObj.searchParams.has(key)) {
                  childObj.searchParams.set(key, val);
                }
              });
              variantUrl = childObj.href;
            }
          } catch (e) {}

          if (!firstVariantUrl) firstVariantUrl = variantUrl;
          let compactQuality = height ? height : (bandwidth ? `${Math.round(bandwidth / 1000)}k` : "HLS");
          let label = height ? `${height} (${resolution})` : (bandwidth ? `${Math.round(bandwidth / 1000)} kbps` : "스트림");
          let bitrateStr = (bandwidth >= 1000000) ? (bandwidth / 1000000).toFixed(1) + " Mbps" : Math.round(bandwidth / 1000) + " kbps";
          let cleanBase = cleanPageTitle(baseTitle, 50) || "동영상_스트림";
          variants.push({
            url: variantUrl,
            referer: refererUrl,
            type: `HLS ${label}`,
            title: `${cleanBase} [${compactQuality}]`,
            quality: compactQuality,
            resolution: resolution || height,
            bandwidth: bandwidth,
            bitrate: bitrateStr,
            size: "",
            detectedAt: new Date().toLocaleTimeString()
          });
        }
      }
    }

    // Attempt to calculate total duration from first variant to compute estimated file size
    let totalDuration = 0;
    if (firstVariantUrl) {
      try {
        const vRes = await fetch(firstVariantUrl, { headers });
        if (vRes.ok) {
          const vText = await vRes.text();
          const vLines = vText.split("\n");
          for (const vl of vLines) {
            if (vl.startsWith("#EXTINF:")) {
              const dur = parseFloat(vl.substring(8));
              if (!isNaN(dur)) totalDuration += dur;
            }
          }
        }
      } catch (e) {}
    }

    // Estimate file sizes and attach duration
    variants.forEach(v => {
      if (totalDuration > 0) {
        v.duration = totalDuration;
        v.durationStr = formatDuration(totalDuration);
      }
      if (v.bandwidth > 0 && totalDuration > 0) {
        const estBytes = Math.round((v.bandwidth * totalDuration) / 8);
        v.size = "~" + formatBytes(estBytes);
      } else if (v.bandwidth > 0) {
        v.size = v.bitrate;
      }
    });

    if (variants.length > 0) {
      if (!tabVideos.has(tabId)) tabVideos.set(tabId, []);
      const list = tabVideos.get(tabId);
      for (const v of variants) {
        if (!list.some(existing => existing.url === v.url)) {
          list.push(v);
          chrome.tabs.sendMessage(tabId, {
            action: "streamDetected",
            stream: v,
            count: list.length
          }).catch(() => {});
        }
      }
      chrome.action.setBadgeText({ text: String(list.length), tabId: tabId });
      chrome.action.setBadgeBackgroundColor({ color: "#0284C7", tabId: tabId });
    }
  } catch (e) {
    console.debug("[EasyDownload] M3U8 parse failed:", e);
  }
}

// Video Stream Sniffer via WebRequest
chrome.webRequest.onResponseStarted.addListener(
  async (details) => {
    if (details.tabId < 0) return;

    const settings = await chrome.storage.local.get(["videoSnifferEnabled"]);
    if (settings.videoSnifferEnabled === false) return;

    const url = details.url.toLowerCase();

    // 1. Completely ignore YouTube / GoogleVideo streaming chunks (handled by dedicated YouTube tab detector)
    if (url.includes("googlevideo.com") || url.includes("youtube.com") || url.includes("youtu.be") || url.includes("videoplayback")) {
      return;
    }

    // 2. Ignore fragment chunks (.m4s, .ts, or byte-range requests) to prevent endless counter increase
    if (url.includes(".m4s") || url.includes(".ts") || url.includes("range=") || url.includes("sq=")) {
      return;
    }

    // 3. Intercept player APIs that supply the real stream (e.g. f4stream / f4scdn / sextb)
    if (url.includes("/api/play/") || url.includes("/player/api/")) {
      (async () => {
        try {
          let tabReferer = "";
          try {
            const tab = await chrome.tabs.get(details.tabId);
            if (tab && tab.url) tabReferer = tab.url;
          } catch (e) {}

          const headers = { "Accept": "application/json" };
          if (tabReferer) headers["Referer"] = tabReferer;
          if (details.url.includes("f4s")) {
            headers["Origin"] = "https://f4scdn.com";
            headers["Referer"] = details.url;
          }

          const resp = await fetch(details.url, { headers });
          if (resp.ok) {
            const data = await resp.json();
            const streamUrl = data.src || data.url;
            if (streamUrl && typeof streamUrl === "string") {
              let fullStreamUrl = streamUrl.startsWith("http") ? streamUrl : new URL(streamUrl, details.url).href;
              let title = await getBestTitleForTab(details.tabId, "", fullStreamUrl);
              parseM3u8Variants(fullStreamUrl, title, details.tabId, details.url);
            }
          }
        } catch (e) {}
      })();
      return;
    }

    let isVideo = false;
    let type = "video";

    const isPlaylist = url.includes(".m3u8") ||
                       url.includes("/playlist.txt") ||
                       url.includes("/index.txt") ||
                       url.includes("/master.txt") ||
                       (url.includes(".txt") && (url.includes("/hls/") || url.includes("/stream/")));

    if (isPlaylist) {
      isVideo = true;
      type = "HLS Stream (M3U8)";
    } else if (url.includes(".mpd")) {
      isVideo = true;
      type = "DASH Stream (MPD)";
    } else if (url.includes(".mp4") || url.includes(".webm") || url.includes(".m4v") || url.includes(".flv")) {
      isVideo = true;
      type = url.includes(".mp4") ? "MP4 Video" : (url.includes(".webm") ? "WebM Video" : "FLV Video");
    } else if (details.responseHeaders) {
      let clen = 0;
      let ctype = "";
      for (const h of details.responseHeaders) {
        const hname = h.name.toLowerCase();
        if (hname === "content-type") {
          ctype = h.value.toLowerCase();
        } else if (hname === "content-length") {
          clen = parseInt(h.value) || 0;
        }
      }

      if (ctype.includes("application/vnd.apple.mpegurl") || ctype.includes("application/x-mpegurl")) {
        isVideo = true;
        type = "HLS Stream (M3U8)";
      } else if (ctype.startsWith("video/") && clen > 500 * 1024) {
        isVideo = true;
        type = ctype.replace("video/", "").toUpperCase() + " Video";
      } else if (clen > 5 * 1024 * 1024 && (ctype.includes("octet-stream") || ctype.includes("binary")) &&
                 (url.includes("video") || url.includes("stream") || url.includes("media") || url.includes("play"))) {
        isVideo = true;
        type = "Media Stream";
      }
    }

    if (isVideo) {
      if (!tabVideos.has(details.tabId)) {
        tabVideos.set(details.tabId, []);
      }
      const list = tabVideos.get(details.tabId);
      // Avoid duplicates
      if (!list.some(v => v.url === details.url)) {
        let title = "동영상 스트림";
        let tabReferer = "";
        try {
          const tab = await chrome.tabs.get(details.tabId);
          if (tab && tab.title) title = cleanPageTitle(tab.title);
          if (tab && tab.url) tabReferer = tab.url;
        } catch (e) {}

        if (!title || isGenericStreamTitle(title)) {
          const slug = extractSlugFromUrl(tabReferer);
          if (slug) title = slug;
        }
        if (!title) title = "동영상 스트림";

        let fileSizeStr = "";
        if (details.responseHeaders) {
          for (const h of details.responseHeaders) {
            if (h.name.toLowerCase() === "content-length") {
              const cl = parseInt(h.value);
              if (cl > 0) fileSizeStr = formatBytes(cl);
              break;
            }
          }
        }

        const streamInfo = {
          url: details.url,
          referer: tabReferer,
          type: type,
          title: title,
          quality: type.replace(" Video", "").replace(" Stream", ""),
          size: fileSizeStr,
          bitrate: "",
          detectedAt: new Date().toLocaleTimeString()
        };
        list.push(streamInfo);

        // Update badge
        chrome.action.setBadgeText({ text: String(list.length), tabId: details.tabId });
        chrome.action.setBadgeBackgroundColor({ color: "#0284C7", tabId: details.tabId });

        // Notify tab content script to display floating download button
        chrome.tabs.sendMessage(details.tabId, {
          action: "streamDetected",
          stream: streamInfo,
          count: list.length
        }).catch(() => {});

        // If HLS playlist, automatically parse sub-qualities (1080p, 720p, etc.)
        if (isPlaylist) {
          parseM3u8Variants(details.url, title, details.tabId, tabReferer);
        }
      }
    }
  },
  { urls: ["<all_urls>"] },
  ["responseHeaders"]
);

// Clear videos on tab navigation, SPA URL change, or removal
chrome.tabs.onUpdated.addListener((tabId, changeInfo) => {
  if (changeInfo.status === "loading" || changeInfo.url) {
    tabVideos.delete(tabId);
    chrome.action.setBadgeText({ text: "", tabId: tabId });
  }
});

chrome.tabs.onRemoved.addListener((tabId) => {
  tabVideos.delete(tabId);
});

// Context Menu Action
chrome.contextMenus.onClicked.addListener(async (info, tab) => {
  if (info.menuItemId === "easydownload-link") {
    const targetUrl = info.srcUrl || info.linkUrl;
    if (!targetUrl) return;

    let cookiesStr = "";
    try {
      const cookies = await chrome.cookies.getAll({ url: targetUrl });
      cookiesStr = cookies.map(c => `${c.name}=${c.value}`).join("; ");
    } catch (e) {}

    try {
      await fetch(`${BRIDGE_URL}/download`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          url: targetUrl,
          filename: "",
          referer: tab ? tab.url : "",
          cookies: cookiesStr,
          userAgent: navigator.userAgent,
          isVideo: info.mediaType === "video"
        })
      });
    } catch (e) {
      console.error("[EasyDownload] Context menu download error:", e);
    }
  }
});

// Messages from popup or content script
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  const currentTabId = (sender && sender.tab && sender.tab.id) ? sender.tab.id : msg.tabId;
  if (msg.action === "resetTabVideos") {
    if (currentTabId) {
      tabVideos.set(currentTabId, []);
      chrome.action.setBadgeText({ text: "", tabId: currentTabId });
    }
    sendResponse({ status: "ok" });
    return true;
  }
  if (msg.action === "getVideos") {
    const list = tabVideos.get(currentTabId) || [];
    sendResponse({ videos: list });
    return true;
  } else if (msg.action === "videoDetected") {
    if (msg.stream && msg.stream.url) {
      if (!tabVideos.has(currentTabId)) tabVideos.set(currentTabId, []);
      const list = tabVideos.get(currentTabId);
      const existing = list.find(s => s.url === msg.stream.url);
      if (existing) {
        let changed = false;
        if (msg.stream.duration && !existing.duration) {
          existing.duration = msg.stream.duration;
          existing.durationStr = msg.stream.durationStr || formatDuration(msg.stream.duration);
          changed = true;
        }
        if (msg.stream.resolution && !existing.resolution) {
          existing.resolution = msg.stream.resolution;
          changed = true;
        }
        if (msg.stream.size && !existing.size) {
          existing.size = msg.stream.size;
          changed = true;
        }
        if (changed) {
          chrome.tabs.sendMessage(currentTabId, {
            action: "streamDetected",
            stream: existing,
            count: list.length
          }).catch(() => {});
        }
        sendResponse({ success: true });
        return true;
      }
      if (!list.some(s => s.url === msg.stream.url)) {
        (async () => {
          let bestTitle = await getBestTitleForTab(currentTabId, msg.stream.title, msg.stream.url);
          msg.stream.title = bestTitle;
          if (msg.stream.duration && !msg.stream.durationStr) {
            msg.stream.durationStr = formatDuration(msg.stream.duration);
          }

          list.push(msg.stream);
          chrome.action.setBadgeText({ text: String(list.length), tabId: currentTabId });
          chrome.action.setBadgeBackgroundColor({ color: "#0284C7", tabId: currentTabId });

          // Forward to top frame so in-page UI updates immediately
          chrome.tabs.sendMessage(currentTabId, {
            action: "streamDetected",
            stream: msg.stream,
            count: list.length
          }).catch(() => {});

          if (msg.stream.url.includes(".m3u8")) {
            const tabReferer = (sender && sender.tab) ? sender.tab.url : "";
            parseM3u8Variants(msg.stream.url, bestTitle, currentTabId, tabReferer);
          }
        })();
      }
    }
    sendResponse({ success: true });
    return true;
  } else if (msg.action === "downloadVideo") {
    (async () => {
      let cookiesStr = "";
      try {
        const cookies = await chrome.cookies.getAll({ url: msg.videoUrl });
        cookiesStr = cookies.map(c => `${c.name}=${c.value}`).join("; ");
      } catch (e) {}

      let pageUrl = msg.pageUrl || (sender && sender.tab ? sender.tab.url : "");
      let referer = msg.referer || pageUrl;
      if (!referer) {
        try {
          const u = new URL(msg.videoUrl);
          referer = `${u.protocol}//${u.hostname}/`;
        } catch (e) {}
      }

      // 1. Prioritize title from UI card (msg.title)
      let title = (msg.title || "").trim();
      if (!title || isGenericStreamTitle(title)) {
        title = await getBestTitleForTab(currentTabId, title, msg.videoUrl);
      }

      // 2. Separate quality suffix like [1080p] if present
      let qSuffix = "";
      const qMatch = title.match(/(\s*\[[^\]]+\])/);
      if (qMatch) {
        qSuffix = qMatch[1];
        title = title.replace(qMatch[1], "").trim();
      }

      // 3. Clean and enforce 50-character limit on base title
      title = cleanPageTitle(title, 50);
      if (!title) title = "웹_동영상";

      // 4. Reattach quality suffix and ensure .mp4
      title = `${title}${qSuffix}`;
      if (title.toLowerCase().endsWith(".m3u8")) {
        title = title.substring(0, title.length - 5);
      }
      if (!title.toLowerCase().endsWith(".mp4") && !title.toLowerCase().endsWith(".ts") &&
          !title.toLowerCase().endsWith(".mkv") && !title.toLowerCase().endsWith(".webm")) {
        title += ".mp4";
      }

      fetch(`${BRIDGE_URL}/video`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          videoUrl: msg.videoUrl,
          title: title,
          pageUrl: pageUrl,
          referer: referer,
          cookies: cookiesStr,
          type: msg.type || "hls"
        })
      }).then(res => res.json()).then(data => {
        sendResponse({ success: true, data });
      }).catch(err => {
        sendResponse({ success: false, error: err.toString() });
      });
    })();
    return true; // Keep message channel open for async response
  } else if (msg.action === "downloadUrl") {
    fetch(`${BRIDGE_URL}/download`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        url: msg.url,
        filename: msg.filename || "",
        referer: msg.referer || (sender && sender.tab ? sender.tab.url : ""),
        cookies: msg.cookies || "",
        userAgent: navigator.userAgent,
        isVideo: !!msg.isVideo
      })
    }).then(res => res.json()).then(data => {
      sendResponse({ success: true, data });
    }).catch(err => {
      sendResponse({ success: false, error: err.toString() });
    });
    return true;
  } else if (msg.action === "checkStatus") {
    isAppRunning().then(running => sendResponse({ running }));
    return true;
  }
});

// Toolbar icon click: directly download the video detected in the active tab
chrome.action.onClicked.addListener(async (tab) => {
  if (!tab || !tab.id) return;
  const list = tabVideos.get(tab.id) || [];
  if (list.length > 0) {
    const stream = list[list.length - 1];
    let bestTitle = stream.title;
    if (!bestTitle || isGenericStreamTitle(bestTitle)) {
      bestTitle = await getBestTitleForTab(tab.id, bestTitle, stream.url);
    }
    if (bestTitle.toLowerCase().endsWith(".m3u8")) {
      bestTitle = bestTitle.substring(0, bestTitle.length - 5);
    }
    if (!bestTitle.toLowerCase().endsWith(".mp4") && !bestTitle.toLowerCase().endsWith(".ts")) {
      bestTitle += ".mp4";
    }

    fetch(`${BRIDGE_URL}/video`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        videoUrl: stream.url,
        title: bestTitle,
        pageUrl: tab.url || "",
        referer: tab.url || "",
        type: stream.type || "video"
      })
    }).catch(err => console.warn("[EasyDownload] Action click download error:", err));
  }
});

