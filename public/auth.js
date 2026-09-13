"use strict";

const authStorageKey = "aurelian_auth_token";
let authUser = null;
let authMode = "login";
const authElements = {
  dialog: document.querySelector("#auth-dialog"), open: document.querySelector("#auth-open"),
  close: document.querySelector("#auth-close"), logout: document.querySelector("#auth-logout"),
  username: document.querySelector("#auth-username"), form: document.querySelector("#auth-form"),
  title: document.querySelector("#auth-title"), name: document.querySelector("#auth-name"),
  password: document.querySelector("#auth-password"), message: document.querySelector("#auth-message"),
  submit: document.querySelector("#auth-submit"), switchMode: document.querySelector("#auth-switch")
};

function token() { return localStorage.getItem(authStorageKey) || ""; }
function notify() { dispatchEvent(new CustomEvent("authchange", { detail: authUser })); }
function renderAuth() {
  const authenticated = authUser !== null;
  authElements.username.hidden = !authenticated; authElements.logout.hidden = !authenticated; authElements.open.hidden = authenticated;
  authElements.username.textContent = authenticated ? authUser.username : "";
  notify();
}
function openAuth() { authElements.message.textContent = ""; authElements.dialog.showModal(); authElements.name.focus(); }
function setMode(mode) {
  authMode = mode; const registering = mode === "register";
  authElements.title.textContent = registering ? "Create account" : "Login";
  authElements.submit.textContent = registering ? "Register" : "Login";
  authElements.switchMode.textContent = registering ? "Already registered? Login" : "Need an account? Register";
  authElements.password.autocomplete = registering ? "new-password" : "current-password";
  authElements.message.textContent = "";
}
async function authRequest(path, options = {}) {
  const response = await fetch(path, { ...options, headers: { Accept: "application/json", "Content-Type": "application/json", ...(options.headers || {}) } });
  let body = null; try { body = await response.json(); } catch { /* Logout intentionally has no body. */ }
  if (!response.ok) throw new Error(body?.error || `Request failed with status ${response.status}`);
  return body;
}
async function restoreAuth() {
  if (!token()) { renderAuth(); return; }
  try { authUser = await authRequest("/api/auth/me", { headers: { Authorization: `Bearer ${token()}` } }); }
  catch { localStorage.removeItem(authStorageKey); authUser = null; }
  renderAuth();
}

authElements.open.addEventListener("click", openAuth);
authElements.close.addEventListener("click", () => authElements.dialog.close());
authElements.switchMode.addEventListener("click", () => setMode(authMode === "login" ? "register" : "login"));
authElements.form.addEventListener("submit", async event => {
  event.preventDefault(); authElements.message.textContent = ""; authElements.submit.disabled = true;
  try {
    const result = await authRequest(`/api/auth/${authMode}`, { method: "POST", body: JSON.stringify({ username: authElements.name.value.trim(), password: authElements.password.value }) });
    localStorage.setItem(authStorageKey, result.token); authUser = result.user; authElements.form.reset(); authElements.dialog.close(); renderAuth();
  } catch (error) { authElements.message.textContent = error.message; }
  finally { authElements.submit.disabled = false; }
});
authElements.logout.addEventListener("click", async () => {
  try { await authRequest("/api/auth/logout", { method: "POST", headers: { Authorization: `Bearer ${token()}` } }); } catch { /* Local logout still removes unusable credentials. */ }
  localStorage.removeItem(authStorageKey); authUser = null; renderAuth();
});

window.auctionAuth = { token, user: () => authUser, open: openAuth };
setMode("login"); restoreAuth();
