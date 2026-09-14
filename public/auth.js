"use strict";

const authStorageKey = "aurelian_auth_token";
let authUser = null;
let authMode = "login";
const authElements = {
  dialog: document.querySelector("#auth-dialog"), open: document.querySelector("#auth-open"),
  close: document.querySelector("#auth-close"), logout: document.querySelector("#auth-logout"),
  username: document.querySelector("#auth-username"), form: document.querySelector("#auth-form"),
  title: document.querySelector("#auth-title"), name: document.querySelector("#auth-name"),
  email: document.querySelector("#auth-email"), emailLabel: document.querySelector("#auth-email-label"),
  password: document.querySelector("#auth-password"), message: document.querySelector("#auth-message"),
  submit: document.querySelector("#auth-submit"), switchMode: document.querySelector("#auth-switch")
};
const verification = {
  panel: document.querySelector("#verification-panel"), email: document.querySelector("#verify-email-label"),
  form: document.querySelector("#verify-form"), code: document.querySelector("#verify-code"),
  submit: document.querySelector("#verify-submit"), resend: document.querySelector("#verify-resend"),
  message: document.querySelector("#verify-message"), cooldown: document.querySelector("#verify-cooldown")
};
let verificationCountdownTimer = null;
let verificationCooldownUntil = 0;

function token() { return localStorage.getItem(authStorageKey) || ""; }
const profileNav = document.querySelector("#nav-profile");
function notify() { dispatchEvent(new CustomEvent("authchange", { detail: authUser })); }
function renderAuth() {
  const authenticated = authUser !== null;
  authElements.username.hidden = !authenticated; authElements.logout.hidden = !authenticated; authElements.open.hidden = authenticated;
  authElements.username.textContent = authenticated ? authUser.username : "";
  if (profileNav) profileNav.hidden = !authenticated;
  if (!authenticated) hideVerification();
  else if (!authUser.emailVerified) showVerification();
  else hideVerification();
  notify();
}
function openAuth() {
  if (!authElements.dialog) { console.error("Auth dialog not found"); return; }
  authElements.message.textContent = "";
  try { authElements.dialog.showModal(); } catch (e) { console.error("Dialog showModal failed:", e); }
  authElements.name.focus();
}
function setMode(mode) {
  authMode = mode; const registering = mode === "register";
  authElements.title.textContent = registering ? "Create account" : "Login";
  authElements.submit.textContent = registering ? "Register" : "Login";
  authElements.switchMode.textContent = registering ? "Already registered? Login" : "Need an account? Register";
  authElements.password.autocomplete = registering ? "new-password" : "current-password";
  authElements.email.hidden = !registering; authElements.emailLabel.hidden = !registering;
  authElements.email.required = registering;
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

function clearVerificationCooldown() {
  if (verificationCountdownTimer !== null) { clearInterval(verificationCountdownTimer); verificationCountdownTimer = null; }
  verification.cooldown.hidden = true;
  verification.resend.disabled = false;
}
function startVerificationCooldown(seconds) {
  clearVerificationCooldown();
  verificationCooldownUntil = Date.now() + seconds * 1000;
  verification.resend.disabled = true;
  const tick = () => {
    const remaining = Math.max(0, Math.ceil((verificationCooldownUntil - Date.now()) / 1000));
    if (remaining <= 0) { clearVerificationCooldown(); return; }
    verification.cooldown.hidden = false;
    verification.cooldown.textContent = `You can request another code in ${remaining}s.`;
  };
  tick();
  verificationCountdownTimer = setInterval(tick, 1000);
}
function showVerification(notice = "") {
  if (!authUser || authUser.emailVerified) return;
  verification.email.textContent = authUser.email || "";
  verification.panel.hidden = false;
  if (notice) verification.message.textContent = notice;
}
function hideVerification() {
  verification.panel.hidden = true;
  verification.message.textContent = "";
  verification.code.value = "";
  clearVerificationCooldown();
}
function verificationRequest(path, payload) {
  return fetch(path, { method: "POST", headers: { Accept: "application/json", "Content-Type": "application/json" }, body: JSON.stringify(payload) })
    .then(async response => {
      let body = null; try { body = await response.json(); } catch { /* Keep the partial body null. */ }
      if (!response.ok) throw Object.assign(new Error(body?.error || `Request failed with status ${response.status}`), { status: response.status, body });
      return body;
    });
}

authElements.open.addEventListener("click", openAuth);
authElements.close.addEventListener("click", () => authElements.dialog.close());
authElements.switchMode.addEventListener("click", () => setMode(authMode === "login" ? "register" : "login"));
authElements.form.addEventListener("submit", async event => {
  event.preventDefault();
  if (!authElements.message) { console.error("Auth message element not found"); return; }
  authElements.message.textContent = "";
  authElements.submit.disabled = true;
  try {
    const payload = { username: authElements.name.value.trim(), password: authElements.password.value };
    if (authMode === "register") payload.email = authElements.email.value.trim();
    const result = await authRequest(`/api/auth/${authMode}`, { method: "POST", body: JSON.stringify(payload) });
    localStorage.setItem(authStorageKey, result.token); authUser = result.user; authElements.form.reset(); authElements.dialog.close(); renderAuth();
    if (authUser && !authUser.emailVerified) {
      const devMode = result.emailDeliveryMode === "dev" && result.devCode;
      showVerification(result.emailDeliveryFailed ? "We couldn't email your verification code. Use Resend code." : "");
      if (devMode) verification.message.textContent += ` Demo mode: your code is ${result.devCode}.`;
      if (!result.emailDeliveryFailed) startVerificationCooldown(60);
    }
  } catch (error) { authElements.message.textContent = error.message; }
  finally { authElements.submit.disabled = false; }
});
authElements.logout.addEventListener("click", async () => {
  try { await authRequest("/api/auth/logout", { method: "POST", headers: { Authorization: `Bearer ${token()}` } }); } catch { /* Local logout still removes unusable credentials. */ }
  localStorage.removeItem(authStorageKey); authUser = null; renderAuth();
});
verification.form.addEventListener("submit", async event => {
  event.preventDefault(); verification.message.textContent = "";
  if (!authUser?.email) return;
  verification.submit.disabled = true;
  try {
    const result = await verificationRequest("/api/auth/verify-email", { email: authUser.email, code: verification.code.value.trim() });
    authUser = { ...authUser, emailVerified: true };
    verification.code.value = "";
    verification.message.textContent = result.message || "Email verified. You can now bid.";
    clearVerificationCooldown();
    notify();
    setTimeout(hideVerification, 3000);
  } catch (error) {
    verification.code.value = "";
    verification.message.textContent = error.message;
  } finally { verification.submit.disabled = false; }
});
verification.resend.addEventListener("click", async () => {
  if (verification.resend.disabled) return;
  verification.message.textContent = "";
  verification.resend.disabled = true;
  if (!authUser?.email) return;
  try {
    const result = await verificationRequest("/api/auth/resend-verification", { email: authUser.email });
    verification.code.value = "";
    verification.message.textContent = result.message || "A new code was sent.";
    startVerificationCooldown(60);
  } catch (error) {
    if (error.body?.retryAfterSeconds) startVerificationCooldown(error.body.retryAfterSeconds);
    verification.message.textContent = error.message;
  } finally { if (verificationCountdownTimer === null) verification.resend.disabled = false; }
});
addEventListener("pagehide", () => { if (verificationCountdownTimer !== null) clearInterval(verificationCountdownTimer); });

window.auctionAuth = { token, user: () => authUser, open: openAuth };
setMode("login"); restoreAuth();