"use strict";

const PAGE_SIZE = 20;
const elements = Object.fromEntries([
  "form", "search", "category", "sort", "result-count", "category-notice", "loading-state",
  "error-state", "error-message", "empty-state", "lot-grid", "pagination", "previous-page",
  "next-page", "page-information", "retry-button", "clear-button"
].map(id => [id.replace(/-([a-z])/g, (_, letter) => letter.toUpperCase()), document.querySelector(`#${id}`)]));
elements.form = document.querySelector("#catalog-controls");
let state = readStateFromUrl();
let requestController = null;
let countdownTimer = null;
let searchTimer = null;

function readStateFromUrl() {
  const params = new URLSearchParams(location.search);
  const sortBy = ["current_price", "end_time"].includes(params.get("sort_by")) ? params.get("sort_by") : "end_time";
  const order = ["asc", "desc"].includes(params.get("order")) ? params.get("order") : "asc";
  const rawPage = params.get("page") ?? "";
  return {
    page: /^\d+$/.test(rawPage) && Number(rawPage) > 0 ? Number(rawPage) : 1,
    search: (params.get("search") ?? "").slice(0, 200),
    categoryId: /^[1-9]\d*$/.test(params.get("category_id") ?? "") ? params.get("category_id") : "",
    sortBy, order
  };
}

function writeStateToUrl() {
  const params = new URLSearchParams();
  if (state.page > 1) params.set("page", String(state.page));
  if (state.search) params.set("search", state.search);
  if (state.categoryId) params.set("category_id", state.categoryId);
  if (state.sortBy !== "end_time") params.set("sort_by", state.sortBy);
  if (state.order !== "asc") params.set("order", state.order);
  history.replaceState(null, "", params.size ? `/?${params}` : "/");
}

function syncControls() {
  elements.search.value = state.search;
  elements.category.value = state.categoryId;
  elements.sort.value = `${state.sortBy}:${state.order}`;
}

async function fetchJson(url, signal) {
  const response = await fetch(url, { headers: { Accept: "application/json" }, signal });
  let body = null;
  try { body = await response.json(); } catch { /* HTTP status still provides a useful error. */ }
  if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
  return body;
}

async function loadCategories() {
  try {
    const data = await fetchJson("/api/categories");
    const fragment = document.createDocumentFragment();
    for (const category of data.categories ?? []) {
      const option = document.createElement("option");
      option.value = String(category.id);
      option.textContent = category.name;
      fragment.append(option);
    }
    elements.category.append(fragment);
    syncControls();
  } catch {
    elements.categoryNotice.textContent = "Categories are temporarily unavailable. You can still browse all live lots.";
    elements.categoryNotice.hidden = false;
    elements.category.disabled = true;
  }
}

function showState(name, message = "") {
  elements.loadingState.hidden = name !== "loading";
  elements.errorState.hidden = name !== "error";
  elements.emptyState.hidden = name !== "empty";
  elements.lotGrid.hidden = name !== "results";
  elements.pagination.hidden = name !== "results";
  if (message) elements.errorMessage.textContent = message;
}

function formatMoney(cents) {
  return new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" }).format(Number(cents) / 100);
}

