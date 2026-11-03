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
    const list = (res && Array.isArray(res.videos)) ? res.videos : [];
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

      const titleDiv = document.createElement("div");
      titleDiv.className = "video-title";
      titleDiv.textContent = v.title || tab.title || "웹 비디오 스트림";
      titleDiv.title = titleDiv.textContent;

      const metaDiv = document.createElement("div");
      metaDiv.className = "video-meta";

      const qualityBadge = document.createElement("span");
      qualityBadge.className = "badge-quality";
      qualityBadge.textContent = v.quality || (v.type && v.type.includes("M3U8") ? "HLS" : "VIDEO");

      const sizeSpan = document.createElement("span");
      sizeSpan.textContent = v.size ? v.size : (v.type || "");

      metaDiv.appendChild(qualityBadge);
      if (v.size || v.type) metaDiv.appendChild(sizeSpan);

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
          title: v.title || tab.title || "video_download",
          pageUrl: tab.url || "",
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

