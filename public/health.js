"use strict";

(function () {
  if (window.__auctionHealthMonitorInstalled) return;
  window.__auctionHealthMonitorInstalled = true;

  const HEALTH_URL = "/api/health";
  const HEALTH_INTERVAL_MS = 30000;
  const HEALTH_TIMEOUT_MS = 5000;
  const LABELS = {
    online: "Server online",
    unavailable: "Server unavailable",
    lost: "Connection lost"
  };

  const indicator = document.querySelector("#server-status");
  const statusText = document.querySelector("#server-status-text");
  if (!indicator || !statusText) return;

  let currentState = null;
  let pendingTimer = null;
  let inFlight = false;

  function setState(state) {
    if (state === currentState) return;
    currentState = state;
    indicator.classList.remove("online", "unavailable", "lost");
    indicator.classList.add(state);
    statusText.textContent = LABELS[state];
  }

  async function checkHealth() {
    if (inFlight) return;
    inFlight = true;
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), HEALTH_TIMEOUT_MS);
    try {
      const response = await fetch(HEALTH_URL, {
        signal: controller.signal,
        headers: { Accept: "application/json" }
      });
      setState(response.ok ? "online" : "unavailable");
    } catch (error) {
      setState(error && error.name === "AbortError" ? "unavailable" : "lost");
    } finally {
      clearTimeout(timeoutId);
      inFlight = false;
      if (pendingTimer !== null) clearTimeout(pendingTimer);
      pendingTimer = setTimeout(checkHealth, HEALTH_INTERVAL_MS);
    }
  }

  addEventListener("pagehide", () => {
    if (pendingTimer !== null) clearTimeout(pendingTimer);
    pendingTimer = null;
  });

  window.auctionHealth = { state: () => currentState, checkNow: checkHealth };

  checkHealth();
})();