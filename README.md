# Online Auction Platform

Internship assignment. Deadline: **September 14, 2026**.

## Project overview

QuickBid is a local, presentation-ready online auction demo. A C++20/Drogon
server exposes the catalog, authentication, bidding, and live-auction APIs; SQLite
persists users, hashed sessions, bids, prices, deadlines, and winners; and a dependency-free
HTML/CSS/JavaScript client provides the browser experience. The deterministic dataset has
10 categories, 1,000 lots, and one locally stored, attributed photograph per lot.

## Implemented features (Phases 1–8)

- Responsive catalog with search, category filters, price/time sorting, pagination,
  shareable URL state, loading/error/empty states, and live countdowns.
- Lot details with public bid history, authenticated integer-cent bidding, duplicate-submit
  protection, per-lot WebSocket updates, bounded reconnect with authoritative recovery,
  and automatic final winner display.
- Registration, login, logout, 24-hour bearer sessions, salted PBKDF2 password hashes,
  hashed tokens at rest, prepared SQL, and serialized bid/closure transactions.
- Idempotent schema initialization and migrations, deterministic seed data, local image
  integrity/provenance checks, automatic auction closure, and persisted results.
- CMake/CTest coverage, frontend and two-client transport regressions, a clean isolated
  demo helper, accessibility/responsive refinements, and the Phase 8 final audit.

## Architecture

```text
Browser (vanilla HTML/CSS/JS)
  ├─ HTTP/JSON ──> Drogon controllers ──> services ──> SQLite RAII/prepared statements
  └─ WebSocket <── per-lot event hub <──── committed bid and closure events
```

Controllers parse requests and map errors to HTTP responses. Services own authentication,
bidding, and closing rules; the catalog repository owns read queries. `BEGIN IMMEDIATE`
serializes competing SQLite writers, and events are published only after commits. The
browser treats its countdown as advisory and reconciles authoritative lot state after a
WebSocket connection or reconnection.

## Technology stack

| Layer | Technology |
|---|---|
| Language/build | C++20, CMake 3.24+, Ninja, MSVC |
| Server | Drogon |
| Persistence | SQLite 3 with versioned SQL migrations |
| Security | OpenSSL PBKDF2-HMAC-SHA256 and secure random tokens |
| Frontend | Semantic HTML, responsive CSS, vanilla JavaScript |
| Verification | CTest/Drogon Test, Node.js frontend and WebSocket checks |

## API and WebSocket overview

| Method | Endpoint | Purpose |
|---|---|---|
| `GET` | `/api/health` | Readiness check |
| `GET` | `/api/categories` | List categories |
| `GET` | `/api/lots` | Paginated active catalog with validated filters/sort |
| `GET` | `/api/lots/{id}` | Authoritative lot details and lifecycle state |
| `POST` | `/api/lots/{id}/view` | Record an authenticated user's view (feeds personalized recommendations) |
| `GET` | `/api/recommendations?limit=8` | Content-based TF-IDF personalized or trending recommendations |
| `POST` | `/api/auth/register` | Create a user and session |
| `POST` | `/api/auth/login` | Create a session |
| `GET` | `/api/auth/me` | Resolve a bearer session |
| `POST` | `/api/auth/logout` | Revoke the current session |
| `GET` | `/api/lots/{id}/bids` | Public paginated bid history |
| `POST` | `/api/lots/{id}/bids` | Place an authenticated integer-cent bid |
| WebSocket | `/ws/lots/{id}` | Receive `bid_updated` and `lot_closed` events |

## Demo

After building, start a fresh isolated demo from the repository root:

```powershell
node tools/demo.cjs start build/phase8-clean
```

Open <http://127.0.0.1:18857/> and <http://localhost:18857/> for separate browser
sessions. The helper prints the exact command for shortening lot 1 to a 30-second auction.
Follow [the 5–7 minute demo guide](docs/demo-guide.md) for the presentation sequence,
credentials, talking points, and recovery notes.

## Known limitations

- This is a loopback demo without TLS, payments, shipping, seller administration,
  rate limiting, password reset, or production account security.
- Demo bearer tokens are stored in `localStorage`; only their SHA-256 hashes are stored
  in SQLite. Demo credentials are intentionally public.
