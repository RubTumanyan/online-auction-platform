"use strict";

const loading = document.querySelector("#detail-loading");
const errorPanel = document.querySelector("#detail-error");
const content = document.querySelector("#detail-content");
const countdown = document.querySelector("#detail-countdown");
const liveStatus = document.querySelector("#live-status");
const bidInput = document.querySelector("#bid-amount");
const bidSubmit = document.querySelector("#bid-submit");
let countdownTimer = null;
let closureRefreshTimer = null;
let liveSocket = null;
let socketClosing = false;
let reconnectAttempts = 0;
let closureRefreshAttempts = 0;
let awaitingClosure = false;
let bidPending = false;
let currentLot = null;
let currentId = null;
const pendingEvents = [];

const money = cents => new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" }).format(Number(cents) / 100);

function setLiveStatus(message, unavailable = false) {
  liveStatus.textContent = message;
  liveStatus.classList.toggle("unavailable", unavailable);
}

function minimumNextBid() {
  if (!currentLot) return null;
  return Number(currentLot.current_price) + Number(currentLot.minimum_step);
}

function updateBidMinimum(value = minimumNextBid()) {
  if (!Number.isSafeInteger(value)) return;
  document.querySelector("#minimum-bid").textContent = `Minimum next bid: ${money(value)}`;
  bidInput.min = (value / 100).toFixed(2);
}

function updateBidAccess() {
  const user = window.auctionAuth?.user() ?? null;
  const authenticated = user != null;
  const verified = user?.emailVerified === true;
  const accepting = currentLot?.status === "active" && !awaitingClosure;
  document.querySelector("#login-required").hidden = authenticated || !accepting;
  document.querySelector("#verify-required").hidden = !authenticated || verified || !accepting;
  document.querySelector("#bid-form").hidden = !authenticated || !verified || !accepting;
  bidInput.disabled = !accepting;
  bidSubmit.disabled = !accepting || bidPending;
}

function stopLiveUpdates() {
  socketClosing = true;
  if (liveSocket) {
    liveSocket.close(1000, "Page closed");
    liveSocket = null;
  }
}

async function loadBidHistory() {
  if (!currentId) return;
  const state = document.querySelector("#history-state"), list = document.querySelector("#bid-list");
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}/bids?page=1&pageSize=10`, { headers: { Accept: "application/json" } });
    const body = await response.json();
    if (!response.ok) throw new Error(body.error || "Unable to load bids");
    list.replaceChildren();
    for (const bid of body.items) {
      const item = document.createElement("li"), bidder = document.createElement("strong"), details = document.createElement("span");
      bidder.textContent = bid.bidderUsername;
      details.textContent = `${money(bid.amount)} · ${new Date(bid.createdAt).toLocaleString()}`;
      item.append(bidder, details); list.append(item);
    }
    state.textContent = body.total ? `${body.total} bid${body.total === 1 ? "" : "s"}` : "No bids yet. Be the first.";
  } catch (error) { state.textContent = error.message; }
}

function remainingMilliseconds(value) { return new Date(value).getTime() - Date.now(); }

function remainingLabel(value) {
  const remaining = remainingMilliseconds(value);
  if (!Number.isFinite(remaining) || remaining <= 0) return "Closing…";
  const seconds = Math.floor(remaining / 1000), days = Math.floor(seconds / 86400), hours = Math.floor((seconds % 86400) / 3600), minutes = Math.floor((seconds % 3600) / 60);
  return days ? `${days}d ${hours}h ${minutes}m` : [hours, minutes, seconds % 60].map(part => String(part).padStart(2, "0")).join(":");
}

function showError(title, message) {
  stopLiveUpdates();
  loading.hidden = true; content.hidden = true;
  document.querySelector("#detail-error-title").textContent = title;
  document.querySelector("#detail-error-message").textContent = message;
  errorPanel.hidden = false;
}

function finalResult(price, winnerUsername) {
  const result = document.querySelector("#final-result");
  result.textContent = winnerUsername
    ? `Final price: ${money(price)} · Winner: ${winnerUsername}`
    : "No bids were placed. No winner.";
  result.hidden = false;
}

function applyClosedState(price, winnerUsername, closedAt = null) {
  if (!currentLot) return;
  currentLot.status = "closed";
  document.querySelector("#bid-title").textContent = "Auction result";
  document.querySelector("#detail-price-label").textContent = "Final price";
  document.querySelector("#bid-message").textContent = "";
  currentLot.current_price = price;
  currentLot.winnerUsername = winnerUsername;
  currentLot.closedAt = closedAt;
  awaitingClosure = false;
  document.querySelector("#detail-status").textContent = "Auction closed";
  document.querySelector("#detail-status").classList.add("ended");
  document.querySelector("#detail-price").textContent = money(price);
  countdown.textContent = "Auction closed";
  countdown.classList.add("ended");
  if (countdownTimer !== null) { clearInterval(countdownTimer); countdownTimer = null; }
  if (closureRefreshTimer !== null) { clearTimeout(closureRefreshTimer); closureRefreshTimer = null; }
  updateBidAccess();
  finalResult(price, winnerUsername);
}

async function refreshAuthoritativeClosure() {
  closureRefreshTimer = null;
  if (!currentId || currentLot?.status === "closed") return;
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}`, { headers: { Accept: "application/json" } });
    const body = await response.json();
    if (response.ok && body.status === "closed") {
      applyClosedState(body.currentPrice ?? body.current_price, body.winnerUsername ?? null, body.closedAt ?? null);
      await loadBidHistory();
      return;
    }
  } catch { /* The existing live-status message explains degraded updates. */ }
  closureRefreshAttempts += 1;
  if (closureRefreshAttempts < 3) closureRefreshTimer = setTimeout(refreshAuthoritativeClosure, 1500);
  else setLiveStatus("Live updates unavailable; refresh the page", true);
}

