"use strict";

const recommendSection = document.querySelector("#recommendations");
const recommendTitle = document.querySelector("#recommendations-title");
const recommendCopy = document.querySelector("#recommendations-copy");
const recommendGrid = document.querySelector("#recommend-grid");

function formatRecommendMoney(cents) {
  return new Intl.NumberFormat("en-US", { style: "currency", currency: "USD" }).format(Number(cents) / 100);
}

function createRecommendCard(lot) {
  const article = document.createElement("article"); article.className = "lot-card recommend-card";
  const link = document.createElement("a"); link.className = "card-link"; link.href = `/lot.html?id=${encodeURIComponent(lot.id)}`; link.setAttribute("aria-label", `View ${lot.title}`);
  const imageWrap = document.createElement("div"); imageWrap.className = "card-image-wrap";
  const image = document.createElement("img"); image.className = "card-image"; image.src = lot.image_url; image.alt = `Photo of ${lot.title}`; image.loading = "lazy"; image.width = 480; image.height = 320;
  image.addEventListener("error", () => image.classList.add("is-missing"), { once: true });
  imageWrap.append(image);
  const cardContent = document.createElement("div"); cardContent.className = "card-content";
  const title = document.createElement("h3"); title.className = "card-title"; title.textContent = lot.title;
  const meta = document.createElement("div"); meta.className = "card-meta";
  const price = document.createElement("strong"); price.className = "card-price"; price.textContent = formatRecommendMoney(lot.current_price ?? lot.start_price);
  meta.append(price); cardContent.append(title, meta); link.append(imageWrap, cardContent); article.append(link);
  return article;
}

let lastFetchToken = null;

function showLoadingState() {
  recommendCopy.textContent = "Loading recommendations…";
  recommendGrid.replaceChildren();
  recommendSection.hidden = false;
}

async function loadRecommendations() {
  const token = window.auctionAuth?.token() ?? "";
  if (token === lastFetchToken) return;
  lastFetchToken = token;
  showLoadingState();
  try {
    const response = await fetch("/api/recommendations?limit=8", {
      headers: { Accept: "application/json", ...(token ? { Authorization: `Bearer ${token}` } : {}) }
    });
    let body = null;
    try { body = await response.json(); } catch { /* Keep the partial body null. */ }
    if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
    if (!body.lots?.length) { recommendSection.hidden = true; return; }
    const fragment = document.createDocumentFragment();
    for (const lot of body.lots) fragment.append(createRecommendCard(lot));
    recommendGrid.replaceChildren(fragment);
    recommendTitle.textContent = body.personalized ? "Recommended for you" : "Trending lots";
    recommendCopy.textContent = body.personalized
      ? "Fresh suggestions based on the lots you’ve been browsing."
      : "A quick look at what other collectors are watching.";
    recommendSection.hidden = false;
  } catch {
    recommendSection.hidden = true;
  }
}

addEventListener("authchange", () => loadRecommendations());
loadRecommendations();