function formatCountdown(endTime) {
  const remaining = new Date(endTime).getTime() - Date.now();
  if (!Number.isFinite(remaining) || remaining <= 0) return "Auction ended";
  const seconds = Math.floor(remaining / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (days) return `${days}d ${hours}h ${minutes}m`;
  return [hours, minutes, seconds % 60].map(value => String(value).padStart(2, "0")).join(":");
}

function updateCountdowns() {
  for (const node of document.querySelectorAll("[data-end-time]")) {
    const label = formatCountdown(node.dataset.endTime);
    node.textContent = label;
    node.classList.toggle("ended", label === "Auction ended");
  }
}

function createLotCard(lot) {
  const article = document.createElement("article"); article.className = "lot-card";
  const link = document.createElement("a"); link.className = "card-link"; link.href = `/lot.html?id=${encodeURIComponent(lot.id)}`; link.setAttribute("aria-label", `View ${lot.title}`);
  const imageWrap = document.createElement("div"); imageWrap.className = "card-image-wrap";
  const image = document.createElement("img"); image.className = "card-image"; image.src = lot.image_url; image.alt = `Photo of ${lot.title}`; image.loading = "lazy"; image.width = 480; image.height = 320;
  image.addEventListener("error", () => image.classList.add("is-missing"), { once: true });
  const category = document.createElement("span"); category.className = "card-category"; category.textContent = lot.category?.name ?? "Uncategorized";
  imageWrap.append(image, category);
  const cardContent = document.createElement("div"); cardContent.className = "card-content";
  const title = document.createElement("h2"); title.className = "card-title"; title.textContent = lot.title;
  const meta = document.createElement("div"); meta.className = "card-meta";
  const priceGroup = document.createElement("div");
  const priceLabel = document.createElement("span"); priceLabel.className = "meta-label"; priceLabel.textContent = "Current price";
  const price = document.createElement("strong"); price.className = "card-price"; price.textContent = formatMoney(lot.current_price ?? lot.start_price);
  priceGroup.append(priceLabel, price);
  const timeGroup = document.createElement("div");
  const timeLabel = document.createElement("span"); timeLabel.className = "meta-label"; timeLabel.textContent = "Time left";
  const countdown = document.createElement("span"); countdown.className = "countdown"; countdown.dataset.endTime = lot.end_time;
  timeGroup.append(timeLabel, countdown); meta.append(priceGroup, timeGroup); cardContent.append(title, meta); link.append(imageWrap, cardContent); article.append(link);
  return article;
}

function renderLots(data) {
  const fragment = document.createDocumentFragment();
  for (const lot of data.lots ?? []) fragment.append(createLotCard(lot));
  elements.lotGrid.replaceChildren(fragment);
  const total = Number(data.total) || 0;
  const totalPages = Number(data.total_pages) || 0;
  const strong = document.createElement("strong"); strong.textContent = total.toLocaleString("en-US");
  elements.resultCount.replaceChildren(strong, document.createTextNode(total === 1 ? " live lot" : " live lots"));
  elements.pageInformation.textContent = `Page ${data.page} of ${Math.max(totalPages, 1)}`;
  elements.previousPage.disabled = data.page <= 1;
  elements.nextPage.disabled = !totalPages || data.page >= totalPages;
  if (!data.lots?.length) { showState("empty"); return; }
  showState("results");
  if (countdownTimer !== null) clearInterval(countdownTimer);
  updateCountdowns();
  countdownTimer = setInterval(updateCountdowns, 1000);
}

async function loadLots({ focusResults = false } = {}) {
  requestController?.abort(); requestController = new AbortController();
  showState("loading"); elements.resultCount.textContent = "Loading live lots…"; writeStateToUrl();
  const params = new URLSearchParams({ page: state.page, limit: PAGE_SIZE, sort_by: state.sortBy, order: state.order });
  if (state.search) params.set("search", state.search);
  if (state.categoryId) params.set("category_id", state.categoryId);
  try {
    renderLots(await fetchJson(`/api/lots?${params}`, requestController.signal));
    if (focusResults) elements.resultCount.scrollIntoView({ block: "nearest" });
  } catch (error) {
    if (error.name === "AbortError") return;
    elements.resultCount.textContent = "Catalog unavailable";
    showState("error", error.message || "Check your connection and try again.");
  }
}

elements.form.addEventListener("submit", event => event.preventDefault());
elements.search.addEventListener("input", () => { clearTimeout(searchTimer); searchTimer = setTimeout(() => { state.search = elements.search.value.trim(); state.page = 1; loadLots(); }, 350); });
elements.category.addEventListener("change", () => { state.categoryId = elements.category.value; state.page = 1; loadLots(); });
elements.sort.addEventListener("change", () => { [state.sortBy, state.order] = elements.sort.value.split(":"); state.page = 1; loadLots(); });
elements.previousPage.addEventListener("click", () => { state.page = Math.max(1, state.page - 1); loadLots({ focusResults: true }); });
elements.nextPage.addEventListener("click", () => { state.page += 1; loadLots({ focusResults: true }); });
elements.retryButton.addEventListener("click", () => loadLots());
elements.clearButton.addEventListener("click", () => { state = { page: 1, search: "", categoryId: "", sortBy: "end_time", order: "asc" }; syncControls(); loadLots(); });
addEventListener("popstate", () => { state = readStateFromUrl(); syncControls(); loadLots(); });
addEventListener("pagehide", () => { if (countdownTimer !== null) clearInterval(countdownTimer); requestController?.abort(); });

syncControls(); loadCategories(); loadLots();