- SQLite serializes writers, and WebSocket subscriptions are process-local; horizontal
  scaling would require a shared database strategy and event broker.
- Catalog cards do not stream price changes. The lot-details page is the live view.
- Reconnect is bounded, and the UI eventually asks the user to refresh after repeated
  failures. Firefox, Safari, physical phones, and a full screen-reader audit are untested.

## Windows prerequisites

Install Visual Studio 2026 with **Desktop development with C++**, including the MSVC x64/x86
build tools, a Windows SDK, **C++ CMake tools for Windows**, and vcpkg package manager.
Install Git for Windows. CMake 3.24+ and Ninja are required; Visual Studio supplies both.
Internet access is needed for the first configure to download/build manifest dependencies.

Open this repository as a folder in Visual Studio. Open **View > Terminal**, choose
**Developer PowerShell**, and initialize the x64 build environment with the commands below.
These commands discover the installation rather than assuming the Community edition.

## Configure, build, test (Windows / Developer PowerShell)

Run from the repository root:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install the Visual Studio C++ desktop workload first.' }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VCPKG_ROOT = "$vs\VC\vcpkg"
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Each command must succeed before proceeding. CMake installs dependencies automatically in manifest mode.
The baseline in `vcpkg.json` pins the dependency registry. No global `vcpkg integrate install` is needed.
For a separate vcpkg checkout, set `VCPKG_ROOT` to that checkout instead (bootstrap it first).
The shared preset uses Ninja on both Windows and Linux. In the Visual Studio UI, choose the `debug`
preset (use CMakePresets.json rather than the locally generated, ignored CMakeSettings.json).
Launch Visual Studio from a shell with `VCPKG_ROOT` set if its environment does not contain it.

## Run and verify

Run from the repository root:

```powershell
.\build\debug\auction_server.exe
```

In Visual Studio, select `auction_server.exe` as the startup target and press **F5**
or **Ctrl+F5**. CMake copies `config/config.example.json` and `public/` beside the
executable on every build. The server locates its own executable using the operating
system (Windows, Linux, or macOS), then uses that folder for relative resource paths.
No source-directory path is compiled into the application. Rebuild after editing
the example configuration or public files. Keep the executable, dependency libraries,
`config/`, and `public/` together when copying the build to another folder.

Launching directly from the build folder also works:

```powershell
Push-Location build/debug
.\auction_server.exe
# After stopping the server with Ctrl+C:
Pop-Location
```

In a second terminal:

```powershell
curl.exe --fail --include http://127.0.0.1:8080/api/health
curl.exe --fail --include http://127.0.0.1:8080/
curl.exe --fail --include http://127.0.0.1:8080/api/categories
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?page=1&limit=20"
```

Expect HTTP 200, `Content-Type: application/json`, and `{"status":"ok"}` from health
(JSON whitespace may vary). The root URL serves the HTML home page. Open
<http://127.0.0.1:8080/> in a browser. Stop the server with **Ctrl+C**.

On first startup the server creates `runtime/auction.sqlite3` beside the executable,
applies `database/schema.sql`, and inserts the deterministic seed in one transaction.
Later starts migrate older databases to schema version 3 and leave existing data intact. To recreate
the seed database in the normal debug build, stop the server and run:

```powershell
Remove-Item -LiteralPath .\build\debug\runtime\auction.sqlite3 -ErrorAction SilentlyContinue
.\build\debug\auction_server.exe
```

The SQLite file and its journal files are ignored by Git. A custom configuration may
override `custom_config.database.path`; relative paths are resolved beside the executable.

Optional local configuration:

```powershell
Copy-Item config/config.example.json config/config.json
.\build\debug\auction_server.exe config/config.json
```

Edit the ignored `config/config.json` to change the listener port. The example file is safe to commit.
The server defaults to the example config; a local config must be explicitly passed.
The config argument is resolved against the directory you launch from. Paths inside
the configuration, including `./public`, use the executable directory. The build copies
only the example config, never your local `config/config.json`.
Runtime uploads, if Drogon creates its working directories, stay in `runtime/` beside the executable,
outside `public/` and under the ignored build directory.
Only put publicly accessible frontend assets in `public/`.

## Phase 3 catalog API