function updateCountdown(endTime) {
  if (currentLot?.status === "closed") return;
  const label = remainingLabel(endTime);
  countdown.textContent = label;
  if (label !== "Closing…") return;
  awaitingClosure = true;
  countdown.classList.add("ended");
  document.querySelector("#detail-status").textContent = "Closing…";
  updateBidAccess();
  if (countdownTimer !== null) { clearInterval(countdownTimer); countdownTimer = null; }
  if (closureRefreshTimer === null) closureRefreshTimer = setTimeout(refreshAuthoritativeClosure, 1200);
}

function renderLot(lot) {
  currentLot = lot;
  awaitingClosure = false;
  closureRefreshAttempts = 0;
  if (countdownTimer !== null) { clearInterval(countdownTimer); countdownTimer = null; }
  const image = document.querySelector("#detail-image"), fallback = document.querySelector("#detail-image-fallback");
  image.hidden = false; fallback.hidden = true; image.src = lot.image_url; image.alt = `Photo of ${lot.title}`;
  image.addEventListener("error", () => { image.hidden = true; fallback.hidden = false; }, { once: true });
  document.querySelector("#detail-category").textContent = lot.category?.name ?? "Uncategorized";
  document.querySelector("#detail-title").textContent = lot.title;
  document.querySelector("#detail-lot-number").textContent = `Lot #${lot.id}`;
  document.querySelector("#detail-price").textContent = money(lot.currentPrice ?? lot.current_price ?? lot.start_price);
  document.querySelector("#detail-step").textContent = lot.minimum_step == null ? "Not specified" : money(lot.minimum_step);
  const endValue = lot.endsAt ?? lot.end_time;
  const end = new Date(endValue);
  document.querySelector("#detail-end-time").textContent = Number.isFinite(end.getTime())
    ? new Intl.DateTimeFormat("en-US", { year: "numeric", month: "long", day: "numeric", hour: "numeric", minute: "2-digit", timeZoneName: "short" }).format(end)
    : "Date unavailable";
  document.querySelector("#detail-description").textContent = lot.description;
  updateBidMinimum();
  document.title = `${lot.title} — QuickBid`;
  loading.hidden = true; content.hidden = false;
  if (lot.status === "closed") applyClosedState(lot.currentPrice ?? lot.current_price, lot.winnerUsername ?? null, lot.closedAt ?? null);
  else {
    document.querySelector("#detail-status").textContent = "Live auction";
    updateCountdown(endValue);
    if (!awaitingClosure) countdownTimer = setInterval(() => updateCountdown(endValue), 1000);
  }
  updateBidAccess();
  while (pendingEvents.length) handleLiveEvent(pendingEvents.shift());
}

function handleLiveEvent(event) {
  if (!event || event.lotId !== Number(currentId)) return;
  if (!currentLot) { pendingEvents.push(event); return; }
  if (event.type === "bid_updated" && currentLot.status === "active") {
    // HTTP replies and broadcasts may arrive in a different order than commits.
    if (event.currentPrice < currentLot.current_price) return;
    currentLot.current_price = event.currentPrice;
    currentLot.winnerUsername = event.bid?.bidderUsername ?? null;
    document.querySelector("#detail-price").textContent = money(event.currentPrice);
    updateBidMinimum(event.minimumNextBid);
    setLiveStatus("New bid received");
    loadBidHistory();
  } else if (event.type === "lot_closed") {
    applyClosedState(event.currentPrice, event.winnerUsername ?? null, event.closedAt ?? null);
    setLiveStatus("Final auction result received");
    loadBidHistory();
  }
}

