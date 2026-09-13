// Run from the repository root: node tests/LotLiveUpdatesTests.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const elements = new Map();
function element(selector) {
  if (!elements.has(selector)) elements.set(selector, { textContent: '', value: '', classList: { add() {}, toggle() {} }, addEventListener() {}, replaceChildren() {}, append() {} });
  return elements.get(selector);
}
let open;
let snapshot;
const context = vm.createContext({
  document: { querySelector: element, createElement: () => element(Symbol()) },
  window: { auctionAuth: { user: () => null } },
  location: { protocol: 'http:', host: 'localhost' },
  addEventListener() {}, setTimeout() {}, clearTimeout() {}, setInterval() {}, clearInterval() {},
  WebSocket: class { addEventListener(type, handler) { if (type === 'open') open = handler; } },
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
  await open();
  assert.equal(run('currentLot.current_price'), 6000, 'Reconnect recovers missed bid');
  snapshot = { status: 'closed', current_price: 6500, winnerUsername: 'alice' };
  await open();
  assert.equal(run('currentLot.status'), 'closed', 'Reconnect recovers missed closure');
  assert.equal(element('#final-result').textContent, 'Final price: $65.00 · Winner: alice');
  run("handleLiveEvent({lotId:1,type:'bid_updated',currentPrice:7000})");
  assert.equal(run('currentLot.current_price'), 6500, 'Delayed bid cannot change final result');
  run('currentLot=null');
  await open();
  assert.equal(run('pendingEvents[0].type'), 'lot_closed', 'Initial handshake snapshot is retained');
  console.log('PASS: stale bids, reconnect bid/closure recovery, final result, initial snapshot buffering');
})().catch(error => { console.error(error); process.exitCode = 1; });