All money values are integer cents and all timestamps are UTC. Successful responses use
JSON. Validation failures use HTTP 400 with an `{"error":"..."}` body, and a missing lot
uses HTTP 404 with `{"error":"Lot not found"}`.

```text
GET /api/categories
GET /api/lots
GET /api/lots/{id}
```

`GET /api/lots` returns only rows whose status is `active` and whose end time is still in
the future. Its optional parameters are:

| Parameter | Meaning | Default |
|---|---|---|
| `page` | Positive page number | `1` |
| `limit` | Rows per page, from 1 through 100 | `20` |
| `category_id` | Existing positive category ID | all categories |
| `search` | Case-insensitive title substring, at most 200 characters | no search |
| `sort_by` | `current_price`, `end_time`, or `title` | `end_time` |
| `order` | `asc` or `desc` | `asc` |

Values are passed to SQLite with prepared-statement parameters. The sort column and
direction cannot be parameters, so the controller validates both against the allow-list
above before the repository builds the `ORDER BY` clause.

With the server running, these commands exercise every endpoint and the main query
features:

```powershell
curl.exe --fail --include http://127.0.0.1:8080/api/categories
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?page=1&limit=20"
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?category_id=2&limit=10"
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?search=digital"
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?sort_by=current_price&order=desc"
curl.exe --fail --include "http://127.0.0.1:8080/api/lots?sort_by=end_time&order=asc"
curl.exe --fail --include http://127.0.0.1:8080/api/lots/101
curl.exe --include http://127.0.0.1:8080/api/lots/9999
curl.exe --include "http://127.0.0.1:8080/api/lots?limit=101"
```

The final two commands intentionally return 404 and 400, so they omit `--fail` to show
the JSON error bodies.

## Phase 4 frontend catalog

Start the server and open <http://127.0.0.1:8080/>. The catalog loads categories and lots
from the API; no product data is hardcoded in JavaScript. Selecting a card opens
`/lot.html?id={id}`, which loads that lot from `GET /api/lots/{id}`.

Catalog filters are stored in the address bar, so refreshing or sharing the URL preserves
the current page, search, category, and sort order. Search requests are debounced. A single
one-second timer updates all visible card countdowns, and the details page cleans up its
timer when navigation occurs.

Price inputs are displayed and shared in **whole dollars** (`?min_price=50&max_price=200`);
the UI converts them ×100 to the integer-cent API parameters when loading lots, and announces
the URL state in dollars. The API itself always uses cents, so when writing links by hand use
dollars (they are multiplied by 100 on load, exactly like the inline inputs).

Frontend files:

```text
public/index.html   Catalog structure, controls, states and accessible labels
public/catalog.js   API integration, URL state, cards, pagination and shared countdown
public/lot.html     Separate lot-details page and its failure states
public/lot.js       Lot lookup, safe rendering, countdown, bids and history
public/styles.css   Responsive auction-house visual design and reduced-motion support
```

Manual testing checklist:

- Open `/` and confirm 20 live lot cards and all 10 category choices appear.
- Search for `digital`, choose a category, and try each sort option.
- Use Previous/Next and refresh to confirm the address-bar state is preserved.
- Open a lot card, refresh its details URL, and return to the catalog.
- Try `/lot.html?id=9999` and `/lot.html?id=invalid` for friendly error states.
- Resize to desktop, tablet, and mobile widths and navigate controls with the keyboard.
- Confirm the browser console has no JavaScript errors.

Real-time catalog-card updates are intentionally not implemented; the lot-details page is
the authoritative live view.

## Phase 5 authentication and bidding

Demo accounts are created idempotently when the server starts:

| Username | Password |
|---|---|
| `alice` | `Alice123!` |
| `bob` | `Bob123!` |

Passwords are stored as salted PBKDF2-HMAC-SHA256 hashes with 210,000 iterations using
OpenSSL. Sessions expire after 24 hours. The client receives a random 256-bit bearer token,
while SQLite stores only its SHA-256 hash. The requested browser client keeps the raw token
in `localStorage`; production deployment should additionally use HTTPS and a strict CSP.

Schema version 2 adds a case-insensitive unique `username` to `users` and an `auth_tokens`
table containing the token hash, user foreign key, creation time, and expiry. Existing
`bids` rows store the auction/user foreign keys, integer-cent amount, and UTC creation time.
Bid placement uses `BEGIN IMMEDIATE`, validates the current price while holding SQLite's
write lock, inserts the bid, and updates the auction in one transaction.