function connectLiveUpdates() {
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  socketClosing = false;
  const socket = new WebSocket(`${protocol}//${location.host}/ws/lots/${encodeURIComponent(currentId)}`);
  liveSocket = socket;
  socket.addEventListener("open", async () => {
    setLiveStatus("Live updates connected");
    // Subscribe first, then reconcile bids or closure missed during a disconnect
    // (or between the initial detail request and the WebSocket handshake).
    try {
      const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}`, { headers: { Accept: "application/json" } });
      if (!response.ok) throw new Error("Unable to refresh lot");
      const lot = await response.json();
      if (!currentLot) {
        pendingEvents.push(lot.status === "closed"
          ? { type: "lot_closed", lotId: Number(currentId), currentPrice: lot.currentPrice ?? lot.current_price, winnerUsername: lot.winnerUsername, closedAt: lot.closedAt }
          : { type: "bid_updated", lotId: Number(currentId), currentPrice: lot.currentPrice ?? lot.current_price, minimumNextBid: (lot.currentPrice ?? lot.current_price) + lot.minimum_step });
        return;
      }
      if (lot.status === "closed") {
        applyClosedState(lot.currentPrice ?? lot.current_price, lot.winnerUsername ?? null, lot.closedAt ?? null);
      } else if (currentLot.status === "active") {
        currentLot.current_price = Math.max(currentLot.current_price, lot.currentPrice ?? lot.current_price);
        document.querySelector("#detail-price").textContent = money(currentLot.current_price);
        updateBidMinimum();
      }
      await loadBidHistory();
      if (liveSocket === socket) reconnectAttempts = 0;
    } catch { setLiveStatus("Live updates unavailable; refresh the page", true); }
  });
  socket.addEventListener("message", message => {
    try { handleLiveEvent(JSON.parse(message.data)); }
    catch { setLiveStatus("Live updates unavailable; refresh the page", true); }
  });
  socket.addEventListener("error", () => { /* The close event owns the user-facing fallback. */ });
  socket.addEventListener("close", () => {
    if (liveSocket === socket) liveSocket = null;
    if (socketClosing) return;
    if (reconnectAttempts < 5) {
      reconnectAttempts += 1;
      setLiveStatus("Reconnecting live updates…", true);
      setTimeout(() => { if (!socketClosing) connectLiveUpdates(); }, Math.min(1000 * 2 ** (reconnectAttempts - 1), 8000));
    } else setLiveStatus("Live updates unavailable; refresh the page", true);
  });
}

const recordedViews = new Set();

function recordView(lotId) {
  const token = window.auctionAuth?.token();
  if (!token || recordedViews.has(lotId)) return;
  recordedViews.add(lotId);
  fetch(`/api/lots/${encodeURIComponent(lotId)}/view`, {
    method: "POST",
    headers: { Accept: "application/json", Authorization: `Bearer ${token}` }
  }).catch(() => recordedViews.delete(lotId));
}

async function loadLot() {
  currentId = new URLSearchParams(location.search).get("id");
  if (!/^[1-9]\d*$/.test(currentId ?? "")) { showError("Invalid lot address", "This link does not contain a valid positive lot ID."); return; }
  connectLiveUpdates();
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}`, { headers: { Accept: "application/json" } });
    let body = null; try { body = await response.json(); } catch { /* HTTP status still provides a useful error. */ }
    if (response.status === 404) { showError("Lot not found", "This lot may have been removed or the address may be incorrect."); return; }
    if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
    renderLot(body); await loadBidHistory(); recordView(body.id ?? Number(currentId));
  } catch (error) { showError("Unable to load this lot", error.message || "Check your connection and try again."); }
}

document.querySelector("#bid-login").addEventListener("click", () => window.auctionAuth.open());
addEventListener("authchange", updateBidAccess);
document.querySelector("#bid-form").addEventListener("submit", async event => {
  event.preventDefault();
  if (bidPending) return;
  const message = document.querySelector("#bid-message");
  const value = bidInput.value.trim();
  if (!/^\d+(\.\d{1,2})?$/.test(value)) { message.textContent = "Enter a valid USD amount with at most two decimals."; return; }
  const amount = Math.round(Number(value) * 100);
  if (!Number.isSafeInteger(amount) || amount <= 0) { message.textContent = "Enter a valid positive bid amount."; return; }
  bidPending = true; bidSubmit.disabled = true; message.textContent = "Placing bid…";
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}/bids`, {
      method: "POST", headers: { Accept: "application/json", "Content-Type": "application/json", Authorization: `Bearer ${window.auctionAuth.token()}` },
      body: JSON.stringify({ amount })
    });
    const body = await response.json();
    if (!response.ok) throw new Error(body.error || "Bid was not accepted");
    bidInput.value = ""; message.textContent = "Your bid was accepted.";
    if (currentLot.status === "active") {
      currentLot.current_price = Math.max(currentLot.current_price, body.currentPrice);
      document.querySelector("#detail-price").textContent = money(currentLot.current_price);
      updateBidMinimum();
    }
    await loadBidHistory();
  } catch (error) { message.textContent = error.message; }
  finally { bidPending = false; updateBidAccess(); }
});
addEventListener("pagehide", () => {
  stopLiveUpdates();
  if (countdownTimer !== null) clearInterval(countdownTimer);
  if (closureRefreshTimer !== null) clearTimeout(closureRefreshTimer);
});
loadLot();
