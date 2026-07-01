/* QQMouse site behaviour: 3-state theme toggle + OS-aware download links.
   Vanilla JS, no dependencies. */

(function () {
  "use strict";

  var STORAGE_KEY = "qqmouse-theme";
  var root = document.documentElement;

  var REPO = "https://github.com/DebajyotiSaikia/QQMouse";
  var LATEST = REPO + "/releases/latest"; // parent: all latest downloads
  var WIN_URL = LATEST + "/download/QQMouse-Windows-x64-Setup.exe";
  var MAC_URL = LATEST + "/download/QQMouse-macOS.dmg";

  var darkMQ = window.matchMedia("(prefers-color-scheme: dark)");

  /* ---------------- Theme (system / light / dark) ---------------- */

  function storedTheme() {
    try {
      return localStorage.getItem(STORAGE_KEY) || "system";
    } catch (e) {
      return "system";
    }
  }

  function effectiveDark() {
    var mode = storedTheme();
    if (mode === "dark") return true;
    if (mode === "light") return false;
    return darkMQ.matches; // system
  }

  function updateThemeColor() {
    var meta = document.querySelector('meta[name="theme-color"]');
    if (meta)
      meta.setAttribute("content", effectiveDark() ? "#0d1117" : "#ffffff");
  }

  function reflectToggle(mode) {
    var buttons = document.querySelectorAll("[data-theme-option]");
    for (var i = 0; i < buttons.length; i++) {
      var on = buttons[i].getAttribute("data-theme-option") === mode;
      buttons[i].setAttribute("aria-pressed", on ? "true" : "false");
      buttons[i].classList.toggle("is-active", on);
    }
  }

  function applyTheme(mode) {
    if (mode === "light" || mode === "dark") {
      root.setAttribute("data-theme", mode);
    } else {
      root.removeAttribute("data-theme"); // system: let prefers-color-scheme drive
    }
    updateThemeColor();
    reflectToggle(mode);
  }

  function setTheme(mode) {
    try {
      localStorage.setItem(STORAGE_KEY, mode);
    } catch (e) {}
    applyTheme(mode);
  }

  // Keep the address-bar tint in sync when the OS theme changes in system mode.
  function onSystemChange() {
    if (storedTheme() === "system") updateThemeColor();
  }
  if (darkMQ.addEventListener)
    darkMQ.addEventListener("change", onSystemChange);
  else if (darkMQ.addListener) darkMQ.addListener(onSystemChange); // older Safari

  /* ---------------- OS detection for downloads ---------------- */

  function detectOS() {
    var uad = navigator.userAgentData;
    if (uad && uad.platform) {
      var p = uad.platform.toLowerCase();
      if (p.indexOf("win") !== -1) return "windows";
      if (p.indexOf("mac") !== -1) return "mac";
    }
    var ua = navigator.userAgent || "";
    var plat = navigator.platform || "";
    if (/Win/i.test(plat) || /Windows/i.test(ua)) return "windows";
    // Apple desktop + mobile (iPadOS often reports as "MacIntel" with touch points).
    if (
      /Mac/i.test(plat) ||
      /Mac OS X/i.test(ua) ||
      /iPhone|iPad|iPod/i.test(ua)
    )
      return "mac";
    if ((navigator.maxTouchPoints || 0) > 1 && /Macintosh/i.test(ua))
      return "mac";
    return "unknown";
  }

  function setupDownloads() {
    var primary = document.getElementById("primary-download");
    var label = document.getElementById("primary-download-label");
    var note = document.getElementById("detect-note");
    if (!primary || !label || !note) return;

    var os = detectOS();
    if (os === "windows") {
      primary.href = WIN_URL;
      label.textContent = "Download for Windows";
      note.textContent = "Detected Windows — this is the Windows installer.";
    } else if (os === "mac") {
      primary.href = MAC_URL;
      label.textContent = "Download for macOS";
      note.textContent = "Detected macOS — this is the macOS installer.";
    } else {
      // Can't determine: point at the parent release with every download.
      primary.href = LATEST;
      label.textContent = "View all downloads";
      note.textContent = "Couldn't detect your OS — pick your platform below.";
    }
  }

  /* ---------------- Wire up ---------------- */

  function init() {
    var buttons = document.querySelectorAll("[data-theme-option]");
    for (var i = 0; i < buttons.length; i++) {
      (function (btn) {
        btn.addEventListener("click", function () {
          setTheme(btn.getAttribute("data-theme-option"));
        });
      })(buttons[i]);
    }
    applyTheme(storedTheme());
    setupDownloads();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