Authentication endpoints:

```text
POST /api/auth/register
POST /api/auth/login
GET  /api/auth/me
POST /api/auth/logout
```

Bidding endpoints:

```text
GET  /api/lots/{id}/bids?page=1&pageSize=10
POST /api/lots/{id}/bids
```

Windows examples with the server running:

```powershell
curl.exe --include -H "Content-Type: application/json" -d '{"username":"ruben","password":"StrongPassword123!"}' http://127.0.0.1:8080/api/auth/register
$login = curl.exe --silent -H "Content-Type: application/json" -d '{"username":"alice","password":"Alice123!"}' http://127.0.0.1:8080/api/auth/login | ConvertFrom-Json
$token = $login.token
curl.exe --include -H "Authorization: Bearer $token" http://127.0.0.1:8080/api/auth/me
curl.exe --include "http://127.0.0.1:8080/api/lots/1/bids?page=1&pageSize=10"
curl.exe --include -H "Authorization: Bearer $token" -H "Content-Type: application/json" -d '{"amount":3175}' http://127.0.0.1:8080/api/lots/1/bids
curl.exe --include -X POST -H "Authorization: Bearer $token" http://127.0.0.1:8080/api/auth/logout
```

Amounts are integer cents at the API boundary. The UI displays USD and converts an entered
dollar amount to cents before submitting it. Successful bids refresh the displayed price
and recent history; Phase 6 broadcasts the accepted result to every browser subscribed to
that lot.

Manual Phase 5 flow:

- Register a new username in the header dialog, then log out and log back in.
- Open an active lot and submit at least the displayed minimum next bid.
- Confirm the current price and public recent-bid history update immediately.
- Try bidding below the minimum and bidding again while already highest; both show the
  server's validation message without modifying the bid history.
- Log out and confirm the bid form is replaced by the login prompt.

## Phase 6 real-time lifecycle

An auction has exactly two public lifecycle states. `active` means its UTC end time is
still in the future and it accepts bids. `closed` means `now >= endsAt`; it rejects bids,
has an immutable closure timestamp, and exposes either the winning username or `null`.
Schema version 3 persists `closed_at` and replaces the legacy stored `ended` value with
`closed`. The scheduler runs once per second on Drogon's main event loop, so it stops with
the server and does not own a background thread.

Both bid placement and the closing pass use SQLite `BEGIN IMMEDIATE`. The end time is
checked again inside the bid transaction. Consequently, only one writer can cross the
deadline at a time: a pre-deadline bid commits before closure, while a bid observed at or
after the deadline returns HTTP 409. Closing changes only `active` rows, making repeated
passes idempotent. The winner and final price are derived from the highest stored bid;
an auction with no bids keeps its starting/current price and has no winner.

`GET /api/lots/{id}` keeps the Phase 3 snake-case fields and also returns the Phase 6
fields `status`, `endsAt`, `currentPrice`, `winnerUsername`, and `closedAt`. Only public
usernames are exposed.

The live endpoint is:

```text
GET /ws/lots/{id}
```

Connections are grouped by lot ID and removed from the subscription hub when they close.
Bidding remains exclusively on the authenticated HTTP endpoint. Accepted bids produce:

```json
{
  "type": "bid_updated",
  "lotId": 42,
  "currentPrice": 15000,
  "minimumNextBid": 15020,
  "bid": {
    "id": 101,
    "bidderUsername": "alice",
    "amount": 15000,
    "createdAt": "2026-09-13T12:00:00Z"
  }
}
```

Closure produces:

```json
{
  "type": "lot_closed",
  "lotId": 42,
  "status": "closed",
  "currentPrice": 15000,
  "winnerUsername": "alice",
  "closedAt": "2026-09-13T12:01:00Z"
}
```

The details page updates price, minimum bid, bid history, status, final price, and winner
without refresh. At a local countdown of zero it displays `Closing…`, disables bidding,
and waits for the authoritative socket/API state. It makes up to five reconnect attempts with capped exponential backoff after a
socket failure, then shows a refresh message while preserving HTTP behavior.

Two-browser manual check:

