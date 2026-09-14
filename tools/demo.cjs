// Node 22.13+. Start creates a fresh, isolated database; close only accepts its config.
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { DatabaseSync } = require('node:sqlite');
const [action, target, lotArg, secondsArg = '30'] = process.argv.slice(2);
if (action === 'start' && target) {
  const build = path.resolve(target), stamp = Date.now();
  const configPath = path.join(build, `phase7-demo-${stamp}.json`);
  const config = JSON.parse(fs.readFileSync('config/config.example.json'));
  config.listeners[0].port = 18857;
  config.custom_config.database.path = path.join(build, 'runtime', `phase7-demo-${stamp}.sqlite3`);
  fs.writeFileSync(configPath, JSON.stringify(config, null, 2), { flag: 'wx' });
  console.log('Demo: http://127.0.0.1:18857/ and http://localhost:18857/');
  console.log(`Close lot 1 in 30 seconds from another terminal:\nnode tools/demo.cjs close "${configPath}" 1 30`);
  const server = spawn(path.join(build, process.platform === 'win32' ? 'auction_server.exe' : 'auction_server'), [configPath], { stdio: 'inherit', windowsHide: true });
  server.on('error', error => { console.error(error.message); process.exitCode = 1; });
  server.on('exit', code => { process.exitCode = code ?? 0; });
  process.on('SIGINT', () => server.kill());
} else if (action === 'close' && target) {
  const configPath = path.resolve(target);
  const match = /^phase7-demo-(\d+)\.json$/.exec(path.basename(configPath));
  if (!match) throw new Error('Use the isolated config printed by the start command.');
  const config = JSON.parse(fs.readFileSync(configPath));
  const expected = path.join(path.dirname(configPath), 'runtime', `phase7-demo-${match[1]}.sqlite3`);
  if (path.resolve(config.custom_config.database.path) !== expected || !fs.existsSync(expected)) throw new Error('Not an initialized isolated demo database.');
  const lot = Number(lotArg), seconds = Number(secondsArg);
  if (!Number.isSafeInteger(lot) || lot <= 0 || !Number.isSafeInteger(seconds) || seconds < 3 || seconds > 3600) throw new Error('Supply positive lot ID and 3–3600 seconds.');
  const db = new DatabaseSync(expected);
  try {
    db.exec('PRAGMA busy_timeout=5000');
    const result = db.prepare("UPDATE auctions SET starts_at=strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 day'),ends_at=strftime('%Y-%m-%dT%H:%M:%SZ','now',?) WHERE id=? AND status='active'").run(`+${seconds} seconds`, lot);
    if (result.changes !== 1) throw new Error('Active lot not found. Start a fresh demo for another run.');
    console.log(`Lot ${lot} closes in ${seconds} seconds. Reload both pages now to refresh their countdown.`);
  } finally { db.close(); }
} else {
  console.error('Usage: node tools/demo.cjs start <build-directory> | close <printed-config> <lot-id> [seconds]');
  process.exitCode = 1;
}
