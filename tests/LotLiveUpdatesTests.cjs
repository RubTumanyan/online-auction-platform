// Run from the repository root: node tests/LotLiveUpdatesTests.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const elements = new Map();
function element(selector) {
  if (!elements.has(selector)) elements.set(selector, { textContent: '', value: '', handlers: {}, classList: { add() {}, toggle() {} }, addEventListener(type, handler) { this.handlers[type] = handler; }, replaceChildren() {}, append() {} });
  return elements.get(selector);
}
let open;
let snapshot;
const context = vm.createContext({
  document: { querySelector: element, createElement: () => element(Symbol()) },
  window: { auctionAuth: { user: () => null } },
  location: { protocol: 'http:', host: 'localhost' },
  addEventListener() {}, setTimeout() {}, clearTimeout() {}, setInterval() {}, clearInterval() {},
  WebSocket: class { addEventListener(type, handler) { if (type === 'open') open = handler; } close() {} },
  fetch: async url => ({ ok: true, json: async () => url.includes('/bids?') ? { items: [], total: 0 } : snapshot }),
});
vm.runInContext(fs.readFileSync('public/lot.js', 'utf8').replace(/loadLot\(\);\s*$/, ''), context);
const run = code => vm.runInContext(code, context);
(async () => {
  run("currentId='1'; currentLot={status:'active',current_price:5000,minimum_step:500}");
  run("handleLiveEvent({lotId:1,type:'bid_updated',currentPrice:4000,minimumNextBid:4500})");
  assert.equal(run('currentLot.current_price'), 5000, 'Older broadcasts cannot reduce price');
  run("connectLiveUpdates()");
  snapshot = { status: 'active', current_price: 6000, minimum_step: 500 };
  run('reconnectAttempts=4');
  await open();
  assert.equal(run('currentLot.current_price'), 6000, 'Reconnect recovers missed bid');
  assert.equal(run('reconnectAttempts'), 0, 'Healthy reconciliation resets the reconnect budget');
  snapshot = { status: 'closed', current_price: 6500, winnerUsername: 'alice' };
  await open();
  assert.equal(run('currentLot.status'), 'closed', 'Reconnect recovers missed closure');
  assert.equal(element('#final-result').textContent, 'Final price: $65.00 · Winner: alice');
  run("handleLiveEvent({lotId:1,type:'bid_updated',currentPrice:7000})");
  assert.equal(run('currentLot.current_price'), 6500, 'Delayed bid cannot change final result');
  run('currentLot=null');
  await open();
  assert.equal(run('pendingEvents[0].type'), 'lot_closed', 'Initial handshake snapshot is retained');
  run("currentLot={status:'active',current_price:5000,minimum_step:500}; awaitingClosure=false");
  context.window.auctionAuth = { user: () => ({ username: 'alice' }), token: () => 'test-token' };
  element('#bid-amount').value = '55.00';
  let requests = 0, resolveBid;
  context.fetch = async url => {
    if (url.endsWith('/bids')) {
      requests++;
      return new Promise(resolve => { resolveBid = resolve; });
    }
    return { ok: true, json: async () => ({ items: [], total: 0 }) };
  };
  const submit = element('#bid-form').handlers.submit;
  const pending = submit({ preventDefault() {} });
  run('updateBidAccess()');
  assert.equal(run('bidSubmit.disabled'), true, 'Live updates keep a pending bid disabled');
  await submit({ preventDefault() {} });
  assert.equal(requests, 1, 'Repeated submit cannot send a second pending bid');
  resolveBid({ ok: true, json: async () => ({ currentPrice: 5500 }) });
  await pending;
  assert.equal(run('bidSubmit.disabled'), false, 'Bid control recovers after the response');
  console.log('PASS: stale bids, reconnect recovery/budget, final result, snapshot buffering, pending bid protection');
})().catch(error => { console.error(error); process.exitCode = 1; });