1. Open the same active `/lot.html?id={id}` URL in two separate browser profiles/windows.
2. Log in as `alice` (`Alice123!`) in one and `bob` (`Bob123!`) in the other.
3. Place a valid bid in either window.
4. Confirm the other window updates its price, minimum, and recent bids without refresh.
5. For closure, set that test lot's `ends_at` to a UTC value a few seconds ahead and keep
   both windows open. Confirm both show the final result, then verify another POST bid
   returns HTTP 409.

Known limitations: subscriptions are process-local, so multi-process deployment would
need a shared broker; catalog cards do not receive WebSocket updates; and the demo client
stores its bearer token in `localStorage`.

## Recommendations

`GET /api/recommendations?limit=N` returns content-based suggestions built from TF-IDF
term vectors over active lot text (category + title + description) with cosine
similarity. Users with any view or bid history get a personalized Top-N
(`"personalized": true`, excluding lots they already bid on); everyone else gets a
cheap cold-start fallback of trending and ending-soon lots. Views arrive via
`POST /api/lots/{id}/view`, which the lot-details page sends once per session per lot
when the viewer is authenticated. The TF-IDF corpus is cached and rebuilt only when the
set of active lots changes, and cold-start requests never build the model at all.

## Phase 2 image import

The application never downloads images at runtime. The one-time importer uses only the
Wikimedia Commons API, accepts freely reusable JPEG/PNG photographs, resizes them to fit
480x320, rejects duplicate sources and image content, and records attribution and checksums.
From the repository root on Windows:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\tools\requirements-images.txt
.\.venv\Scripts\python.exe .\tools\import_commons_images.py
.\.venv\Scripts\python.exe .\tools\import_commons_images.py --verify-only
```

Interrupted imports can be rerun: the manifest is checkpointed and accepted images are not
downloaded twice. `--prepare-seed` regenerates deterministic auction JSON without downloading.
Image attribution is in `database/image_credits.csv`; detailed provenance and checksums are in
`database/image_manifest.json`.

## Tests

`ctest --preset debug` starts and stops its own Drogon server. No manually running server is required.
It tests the real health route (HTTP status, JSON content type, exact JSON members) and
checks that the static home page exactly matches `public/index.html` and that the details,
stylesheet, and JavaScript assets are served. Catalog HTTP tests
cover categories, active-only default pagination, category filtering, case-insensitive
title search, price/end-time sorting, query validation, lot details, 404 handling, and
serving a lot's local image. Database tests cover
atomic initialization, schema constraints, foreign keys, prepared values, deterministic
idempotent seeding, disk reopen, exact seed counts, indexes, integrity, and all local image
files/checksums. Authentication/bidding tests cover registration conflicts, login failure,
valid and invalid sessions, authenticated bidding, minimum bids, expired/missing lots,
public bid history, detail-price updates, and logout. Lifecycle tests cover active and
expired lots, winner/no-winner closure, idempotency, bid/close serialization, and stable
event JSON. CTest starts in an unrelated directory and uses the same resource locator.
Tests load the example config but use loopback port **18849**, which must be free.
CTest enforces a 30-second timeout; individual HTTP requests have a five-second timeout.
To see individual Drogon assertions:

```powershell
ctest --preset debug --verbose
```

## Linux

On Ubuntu/Debian, install the compiler and vcpkg build prerequisites:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git curl zip unzip tar pkg-config
# Ensure cmake --version is at least 3.24.
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
# From this repository's root:
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
./build/debug/auction_server
# In another terminal:
curl --fail --include http://127.0.0.1:8080/api/health
```

## Directory structure and responsibilities

