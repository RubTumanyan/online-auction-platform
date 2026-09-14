// Requires Node 22.13+; run: node tests/RealtimeTransportTests.cjs <build-directory>
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { DatabaseSync } = require('node:sqlite');
const build = path.resolve(process.argv[2]);

const id = `transport-${process.pid}`;
const database = path.join(build, 'runtime', `${id}.sqlite3`);
const configPath = path.join(build, `${id}.json`);
const config = JSON.parse(fs.readFileSync('config/config.example.json'));
config.listeners[0].port = 18856;
config.custom_config.database.path = database;
fs.writeFileSync(configPath, JSON.stringify(config));
const server = spawn(path.join(build, process.platform === 'win32' ? 'auction_server.exe' : 'auction_server'), [configPath], { windowsHide: true, stdio: ['ignore', 'inherit', 'inherit'] });
const sockets = [];
const base = 'http://127.0.0.1:18856';
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate) {
  for (let i = 0; i < 100; i++) { if (await predicate()) return; await delay(100); }
  throw new Error('Timed out waiting for server/event');
}
function sql(statement) {
  const connection = new DatabaseSync(database);
  try { connection.exec('PRAGMA busy_timeout=5000'); connection.exec(statement); } finally { connection.close(); }
}
async function connect(lot) {
  if (process.argv.includes('--native')) {
    const events = [], socket = new WebSocket('ws://127.0.0.1:18856/ws/lots/' + lot);
    sockets.push({ destroy: () => socket.close() });
    socket.addEventListener('message', message => events.push(JSON.parse(message.data)));
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Native WebSocket handshake timed out')), 5000);
      socket.addEventListener('open', () => { clearTimeout(timer); console.log('Native subscription opened:', lot); resolve(); }, { once: true });
      socket.addEventListener('error', event => { clearTimeout(timer); reject(event.error ?? new Error('Native WebSocket failed')); }, { once: true });
    });
    return events;
  }
  const net = require('node:net');
  const crypto = require('node:crypto');
  const key = crypto.randomBytes(16).toString('base64');
  const expected = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  const events = [];
  const socket = net.connect(18856, '127.0.0.1');
  sockets.push(socket);
  let buffer = Buffer.alloc(0), upgraded = false;
  await new Promise((resolve, reject) => {
    socket.on('error', reject);
    socket.on('connect', () => socket.write([
      'GET /ws/lots/' + lot + ' HTTP/1.1', 'Host: 127.0.0.1:18856',
      'Upgrade: websocket', 'Connection: Upgrade', 'Sec-WebSocket-Key: ' + key,
      'Sec-WebSocket-Version: 13', '', ''
    ].join('\r\n')));
    socket.on('data', data => {
      buffer = Buffer.concat([buffer, data]);
      if (!upgraded) {
        const end = buffer.indexOf('\r\n\r\n');
        if (end < 0) return;
        const headers = buffer.subarray(0, end).toString();
        try {
          assert.ok(headers.startsWith('HTTP/1.1 101 '));
          assert.ok(headers.includes('sec-websocket-accept: ' + expected), 'Valid WebSocket accept hash');
        } catch (error) { reject(error); return; }
        buffer = buffer.subarray(end + 4); upgraded = true; resolve();
      }
      while (buffer.length >= 2) {
        const opcode = buffer[0] & 15;
        let length = buffer[1] & 127, offset = 2;
        if (length === 126) { if (buffer.length < 4) return; length = buffer.readUInt16BE(2); offset = 4; }
        if (length === 127) { if (buffer.length < 10) return; length = Number(buffer.readBigUInt64BE(2)); offset = 10; }
        if (buffer.length < offset + length) return;
        if (opcode === 1) events.push(JSON.parse(buffer.subarray(offset, offset + length).toString()));
        buffer = buffer.subarray(offset + length);
      }
    });
  });
  return events;
}

(async () => {
  try {
    await until(async () => { try { return (await fetch(`${base}/api/health`, { headers: { Connection: "close" } })).ok; } catch { return false; } });
    sql("UPDATE auctions SET ends_at='2099-01-01T00:00:00Z' WHERE id IN (1,2)");
    const a = await connect(1), b = await connect(1), unrelated = await connect(2);
    const login = await fetch(`${base}/api/auth/login`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ username: 'alice', password: 'Alice123!' }) });
    assert.equal(login.status, 200);
    const auth = await login.json();
    const lot = await (await fetch(`${base}/api/lots/1`)).json();
    const amount = lot.current_price + lot.minimum_step;
    const bid = await fetch(`${base}/api/lots/1/bids`, { method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${auth.token}` }, body: JSON.stringify({ amount }) });
    assert.equal(bid.status, 201);
    await until(() => a.some(e => e.type === 'bid_updated') && b.some(e => e.type === 'bid_updated'));
    assert.equal(a.find(e => e.type === 'bid_updated').currentPrice, amount);
    assert.equal(b.find(e => e.type === 'bid_updated').bid.bidderUsername, 'alice');
    sql("UPDATE auctions SET starts_at='1999-01-01T00:00:00Z',ends_at='2000-01-01T00:00:00Z' WHERE id=1");
    await until(() => a.some(e => e.type === 'lot_closed') && b.some(e => e.type === 'lot_closed'));
    assert.equal(a.find(e => e.type === 'lot_closed').winnerUsername, 'alice');
    assert.equal(b.find(e => e.type === 'lot_closed').currentPrice, amount);
    assert.equal(unrelated.length, 0, 'Other lots must not receive these events');
    const closed = await (await fetch(`${base}/api/lots/1`)).json();
    assert.equal(closed.status, 'closed');
    assert.equal(closed.winnerUsername, 'alice');
    console.log('PASS: two-client bid broadcast, scheduled closure, winner persistence, per-lot isolation');
  } finally {
    sockets.forEach(socket => socket.destroy());
    server.kill();
    await new Promise(resolve => server.exitCode !== null ? resolve() : server.once('exit', resolve));
    fs.rmSync(configPath, { force: true });
    for (const suffix of ['', '-wal', '-shm', '-journal']) fs.rmSync(database + suffix, { force: true });
  }
})().catch(error => { console.error(error); process.exitCode = 1; });


