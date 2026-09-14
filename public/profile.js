"use strict";

const profile = {
  identity: document.querySelector("#profile-identity"),
  stats: document.querySelector("#profile-stats"),
  loading: document.querySelector("#profile-loading"),
  error: document.querySelector("#profile-error"),
  errorMessage: document.querySelector("#profile-error-message"),
  retry: document.querySelector("#profile-retry"),
  empty: document.querySelector("#profile-empty"),
  emptyTitle: document.querySelector("#profile-empty-title"),
  emptyCopy: document.querySelector("#profile-empty-copy"),
  summary: document.querySelector("#profile-summary"),
  grid: document.querySelector("#profile-grid"),
  tabs: Array.from(document.querySelectorAll(".profile-tab"))
};
let activeFilter = "active";

function token() { return window.auctionAuth?.token() ?? ""; }

async function apiJson(path) {
  const response = await fetch(path, {
    headers: { Accept: "application/json", Authorization: `Bearer ${token()}` }
  });
  let body = null;
  try { body = await response.json(); } catch { /* HTTP status still provides a useful error. */ }
  if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
  return body;
}

function money(cents) {
  return new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" }).format(Number(cents) / 100);
}

function countdown(endTime) {
  const remaining = new Date(endTime).getTime() - Date.now();
  if (!Number.isFinite(remaining) || remaining <= 0) return "Auction ended";
  const seconds = Math.floor(remaining / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (days) return `${days}d ${hours}h ${minutes}m`;
  return `${String(hours).padStart(2, "0")}:${String(minutes).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
}

function badgeFor(item) {
  if (item.participationStatus === "won") return { label: "Won", css: "is-won" };
  if (item.participationStatus === "lost") return { label: "Lost", css: "is-lost" };
  return item.isLeading ? { label: "You’re winning", css: "is-leading" } : { label: "Outbid", css: "is-outbid" };
}

function createProfileCard(item) {
  const lot = item;
  const article = document.createElement("article"); article.className = "lot-card";
  const link = document.createElement("a"); link.className = "card-link"; link.href = `/lot.html?id=${encodeURIComponent(lot.id)}`; link.setAttribute("aria-label", `View ${lot.title}`);
  const imageWrap = document.createElement("div"); imageWrap.className = "card-image-wrap";
  const image = document.createElement("img"); image.className = "card-image"; image.src = lot.image_url; image.alt = `Photo of ${lot.title}`; image.loading = "lazy"; image.width = 480; image.height = 320;
  image.addEventListener("error", () => image.classList.add("is-missing"), { once: true });
  const category = document.createElement("span"); category.className = "card-category"; category.textContent = lot.category?.name ?? "Uncategorized";
  const badge = badgeFor(item);
  const status = document.createElement("span"); status.className = `card-badge ${badge.css}`; status.textContent = badge.label;
  imageWrap.append(image, category, status);
  const cardContent = document.createElement("div"); cardContent.className = "card-content";
  const title = document.createElement("h2"); title.className = "card-title"; title.textContent = lot.title;
  const meta = document.createElement("div"); meta.className = "card-meta";
  const priceGroup = document.createElement("div");
  const priceLabel = document.createElement("span"); priceLabel.className = "meta-label"; priceLabel.textContent = "Current price";
  const price = document.createElement("strong"); price.className = "card-price"; price.textContent = money(lot.current_price ?? lot.start_price);
  priceGroup.append(priceLabel, price);
  const yourGroup = document.createElement("div");
  const yourLabel = document.createElement("span"); yourLabel.className = "meta-label"; yourLabel.textContent = "Your high bid";
  const yourBid = document.createElement("span"); yourBid.className = "your-bid"; yourBid.textContent = item.userHighestBid ? money(item.userHighestBid) : "—";
  yourGroup.append(yourLabel, yourBid);
  const timeGroup = document.createElement("div");
  const timeLabel = document.createElement("span"); timeLabel.className = "meta-label"; timeLabel.textContent = lot.participationStatus === "active" ? "Time left" : "Closed";
  const time = document.createElement("span"); time.className = "profile-time"; time.textContent = lot.participationStatus === "active" ? countdown(lot.end_time) : (lot.closedAt ? new Date(lot.closedAt).toLocaleString("en-US", { year: "numeric", month: "short", day: "numeric", hour: "numeric", minute: "2-digit" }) : "");
  timeGroup.append(timeLabel, time);
  meta.append(priceGroup, yourGroup, timeGroup);
  cardContent.append(title, meta); link.append(imageWrap, cardContent); article.append(link);
  return article;
}

function show(name) {
  profile.loading.hidden = name !== "loading";
  profile.error.hidden = name !== "error";
  profile.empty.hidden = name !== "empty";
  profile.grid.hidden = name !== "results";
  profile.summary.hidden = name !== "results" && name !== "empty";
}

function setEmptyCopy(filter) {
  if (filter === "active") { profile.emptyTitle.textContent = "No active bids"; profile.emptyCopy.textContent = "When you place a bid on a live lot it will show up here."; }
  else if (filter === "won") { profile.emptyTitle.textContent = "Nothing won yet"; profile.emptyCopy.textContent = "Auctions you win will be collected here."; }
  else { profile.emptyTitle.textContent = "No bid history"; profile.emptyCopy.textContent = "Bids you place across all lots will appear here."; }
}

async function loadParticipations() {
  show("loading");
  try {
    const data = await apiJson(`/api/profile/auctions?filter=${encodeURIComponent(activeFilter)}`);
    const items = data.items ?? [];
    if (!items.length) {
      setEmptyCopy(activeFilter);
      profile.summary.hidden = true;
      show("empty");
      return;
    }
    const fragment = document.createDocumentFragment();
    for (const item of items) fragment.append(createProfileCard(item));
    profile.grid.replaceChildren(fragment);
    const count = items.length;
    profile.summary.textContent = `${count} ${count === 1 ? "lot" : "lots"} shown.`;
    show("results");
  } catch (error) {
    profile.errorMessage.textContent = error.message;
    show("error");
  }
}

async function loadSummary() {
  try {
    const data = await apiJson("/api/profile/summary");
    const user = data.user ?? {};
    profile.identity.textContent = `${user.username || ""}${user.email ? ` · ${user.email}` : ""}`;
    profile.identity.hidden = !user.username;
    const stats = data.stats ?? {};
    profile.stats.hidden = false;
    profile.stats.querySelector('[data-stat="active"]').textContent = String(stats.activeParticipations ?? 0);
    profile.stats.querySelector('[data-stat="won"]').textContent = String(stats.wonAuctions ?? 0);
    profile.stats.querySelector('[data-stat="total"]').textContent = String(stats.totalParticipations ?? 0);
  } catch { /* The participations call reports failures. */ }
}

for (const tab of profile.tabs) {
  tab.addEventListener("click", () => {
    activeFilter = tab.dataset.filter;
    for (const other of profile.tabs) {
      other.classList.toggle("is-active", other === tab);
      other.setAttribute("aria-selected", other === tab ? "true" : "false");
    }
    loadParticipations();
  });
}
profile.retry.addEventListener("click", loadParticipations);

function render() {
  const user = window.auctionAuth?.user?.() ?? null;
  if (!user) {
    profile.identity.hidden = true;
    profile.stats.hidden = true;
    profile.summary.hidden = true;
    profile.grid.replaceChildren();
    setEmptyCopy("active");
    profile.emptyTitle.textContent = "Log in to see your bids";
    profile.emptyCopy.textContent = "Sign in to view the lots you’re bidding on.";
    const browse = profile.empty.querySelector("a");
    browse.textContent = "Back to the catalog";
    browse.href = "/";
    show("empty");
    return;
  }
  loadSummary();
  loadParticipations();
}

addEventListener("authchange", () => render());
render();