```text
config/config.example.json     Listener, static document root, UTC logging
public/index.html              Responsive API-backed catalog page
public/lot.html                API-backed lot-details page
public/styles.css              Shared responsive catalog styling
public/catalog.js              Catalog state, API rendering and countdowns
public/lot.js                  Lot details, countdown, bidding and WebSocket updates
public/auth.js                 Shared login/register/session UI
src/main.cpp                   Load config and run Drogon's event loop
src/runtime/                   Locate configuration and assets beside the executable
src/controllers/               Thin health, catalog, authentication and bid handlers
src/database/                  SQLite RAII wrapper, prepared statements and initialization
database/schema.sql            Base schema and query indexes
database/migrations/           Versioned authentication and lifecycle migrations
database/seed_*.json           Deterministic categories and auctions
database/image_manifest.json   Local-image provenance, dimensions and checksums
database/image_credits.csv     Human-readable Wikimedia author/license attribution
tools/import_commons_images.py Resumable one-time image importer and verifier
public/images/products/        One local photograph per seeded auction
src/services/                  Catalog, authentication, bidding and closure logic
src/security/                  PBKDF2 password and random-token helpers
src/repositories/              Prepared catalog queries and result mapping
src/models/                    Catalog query and response-domain structures
src/realtime/                  Per-lot event JSON and thread-safe subscription hub
tests/HttpTests.cpp            Drogon HTTP integration tests
tests/DatabaseTests.cpp        SQLite, seed, constraint and image-integrity tests
tests/CatalogHttpTests.cpp      Catalog API and static product-image integration tests
tests/AuthBidHttpTests.cpp      Authentication and bidding HTTP integration tests
tests/AuctionLifecycleTests.cpp Closure, race-safety and event-format tests
CMakeLists.txt                 C++20 targets, Drogon linkage, CTest registration
CMakePresets.json              Portable debug configure/build/test commands
vcpkg.json                     Pinned manifest with Drogon ORM and SQLite features
```

The remaining reserved directories contain only `.gitkeep` placeholders.

CMake reads the preset and loads the vcpkg toolchain before configuring the compiler. vcpkg resolves
Drogon and its transitive dependencies from the manifest. `find_package(Drogon CONFIG REQUIRED)` exposes
`Drogon::Drogon`, which supplies includes and libraries to the targets. The controller is linked as an
object library into both executables so its automatic route registration is retained. `main.cpp` loads
Drogon's JSON settings and runs its HTTP event loop; Drogon dispatches `/api/health` to the controller
and serves the home page from `public/`. The database wrapper links SQLite directly and uses prepared
statements for seeded values. CTest runs the test executables using Drogon's testing tools.

## Maintenance rules

Keep controllers thin and business logic in services. Use prepared statements, transactions for bidding,
integer cents, UTC timestamps, secure password hashing, pagination, and filtering/sorting indexes.
The target dataset is approximately 1,000 products. Keep Windows/MSVC and Linux support.
Do not commit secrets, generated builds, or local configuration. No automatic commits or pushes.
Phases 1–8 are implemented for the local internship demo; production deployment is outside this scope.

## References

