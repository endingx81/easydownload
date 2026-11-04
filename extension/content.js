// EasyDownload In-Page Video Sniffer & Floating Download Bar
// Strictly renders OUTSIDE video players (top = rect.top - 34px) in the top window only.
// Subframes/iframes perform video sniffing only and NEVER inject DOM buttons.

(function() {
  if (window.__EasyDownloadContentInjected) return;
  window.__EasyDownloadContentInjected = true;

  const isIframe = window.top !== window.self;

  // -------------------------------------------------------------
  // 1. SUBFRAME / IFRAME LOGIC: SNIFF ONLY, NO DOM BUTTONS
  // -------------------------------------------------------------
  if (isIframe) {
    function notifyVideo(v) {
      if (!v) return;
      const src = v.currentSrc || v.src;
      if (src && src.startsWith("http")) {
        chrome.runtime.sendMessage({
          action: "videoDetected",
          stream: {
            url: src,
            title: "", // Empty so background.js assigns top-level tab.title
            type: src.includes(".m3u8") ? "HLS" : "HTML5 Video",
            detectedAt: new Date().toLocaleTimeString()
          }
        }).catch(() => {});
      }
    }

    const mediaEvents = ["play", "playing", "loadedmetadata", "loadeddata", "canplay", "timeupdate"];
    mediaEvents.forEach(evt => {
      window.addEventListener(evt, (e) => {
        if (e.target && e.target.tagName === "VIDEO") {
          notifyVideo(e.target);
        }
      }, true);
    });

    document.querySelectorAll("video").forEach(notifyVideo);
    const subObserver = new MutationObserver(() => {
      document.querySelectorAll("video").forEach(notifyVideo);
    });
    subObserver.observe(document.body || document.documentElement, { childList: true, subtree: true });

    // Stop execution in subframes - NO DOM BUTTONS CREATED INSIDE IFRAMES!
    return;
  }

  // -------------------------------------------------------------
  // 2. TOP FRAME ONLY: DOM BUTTONS & QUALITY DROPDOWN MENU
  // -------------------------------------------------------------
  let detectedStreams = [];
  const activeTargets = []; // { targetElement, buttonEl }
  let currentDropdown = null;

  // Inject Modern 3D Dark Titanium Metallic & Glassmorphism CSS Styles
  function injectStyles() {
    if (document.getElementById("easydl-styles")) return;
    const style = document.createElement("style");
    style.id = "easydl-styles";
    style.textContent = `
      @keyframes easydlFadeIn {
        from { opacity: 0; transform: translateY(-4px); }
        to { opacity: 1; transform: translateY(0); }
      }

      /* 3D Dark Titanium Hardware Metallic Button */
      .easydl-video-btn {
        position: fixed !important;
        z-index: 2147483647 !important;
        width: 30px !important;
        height: 30px !important;
        border-radius: 7px !important;
        cursor: pointer !important;
        display: none;
        align-items: center !important;
        justify-content: center !important;
        padding: 0 !important;
        margin: 0 !important;
        outline: none !important;
        user-select: none !important;
        box-sizing: border-box !important;
        background: linear-gradient(145deg, #374151 0%, #1f2937 35%, #111827 75%, #0a0e17 100%) !important;
        border: 1px solid rgba(156, 163, 175, 0.45) !important;
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.65),
                    inset 0 1px 1px rgba(255, 255, 255, 0.4),
                    inset 0 -2px 3px rgba(0, 0, 0, 0.75) !important;
        color: #38bdf8 !important;
        transition: transform 0.15s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.15s, background 0.2s, border-color 0.2s, color 0.15s !important;
      }
      .easydl-video-btn:hover {
        background: linear-gradient(145deg, #4b5563 0%, #283548 35%, #162030 75%, #0f172a 100%) !important;
        border-color: rgba(56, 189, 248, 0.8) !important;
        box-shadow: 0 6px 16px rgba(0, 0, 0, 0.75),
                    0 0 10px rgba(56, 189, 248, 0.35),
                    inset 0 1px 1px rgba(255, 255, 255, 0.55),
                    inset 0 -2px 3px rgba(0, 0, 0, 0.8) !important;
        transform: scale(1.08) !important;
        color: #7dd3fc !important;
      }
      .easydl-video-btn:active, .easydl-video-btn.active {
        background: linear-gradient(145deg, #111827 0%, #1f2937 60%, #374151 100%) !important;
        box-shadow: inset 0 2px 6px rgba(0, 0, 0, 0.85),
                    inset 0 1px 2px rgba(0, 0, 0, 0.9) !important;
        transform: translateY(1px) scale(0.96) !important;
      }

      /* 2. Silver Chrome (Polished Aluminum 3D Metallic) */
      .easydl-video-btn svg {
        width: 15px !important;
        height: 15px !important;
        flex-shrink: 0 !important;
        pointer-events: none !important;
        filter: drop-shadow(0 1px 1px rgba(0, 0, 0, 0.4)) !important;
      }
      .easydl-badge-count {
        background: rgba(56, 189, 248, 0.25) !important;
        color: #38bdf8 !important;
        border: 1px solid rgba(56, 189, 248, 0.5) !important;
        border-radius: 10px !important;
        padding: 1px 6px !important;
        font-size: 10px !important;
        font-weight: 700 !important;
        line-height: 1.2 !important;
      }
      .easydl-video-btn:hover .easydl-badge-count {
        background: #ffffff !important;
        color: #0284c7 !important;
        border-color: #ffffff !important;
      }
      .easydl-arrow {
        transition: transform 0.15s !important;
      }
      .easydl-video-btn.active .easydl-arrow {
        transform: rotate(180deg) !important;
      }

      /* Dropdown Menu */
      .easydl-quality-menu {
        position: fixed !important;
        z-index: 2147483647 !important;
        width: 360px !important;
        max-width: calc(100vw - 20px) !important;
        background: #0f172a !important;
        border: 1px solid rgba(56, 189, 248, 0.5) !important;
        border-radius: 10px !important;
        box-shadow: 0 16px 36px rgba(0, 0, 0, 0.75), 0 0 0 1px rgba(255, 255, 255, 0.05) !important;
        color: #f1f5f9 !important;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif !important;
        font-size: 12px !important;
        overflow: hidden !important;
        display: flex !important;
        flex-direction: column !important;
        animation: easydlFadeIn 0.15s ease-out !important;
        box-sizing: border-box !important;
        user-select: none !important;
      }
      .easydl-menu-header {
        display: flex !important;
        align-items: center !important;
        justify-content: space-between !important;
        padding: 10px 14px !important;
        background: #1e293b !important;
        border-bottom: 1px solid #334155 !important;
      }
      .easydl-menu-title {
        display: flex !important;
        align-items: center !important;
        gap: 6px !important;
        font-size: 13px !important;
        font-weight: 700 !important;
        color: #38bdf8 !important;
      }
      .easydl-menu-close {
        background: transparent !important;
        border: none !important;
        color: #94a3b8 !important;
        font-size: 16px !important;
        cursor: pointer !important;
        padding: 0 4px !important;
        line-height: 1 !important;
      }
      .easydl-menu-close:hover {
        color: #f1f5f9 !important;
      }
      .easydl-menu-body {
        max-height: 280px !important;
        overflow-y: auto !important;
        padding: 8px !important;
        display: flex !important;
        flex-direction: column !important;
        gap: 6px !important;
      }
      .easydl-stream-card {
        background: #1e293b !important;
        border: 1px solid #334155 !important;
        border-radius: 6px !important;
        padding: 8px 10px !important;
        display: flex !important;
        align-items: center !important;
        justify-content: space-between !important;
        gap: 8px !important;
        transition: border-color 0.15s, background 0.15s !important;
        cursor: pointer !important;
      }
      .easydl-stream-card:hover {
        border-color: #38bdf8 !important;
        background: #243248 !important;
      }
      .easydl-stream-info {
        flex: 1 !important;
        min-width: 0 !important;
      }
      .easydl-stream-title {
        font-size: 12px !important;
        font-weight: 600 !important;
        color: #f8fafc !important;
        white-space: nowrap !important;
        overflow: hidden !important;
        text-overflow: ellipsis !important;
        margin-bottom: 3px !important;
      }
      .easydl-stream-meta {
        display: flex !important;
        align-items: center !important;
        gap: 6px !important;
        font-size: 11px !important;
        color: #94a3b8 !important;
      }
      .easydl-badge-q {
        font-size: 10px !important;
        font-weight: 700 !important;
        padding: 2px 6px !important;
        border-radius: 4px !important;
        background: rgba(56, 189, 248, 0.15) !important;
        color: #38bdf8 !important;
        border: 1px solid rgba(56, 189, 248, 0.3) !important;
        line-height: 1 !important;
      }
      .easydl-badge-q.high {
        background: rgba(16, 185, 129, 0.15) !important;
        color: #34d399 !important;
        border-color: rgba(16, 185, 129, 0.3) !important;
      }
      .easydl-dl-btn {
        background: #0284c7 !important;
        border: 1px solid #38bdf8 !important;
        color: #ffffff !important;
        font-size: 11px !important;
        font-weight: 600 !important;
        border-radius: 5px !important;
        padding: 5px 10px !important;
        cursor: pointer !important;
        display: flex !important;
        align-items: center !important;
        gap: 4px !important;
        white-space: nowrap !important;
        transition: background 0.15s, transform 0.1s !important;
      }
      .easydl-dl-btn:hover {
        background: #0369a1 !important;
        transform: scale(1.03) !important;
      }
      .easydl-dl-btn:disabled {
        background: #475569 !important;
        border-color: #64748b !important;
        color: #94a3b8 !important;
        cursor: not-allowed !important;
        transform: none !important;
      }
      .easydl-menu-footer {
        padding: 8px 12px !important;
        background: #0f172a !important;
        border-top: 1px solid #1e293b !important;
        font-size: 11px !important;
        color: #64748b !important;
        text-align: center !important;
      }
      .easydl-empty {
        padding: 24px 16px !important;
        text-align: center !important;
        color: #94a3b8 !important;
        font-size: 12px !important;
      }
    `;
    (document.head || document.documentElement).appendChild(style);
  }

  // Add detected stream to cache
  function addStream(stream) {
    if (!stream || !stream.url) return;
    if (detectedStreams.some(s => s.url === stream.url)) {
      // Update metadata if richer
      const idx = detectedStreams.findIndex(s => s.url === stream.url);
      if (idx !== -1 && stream.size && !detectedStreams[idx].size) {
        detectedStreams[idx] = stream;
      }
      return;
    }
    detectedStreams.push(stream);
    updateAllButtonsUI();
  }

  // Update button text and badge count
  function updateAllButtonsUI() {
    activeTargets.forEach(({ buttonEl }) => {
      const countEl = buttonEl.querySelector(".easydl-badge-count");
      if (countEl) {
        countEl.textContent = detectedStreams.length || "1";
        countEl.style.display = detectedStreams.length > 1 ? "inline-block" : "none";
      }
    });
  }

  // Check if an iframe is likely a video player
  function isPlayerIframe(iframe) {
    try {
      const rect = iframe.getBoundingClientRect();
      if (rect.width < 250 || rect.height < 140) return false;
      const src = (iframe.src || "").toLowerCase();
      const id = (iframe.id || "").toLowerCase();
      const cls = (iframe.className || "").toLowerCase();

      if (src.includes("player") || src.includes("embed") || src.includes("video") ||
          src.includes("stream") || src.includes("recordplay") || src.includes("m3u8") ||
          src.includes("tube") || id.includes("player") || cls.includes("player")) {
        return true;
      }
      // If streams are already detected and this is a primary video frame on the page
      if (detectedStreams.length > 0 && rect.width >= 320 && rect.height >= 180) {
        return true;
      }
    } catch (e) {}
    return false;
  }

  // Trigger download of a specific stream
  function triggerDownload(stream, dlBtn, cardTitle) {
    if (dlBtn) {
      dlBtn.textContent = "전송 중...";
      dlBtn.disabled = true;
    }

    let pageTitle = document.title ? document.title.replace(/[\\/:*?"<>|]/g, "_").trim() : "웹_동영상";
    let streamTitle = (cardTitle || stream.title || pageTitle).trim();

    // Separate quality suffix if present
    let qSuffix = "";
    const qMatch = streamTitle.match(/(\s*\[[^\]]+\])/);
    if (qMatch) {
      qSuffix = qMatch[1];
      streamTitle = streamTitle.replace(qMatch[1], "").trim();
    }

    // Strip site suffix, clean forbidden chars, and limit to 50 chars
    streamTitle = streamTitle.replace(/\s*[-|_|–—]\s*(SupJav|JavDB|MissAV|YouTube|FC2|Dailymotion|Vimeo|Twitch|Bilibili|XVIDEOS|Pornhub|SpankBang)\s*$/i, "");
    streamTitle = streamTitle.replace(/\.(html?|m3u8|mp4|ts|mkv|webm)$/i, "");
    streamTitle = streamTitle.replace(/[\\/:*?"<>|]/g, "_").replace(/\s+/g, " ").trim();
    if (streamTitle.length > 50) {
      streamTitle = streamTitle.substring(0, 50).trim();
    }
    streamTitle = `${streamTitle}${qSuffix}.mp4`;

    chrome.runtime.sendMessage({
      action: "downloadVideo",
      videoUrl: stream.url,
      title: streamTitle,
      pageUrl: window.location.href,
      referer: window.location.href,
      type: stream.type || "video"
    }, (res) => {
      if (dlBtn) {
        if (res && res.success) {
          dlBtn.textContent = "전송 완료 ✓";
          dlBtn.style.background = "#10b981";
          dlBtn.style.borderColor = "#10b981";
          setTimeout(() => {
            closeDropdown();
          }, 800);
        } else {
          dlBtn.textContent = "실패";
          dlBtn.style.background = "#ef4444";
          dlBtn.style.borderColor = "#ef4444";
          setTimeout(() => {
            dlBtn.textContent = "다운로드";
            dlBtn.disabled = false;
            dlBtn.style.background = "";
            dlBtn.style.borderColor = "";
          }, 2000);
        }
      }
    });
  }

  // Close Quality Dropdown Menu
  function closeDropdown() {
    if (currentDropdown) {
      currentDropdown.menuEl.remove();
      if (currentDropdown.buttonEl) {
        currentDropdown.buttonEl.classList.remove("active");
      }
      currentDropdown = null;
    }
  }

  // Open Quality Dropdown Menu directly below button
  function openDropdown(buttonEl) {
    if (currentDropdown && currentDropdown.buttonEl === buttonEl) {
      closeDropdown();
      return;
    }
    closeDropdown();

    injectStyles();

    // If on YouTube, always ensure current active video URL is 100% synchronized!
    if (location.hostname.includes("youtube.com") || location.hostname.includes("youtu.be")) {
      const currentUrl = location.href;
      if (currentUrl.includes("/watch") || currentUrl.includes("/shorts/") || location.hostname.includes("youtu.be")) {
        let ytTitle = document.title ? document.title.replace(/\s*-\s*YouTube\s*$/i, "").trim() : "YouTube 동영상";
        detectedStreams = [{
          url: currentUrl,
          title: ytTitle,
          type: "YouTube 최고 화질 (1080p/4K)",
          quality: "YouTube",
          size: "최고 화질"
        }];
        renderDropdownMenu(buttonEl);
        return;
      }
    }

    // Query latest videos from background to make sure all 1080p/720p/480p variants are loaded
    chrome.runtime.sendMessage({ action: "getVideos" }, (res) => {
      if (res && Array.isArray(res.videos)) {
        res.videos.forEach(s => addStream(s));
      }
      renderDropdownMenu(buttonEl);
    });
  }

  function renderDropdownMenu(buttonEl) {
    closeDropdown();

    const menu = document.createElement("div");
    menu.className = "easydl-quality-menu";

    // Header
    const header = document.createElement("div");
    header.className = "easydl-menu-header";
    header.innerHTML = `
      <div class="easydl-menu-title">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
          <polyline points="7 10 12 15 17 10"></polyline>
          <line x1="12" y1="15" x2="12" y2="3"></line>
        </svg>
        <span>다운로드할 화질 선택</span>
      </div>
    `;

    const closeBtn = document.createElement("button");
    closeBtn.className = "easydl-menu-close";
    closeBtn.innerHTML = "&times;";
    closeBtn.title = "닫기";
    closeBtn.onclick = (e) => {
      e.stopPropagation();
      closeDropdown();
    };
    header.appendChild(closeBtn);
    menu.appendChild(header);

    // Body
    const body = document.createElement("div");
    body.className = "easydl-menu-body";

    // Fallback stream if empty but video tag exists
    let streamList = [...detectedStreams];
    if (streamList.length === 0) {
      const v = document.querySelector("video");
      if (v && (v.currentSrc || v.src)) {
        streamList.push({
          url: v.currentSrc || v.src,
          title: document.title || "웹 비디오",
          type: "HTML5 Video",
          quality: "기본 화질"
        });
      }
    }

    if (streamList.length === 0) {
      body.innerHTML = `
        <div class="easydl-empty">
          <div>감지된 비디오 스트림이 없습니다.</div>
          <div style="font-size:11px; margin-top:4px; color:#64748b;">영상을 재생하면 화질 목록이 나타납니다.</div>
        </div>
      `;
    } else {
      streamList.forEach((s) => {
        const card = document.createElement("div");
        card.className = "easydl-stream-card";

        const info = document.createElement("div");
        info.className = "easydl-stream-info";

        const titleDiv = document.createElement("div");
        titleDiv.className = "easydl-stream-title";
        let displayTitle = (s.title || document.title || "웹_동영상").trim();
        let qSuffix = "";
        const qMatch = displayTitle.match(/(\s*\[[^\]]+\])/);
        if (qMatch) {
          qSuffix = qMatch[1];
          displayTitle = displayTitle.replace(qMatch[1], "").trim();
        }
        displayTitle = displayTitle.replace(/\s*[-|_|–—]\s*(SupJav|JavDB|MissAV|YouTube|FC2|Dailymotion|Vimeo|Twitch|Bilibili|XVIDEOS|Pornhub|SpankBang)\s*$/i, "");
        displayTitle = displayTitle.replace(/\.(html?|m3u8|mp4|ts|mkv|webm)$/i, "");
        displayTitle = displayTitle.replace(/[\\/:*?"<>|]/g, "_").replace(/\s+/g, " ").trim();
        if (displayTitle.length > 50) {
          displayTitle = displayTitle.substring(0, 50).trim();
        }
        displayTitle = `${displayTitle}${qSuffix}.mp4`;
        titleDiv.textContent = displayTitle;
        titleDiv.title = titleDiv.textContent;

        const metaDiv = document.createElement("div");
        metaDiv.className = "easydl-stream-meta";

        const q = s.quality || (s.type && s.type.includes("HLS") ? "HLS" : "VIDEO");
        const isHigh = q.includes("1080") || q.includes("4K") || q.includes("HD");

        const qBadge = document.createElement("span");
        qBadge.className = `easydl-badge-q ${isHigh ? "high" : ""}`;
        qBadge.textContent = q;

        const sizeSpan = document.createElement("span");
        sizeSpan.textContent = s.size ? s.size : (s.resolution || s.type || "");

        metaDiv.appendChild(qBadge);
        if (sizeSpan.textContent) metaDiv.appendChild(sizeSpan);

        info.appendChild(titleDiv);
        info.appendChild(metaDiv);

        const dlBtn = document.createElement("button");
        dlBtn.className = "easydl-dl-btn";
        dlBtn.innerHTML = `
          <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
            <polyline points="7 10 12 15 17 10"></polyline>
            <line x1="12" y1="15" x2="12" y2="3"></line>
          </svg>
          <span>다운로드</span>
        `;

        dlBtn.onclick = (e) => {
          e.stopPropagation();
          triggerDownload(s, dlBtn, titleDiv.textContent);
        };

        card.onclick = (e) => {
          triggerDownload(s, dlBtn, titleDiv.textContent);
        };

        card.appendChild(info);
        card.appendChild(dlBtn);
        body.appendChild(card);
      });
    }

    menu.appendChild(body);

    // Footer
    const footer = document.createElement("div");
    footer.className = "easydl-menu-footer";
    footer.textContent = "EasyDownload 가속기로 고속 분할 다운로드됩니다.";
    menu.appendChild(footer);

    document.body.appendChild(menu);
    buttonEl.classList.add("active");
    currentDropdown = { menuEl: menu, buttonEl: buttonEl };

    positionDropdown(buttonEl, menu);
  }

  // Adjust dropdown position relative to button
  function positionDropdown(buttonEl, menu) {
    if (!buttonEl || !menu) return;
    const btnRect = buttonEl.getBoundingClientRect();
    const menuWidth = 360;

    let top = btnRect.bottom + 6;
    let left = btnRect.right - menuWidth;

    if (left < 10) left = 10;
    if (top + 280 > window.innerHeight) {
      top = Math.max(10, btnRect.top - 290);
    }

    menu.style.top = Math.round(top) + "px";
    menu.style.left = Math.round(left) + "px";
  }

  // Create or attach button to a target video or player iframe
  function attachButtonToTarget(target) {
    if (target.__hasEasyDlButton) return;
    target.__hasEasyDlButton = true;

    injectStyles();

    const btn = document.createElement("button");
    btn.className = "easydl-video-btn";
    btn.title = "EasyDownload 동영상 화질별 다운로드";
    btn.innerHTML = `
      <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
        <polyline points="7 10 12 15 17 10"></polyline>
        <line x1="12" y1="15" x2="12" y2="3"></line>
      </svg>
    `;

    btn.onclick = (e) => {
      e.stopPropagation();
      e.preventDefault();
      openDropdown(btn);
    };

    document.body.appendChild(btn);
    activeTargets.push({ targetElement: target, buttonEl: btn });
    updateAllButtonsUI();
  }

  // Scan Top Window for <video> and Player <iframe> elements
  function scanPageTargets() {
    // 1. Direct videos
    const videos = document.querySelectorAll("video");
    videos.forEach(v => attachButtonToTarget(v));

    // 2. Player Iframes (e.g., embed, recordplay, player)
    const iframes = document.querySelectorAll("iframe");
    iframes.forEach(f => {
      if (isPlayerIframe(f)) {
        attachButtonToTarget(f);
      }
    });

    updatePositions();
  }

  // Update position of all buttons to stay STRICTLY OUTSIDE and RIGHT ABOVE the video / iframe
  function updatePositions() {
    for (let i = activeTargets.length - 1; i >= 0; --i) {
      const { targetElement, buttonEl } = activeTargets[i];
      if (!document.contains(targetElement)) {
        buttonEl.remove();
        activeTargets.splice(i, 1);
        continue;
      }

      const rect = targetElement.getBoundingClientRect();
      // If element is not displayed, too small, or completely scrolled off screen
      if (rect.width < 160 || rect.height < 90 || rect.bottom <= 40 || rect.top >= window.innerHeight) {
        buttonEl.style.display = "none";
        continue;
      }

      buttonEl.style.display = "inline-flex";

      // Positioning:
      // STRICTLY ABOVE the video element (rect.top - 34px)
      // Never render inside the video picture
      let top = rect.top - 34;
      if (top < 4 && rect.bottom > 50) {
        // If scrolled past top, clamp to 4px from viewport top
        top = 4;
      }

      let left = rect.right - 36; // 30px width + 6px right margin
      if (left < 6) left = 6;

      buttonEl.style.top = Math.round(top) + "px";
      buttonEl.style.left = Math.round(left) + "px";

      if (currentDropdown && currentDropdown.buttonEl === buttonEl) {
        positionDropdown(buttonEl, currentDropdown.menuEl);
      }
    }
  }

  // Event Listeners for smooth positioning and dismissal
  window.addEventListener("scroll", updatePositions, { passive: true });
  window.addEventListener("resize", updatePositions, { passive: true });
  setInterval(updatePositions, 350);

  // Close dropdown on click outside
  window.addEventListener("click", (e) => {
    if (currentDropdown) {
      if (!currentDropdown.menuEl.contains(e.target) && !currentDropdown.buttonEl.contains(e.target)) {
        closeDropdown();
      }
    }
  }, true);

  // Close on Escape key
  window.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && currentDropdown) {
      closeDropdown();
    }
  });

  // Listen for detected streams from background.js
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (msg.action === "streamDetected" && msg.stream) {
      addStream(msg.stream);
      scanPageTargets();
      updatePositions();
    }
  });

  // Initial query from background
  chrome.runtime.sendMessage({ action: "getVideos" }, (res) => {
    if (res && Array.isArray(res.videos)) {
      res.videos.forEach(s => addStream(s));
    }
    scanPageTargets();
  });

  // YouTube in-page video detection & real-time SPA navigation tracking
  if (location.hostname.includes("youtube.com") || location.hostname.includes("youtu.be")) {
    let lastYouTubeUrl = "";

    function checkYouTubeVideo() {
      const currentUrl = location.href;
      const isVideoPage = currentUrl.includes("/watch") || currentUrl.includes("/shorts/") || location.hostname.includes("youtu.be");

      if (!isVideoPage) {
        lastYouTubeUrl = currentUrl;
        return;
      }

      // If URL changed to a different video clip, completely reset previous cached streams!
      if (currentUrl !== lastYouTubeUrl) {
        detectedStreams = [];
        lastYouTubeUrl = currentUrl;
        chrome.runtime.sendMessage({ action: "resetTabVideos", url: currentUrl }).catch(() => {});
      }

      let ytTitle = document.title ? document.title.replace(/\s*-\s*YouTube\s*$/i, "").trim() : "YouTube 동영상";

      // Re-register only the current active video
      detectedStreams = [{
        url: currentUrl,
        title: ytTitle,
        type: "YouTube 최고 화질 (1080p/4K)",
        quality: "YouTube",
        size: "최고 화질"
      }];

      updateAllButtonsUI();
      scanPageTargets();
      updatePositions();

      // YouTube titles often update 200~500ms after SPA navigation
      setTimeout(() => {
        if (location.href === currentUrl && document.title) {
          let updatedTitle = document.title.replace(/\s*-\s*YouTube\s*$/i, "").trim();
          if (updatedTitle && detectedStreams.length > 0 && detectedStreams[0].url === currentUrl) {
            detectedStreams[0].title = updatedTitle;
          }
        }
      }, 500);
    }

    checkYouTubeVideo();
    window.addEventListener("yt-navigate-finish", checkYouTubeVideo);
    window.addEventListener("yt-page-data-updated", checkYouTubeVideo);
    window.addEventListener("popstate", checkYouTubeVideo);

    // Reliable fallback poll every 500ms in case YouTube SPA events are missed
    setInterval(() => {
      if (location.href !== lastYouTubeUrl) {
        checkYouTubeVideo();
      }
    }, 500);
  }

  // Boot DOM scan
  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", scanPageTargets);
  } else {
    scanPageTargets();
  }

  const observer = new MutationObserver(() => {
    scanPageTargets();
  });
  observer.observe(document.body || document.documentElement, {
    childList: true,
    subtree: true
  });
})();
