"use strict";

const loading = document.querySelector("#detail-loading");
const errorPanel = document.querySelector("#detail-error");
const content = document.querySelector("#detail-content");
const countdown = document.querySelector("#detail-countdown");
let countdownTimer = null;
let currentLot = null;
let currentId = null;

const money = cents => new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" }).format(Number(cents) / 100);

function updateBidAccess() {
  const authenticated = window.auctionAuth?.user() != null;
  document.querySelector("#login-required").hidden = authenticated;
  document.querySelector("#bid-form").hidden = !authenticated || currentLot?.status !== "active";
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

function remainingLabel(value) {
  const remaining = new Date(value).getTime() - Date.now();
  if (!Number.isFinite(remaining) || remaining <= 0) return "Auction ended";
  const seconds = Math.floor(remaining / 1000), days = Math.floor(seconds / 86400), hours = Math.floor((seconds % 86400) / 3600), minutes = Math.floor((seconds % 3600) / 60);
  return days ? `${days}d ${hours}h ${minutes}m` : [hours, minutes, seconds % 60].map(part => String(part).padStart(2, "0")).join(":");
}

function showError(title, message) {
  loading.hidden = true; content.hidden = true;
  document.querySelector("#detail-error-title").textContent = title;
  document.querySelector("#detail-error-message").textContent = message;
  errorPanel.hidden = false;
}

function updateCountdown(endTime) {
  const label = remainingLabel(endTime); countdown.textContent = label;
  const ended = label === "Auction ended"; countdown.classList.toggle("ended", ended);
  if (ended) {
    const status = document.querySelector("#detail-status"); status.textContent = "Auction ended"; status.classList.add("ended");
    if (currentLot) { currentLot.status = "ended"; updateBidAccess(); }
    if (countdownTimer !== null) clearInterval(countdownTimer);
  }
}

function renderLot(lot) {
  currentLot = lot;
  if (countdownTimer !== null) { clearInterval(countdownTimer); countdownTimer = null; }
  const image = document.querySelector("#detail-image"), fallback = document.querySelector("#detail-image-fallback");
  image.hidden = false; fallback.hidden = true; image.src = lot.image_url; image.alt = `Photo of ${lot.title}`;
  image.addEventListener("error", () => { image.hidden = true; fallback.hidden = false; }, { once: true });
  document.querySelector("#detail-category").textContent = lot.category?.name ?? "Uncategorized";
  document.querySelector("#detail-title").textContent = lot.title;
  document.querySelector("#detail-lot-number").textContent = `Lot #${lot.id}`;
  document.querySelector("#detail-price").textContent = money(lot.current_price ?? lot.start_price);
  document.querySelector("#detail-step").textContent = lot.minimum_step == null ? "Not specified" : money(lot.minimum_step);
  const end = new Date(lot.end_time);
  document.querySelector("#detail-end-time").textContent = Number.isFinite(end.getTime())
    ? new Intl.DateTimeFormat("en-US", {
        year: "numeric", month: "long", day: "numeric",
        hour: "numeric", minute: "2-digit", timeZoneName: "short"
      }).format(end)
    : "Date unavailable";
  document.querySelector("#detail-description").textContent = lot.description;
  document.querySelector("#minimum-bid").textContent = `Minimum next bid: ${money(lot.current_price + lot.minimum_step)}`;
  document.title = `${lot.title} — Aurelian Auctions`;
  loading.hidden = true; content.hidden = false;
  if (lot.status !== "active") updateCountdown("1970-01-01T00:00:00Z");
  else { updateCountdown(lot.end_time); countdownTimer = setInterval(() => updateCountdown(lot.end_time), 1000); }
  updateBidAccess();
}

async function loadLot() {
  const id = new URLSearchParams(location.search).get("id"); currentId = id;
  if (!/^[1-9]\d*$/.test(id ?? "")) { showError("Invalid lot address", "This link does not contain a valid positive lot ID."); return; }
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(id)}`, { headers: { Accept: "application/json" } });
    let body = null; try { body = await response.json(); } catch { /* HTTP status still provides a useful error. */ }
    if (response.status === 404) { showError("Lot not found", "This lot may have been removed or the address may be incorrect."); return; }
    if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
    renderLot(body); await loadBidHistory();
  } catch (error) { showError("Unable to load this lot", error.message || "Check your connection and try again."); }
}

document.querySelector("#bid-login").addEventListener("click", () => window.auctionAuth.open());
addEventListener("authchange", updateBidAccess);
document.querySelector("#bid-form").addEventListener("submit", async event => {
  event.preventDefault();
  const message = document.querySelector("#bid-message"), input = document.querySelector("#bid-amount");
  const value = input.value.trim();
  if (!/^\d+(\.\d{1,2})?$/.test(value)) { message.textContent = "Enter a valid USD amount with at most two decimals."; return; }
  const amount = Math.round(Number(value) * 100);
  if (!Number.isSafeInteger(amount) || amount <= 0) { message.textContent = "Enter a valid positive bid amount."; return; }
  const submit = event.submitter; submit.disabled = true; message.textContent = "Placing bid…";
  try {
    const response = await fetch(`/api/lots/${encodeURIComponent(currentId)}/bids`, {
      method: "POST", headers: { Accept: "application/json", "Content-Type": "application/json", Authorization: `Bearer ${window.auctionAuth.token()}` },
      body: JSON.stringify({ amount })
    });
    const body = await response.json();
    if (!response.ok) throw new Error(body.error || "Bid was not accepted");
    input.value = ""; message.textContent = "Your bid was accepted."; await loadLot();
  } catch (error) { message.textContent = error.message; }
  finally { submit.disabled = false; }
});
addEventListener("pagehide", () => { if (countdownTimer !== null) clearInterval(countdownTimer); });
loadLot();