- [Drogon vcpkg features](https://github.com/microsoft/vcpkg/blob/master/ports/drogon/vcpkg.json)
- [Drogon configuration reference](https://github.com/drogonframework/drogon/blob/master/config.example.json)
- [Drogon testing tools](https://github.com/drogonframework/drogon/wiki/ENG-17-Testing-Framework)

## Phase 2 clean verification

After initializing Developer PowerShell as above, use a new, unused directory:

```powershell
cmake --preset debug -B build/phase2-clean
cmake --build build/phase2-clean --parallel
ctest --test-dir build/phase2-clean --output-on-failure --verbose
.\.venv\Scripts\python.exe .\tools\import_commons_images.py --verify-only
.\build\phase2-clean\auction_server.exe
```

In another terminal, run the two `curl.exe` commands from **Run and verify**.
Use another unused directory name if `build/phase2-clean` already exists. The first
configuration may take time because vcpkg builds the pinned C++ dependencies.

The September 13, 2026 Phase 2 verification used a fresh `build/phase2-clean`, compiled
with MSVC, and passed all three CTest entries: HTTP smoke tests, 53 database assertions,
and the 1,000-image integrity test. The server created
`build/phase2-clean/runtime/auction.sqlite3`; it contained schema version 1, 10 categories,
1,000 auctions, and 1,000 distinct local image paths.

## Phase 3 clean verification

Use a new, unused build directory after initializing Developer PowerShell as shown above:

```powershell
cmake --preset debug -B build/phase3-clean
cmake --build build/phase3-clean --parallel
ctest --test-dir build/phase3-clean --output-on-failure --verbose
.\build\phase3-clean\auction_server.exe
```

In another terminal, run the commands under **Phase 3 catalog API**. The catalog test
starts its own server on loopback port **18850**, creates an isolated temporary database,
and removes that database when the test finishes.

The September 12, 2026 audit configured a new `build/phase1-clean` with MSVC,
built both executables, and passed CTest (13 assertions in two test cases).
Linux and macOS resource discovery is implemented but was not executed on this Windows host.
The audit checks IDE-equivalent working directories; clicking Visual Studio's Start
button itself requires a manual IDE check.

## Phase 5 clean verification

Use a fresh build directory so the result does not depend on an earlier phase's generated
files or CMake cache:

```powershell
cmake --preset debug -B build/phase5-clean
cmake --build build/phase5-clean --parallel
ctest --test-dir build/phase5-clean --output-on-failure
.\build\phase5-clean\auction_server.exe
```

The authentication/bidding integration test uses loopback port **18851** and an isolated
temporary database. The temporary database is removed when the test finishes.

## Phase 6 clean verification

Use a new build directory and run the entire suite:

```powershell
cmake --preset debug -B build/phase6-clean
cmake --build build/phase6-clean --parallel
ctest --test-dir build/phase6-clean --output-on-failure
git diff --check
.\build\phase6-clean\auction_server.exe
```

The `auction_lifecycle` suite uses an isolated database and exercises automatic closure,
winner selection, no-bid closure, repeated closing, post-deadline rejection, and a
concurrent bid/close attempt. WebSocket event builders are asserted directly; use the
two-browser checklist above for the complete browser transport and rendering flow.

### Phase 6 continuation verification (September 14, 2026)

Built all C++ targets with MSVC in `build/phase6-verified`, using the previously
installed `build/phase5-clean/vcpkg_installed` dependencies with
`VCPKG_MANIFEST_INSTALL=OFF`. All six CTest suites passed. This was a fresh application
build, not a fresh dependency installation.

The lot page now refreshes authoritative state after a WebSocket connection opens,
including reconnection, and retains that snapshot if initial rendering is still pending.
Older broadcasts and delayed bid responses cannot lower the displayed price or replace
a closed auction's final result.

Additional regression checks (Node 22.13+; run from the repository root):

```powershell
node tests/LotLiveUpdatesTests.cjs
node tests/RealtimeTransportTests.cjs build/phase6-verified
```

Both checks passed. The transport test starts an isolated server on port 18856 with a
throwaway database, validates the WebSocket upgrade hash, and checks two subscribers
receive accepted bids and scheduled closure, winner persistence, and per-lot isolation.
It uses a minimal TCP WebSocket receiver because this host's Node built-in WebSocket
client rejected the handshake. The frontend check uses a simulated DOM; the two-browser
visual checklist above still requires manual verification.


## Phase 7 build, test, and demo

After the Developer PowerShell setup above, use an unused build directory:

```powershell
cmake --preset debug -B build/phase7-verified
cmake --build build/phase7-verified --parallel 4
ctest --test-dir build/phase7-verified --output-on-failure
node tests/LotLiveUpdatesTests.cjs
node tests/RealtimeTransportTests.cjs build/phase7-verified
node tests/RealtimeTransportTests.cjs build/phase7-verified --native
node tools/demo.cjs start build/phase7-verified
```

Run each command only after the previous command succeeds. Node 22.13+ is required for
these optional JavaScript checks and the demo helper; the application itself does not
require Node. The helper uses port 18857 and creates a fresh isolated database. It prints
the exact close command for another terminal. The transport tests use port 18856 and
remove only their own temporary database/configuration on completion.

On September 14, all six CTest suites, the frontend regression check, and both transport
modes passed. The actual two-session Chromium browser flow, restart recovery, desktop
and mobile-frame layout inspection, and error recovery are recorded in
[docs/demo-guide.md](docs/demo-guide.md). Other browser engines and operating systems
were not tested. No commit or push is part of this phase's verification.

This host's fresh MSVC application build reused its existing dependencies by adding:

```powershell
-DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_INSTALLED_DIR="$PWD/build/phase5-clean/vcpkg_installed"
```

to the configure command. This option is only appropriate when those dependencies
already exist; a new checkout should use the normal manifest install above.

The earlier Node handshake limitation was isolated to reuse of the health probe's pooled
connection. The test now sends `Connection: close` for readiness, and native WebSocket
mode passes without a server protocol change. See the demo guide for diagnostic detail.
