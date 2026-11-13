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

document.addEventListener("DOMContentLoaded", async () => {
  const videoListEl = document.getElementById("videoList");
  const appStatusEl = document.getElementById("appStatus");
  const appStatusTextEl = document.getElementById("appStatusText");
  const inputUrlEl = document.getElementById("inputUrl");
  const btnManualDl = document.getElementById("btnManualDl");
  const chkIntercept = document.getElementById("chkIntercept");

  // 1. Load intercept settings
  chrome.storage.local.get(["interceptDownloads"], (res) => {
    chkIntercept.checked = res.interceptDownloads !== false;
  });
  chkIntercept.addEventListener("change", () => {
    chrome.storage.local.set({ interceptDownloads: chkIntercept.checked });
  });

  // 2. Check desktop app connection
  chrome.runtime.sendMessage({ action: "checkStatus" }, (res) => {
    if (res && res.running) {
      appStatusEl.className = "status-badge";
      appStatusTextEl.textContent = "프로그램 연결됨";
    } else {
      appStatusEl.className = "status-badge offline";
      appStatusTextEl.textContent = "프로그램 미실행";
    }
  });

  // 3. Query Active Tab
  const [activeTab] = await chrome.tabs.query({ active: true, currentWindow: true });
  if (!activeTab) return;

  // 4. Request detected videos for this tab
  chrome.runtime.sendMessage({ action: "getVideos", tabId: activeTab.id }, (res) => {
    let list = (res && Array.isArray(res.videos)) ? [...res.videos] : [];
    if (activeTab.url && (activeTab.url.includes("youtube.com/watch") || activeTab.url.includes("youtu.be/") || activeTab.url.includes("youtube.com/shorts/"))) {
      if (!list.some(v => v.url === activeTab.url)) {
        let ytTitle = activeTab.title ? activeTab.title.replace(/\s*-\s*YouTube\s*$/i, "").trim() : "YouTube 동영상";
        list.unshift({
          url: activeTab.url,
          title: ytTitle,
          type: "YouTube 최고 화질 (1080p/4K)",
          quality: "YouTube",
          size: "최고 화질"
        });
      }
    }
    renderVideos(list, activeTab);
  });

  function renderVideos(list, tab) {
    if (!list || list.length === 0) return;

    videoListEl.innerHTML = "";
    list.forEach((v) => {
      const card = document.createElement("div");
      card.className = "video-card";

      const infoDiv = document.createElement("div");
      infoDiv.className = "video-info";

      let displayTitle = v.title || tab.title || "웹_동영상";
      if (/^(index|master|playlist|chunklist|video|stream|웹 비디오|동영상 스트림)/i.test(displayTitle)) {
        displayTitle = (tab.title ? tab.title.trim() : "") || displayTitle;
      }

      // Separate quality suffix if present
      let qSuffix = "";
      const qMatch = displayTitle.match(/(\s*\[[^\]]+\])/);
      if (qMatch) {
        qSuffix = qMatch[1];
        displayTitle = displayTitle.replace(qMatch[1], "").trim();
      }

      // Strip site suffix, clean forbidden chars, and limit to 50 chars
      displayTitle = displayTitle.replace(/\s*[-|_|–—|\|]\s*(SupJav|JavDB|MissAV|YouTube|FC2|Dailymotion|Vimeo|Twitch|Bilibili|XVIDEOS|Pornhub|SpankBang|AV01\.tv|AV01\.media|AV01)\s*$/i, "");
      displayTitle = displayTitle.replace(/^(AV01\.tv|AV01\.media|AV01)\s*[-|_|–—|\|]\s*/i, "");
      displayTitle = displayTitle.replace(/\.(html?|m3u8|mp4|ts|mkv|webm)$/i, "");
      displayTitle = displayTitle.replace(/[\\/:*?"<>|]/g, "_").replace(/\s+/g, " ").trim();
      if (displayTitle.length > 50) {
        displayTitle = displayTitle.substring(0, 50).trim();
      }
      displayTitle = `${displayTitle}${qSuffix}.mp4`;

      const titleDiv = document.createElement("div");
      titleDiv.className = "video-title";
      titleDiv.textContent = displayTitle;
      titleDiv.title = titleDiv.textContent;

      const metaDiv = document.createElement("div");
      metaDiv.className = "video-meta";

      const qualityBadge = document.createElement("span");
      qualityBadge.className = "badge-quality";
      qualityBadge.textContent = v.quality || (v.type && v.type.includes("M3U8") ? "HLS" : "VIDEO");
      metaDiv.appendChild(qualityBadge);

      // Video Dimensions / Resolution (영상 크기)
      let resText = v.resolution || "";
      if (resText) {
        resText = resText.replace("x", "×");
        const resSpan = document.createElement("span");
        resSpan.className = "meta-pill";
        resSpan.textContent = `📐 ${resText}`;
        resSpan.title = `영상 해상도: ${resText}`;
        metaDiv.appendChild(resSpan);
      }

      // Playtime / Duration (플레이타임)
      let durText = v.durationStr || (v.duration ? formatDuration(v.duration) : "");
      if (durText) {
        const durSpan = document.createElement("span");
        durSpan.className = "meta-pill";
        durSpan.textContent = `⏱ ${durText}`;
        durSpan.title = `재생 시간: ${durText}`;
        metaDiv.appendChild(durSpan);
      }

      // File Size (용량)
      let sizeText = v.size || "";
      if (sizeText) {
        const sizeSpan = document.createElement("span");
        sizeSpan.className = "meta-pill";
        sizeSpan.textContent = `💾 ${sizeText}`;
        sizeSpan.title = `예상 파일 크기: ${sizeText}`;
        metaDiv.appendChild(sizeSpan);
      } else if (!resText && !durText) {
        const fallbackSpan = document.createElement("span");
        fallbackSpan.textContent = v.type || "";
        metaDiv.appendChild(fallbackSpan);
      }

      infoDiv.appendChild(titleDiv);
      infoDiv.appendChild(metaDiv);

      const btn = document.createElement("button");
      btn.className = "btn-download";
      btn.innerHTML = `
        <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
          <polyline points="7 10 12 15 17 10"></polyline>
          <line x1="12" y1="15" x2="12" y2="3"></line>
        </svg>
        <span>다운로드</span>
      `;

      btn.addEventListener("click", () => {
        btn.textContent = "전송 중...";
        btn.disabled = true;
        chrome.runtime.sendMessage({
          action: "downloadVideo",
          videoUrl: v.url,
          title: displayTitle,
          pageUrl: tab.url || "",
          referer: tab.url || "",
          type: v.type || "video"
        }, (resp) => {
          if (resp && resp.success) {
            btn.textContent = "완료!";
            setTimeout(() => window.close(), 500);
          } else {
            btn.textContent = "실패";
            btn.disabled = false;
          }
        });
      });

      card.appendChild(infoDiv);
      card.appendChild(btn);
      videoListEl.appendChild(card);
    });
  }

  // 5. Manual Download
  btnManualDl.addEventListener("click", () => {
    const url = inputUrlEl.value.trim();
    if (!url) return;

    btnManualDl.textContent = "전송 중...";
    btnManualDl.disabled = true;

    chrome.runtime.sendMessage({
      action: "downloadUrl",
      url: url,
      referer: activeTab ? activeTab.url : ""
    }, (resp) => {
      if (resp && resp.success) {
        btnManualDl.textContent = "성공!";
        setTimeout(() => window.close(), 500);
      } else {
        btnManualDl.textContent = "실패";
        btnManualDl.disabled = false;
      }
    });
  });
});

