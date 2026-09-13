# Online Auction Platform

Internship assignment. Deadline: **September 14, 2026**.

## Implemented scope (Phases 1–5)

Phase 1 provides the C++20/Drogon server, CMake/CTest, a static home page, and
`GET /api/health` returning HTTP 200 with JSON `{"status":"ok"}`.

Phase 2 adds an automatically initialized SQLite database, a deterministic seed of
10 categories and 1,000 active auctions, and 1,000 local Wikimedia Commons photographs.

Phase 3 adds the read-only catalog REST API: categories, paginated active lots with
filtering/search/sorting, and individual lot details. It intentionally does not add
authentication, bidding, WebSockets, auction closing, profiles, recommendations, or new
frontend pages beyond the read-only catalog.

Phase 4 adds a responsive vanilla HTML/CSS/JavaScript catalog and lot-details page.
The frontend uses the Phase 3 API directly and includes URL-persisted search, category
filtering, sorting, pagination, shared live countdowns, and loading/empty/error states.

Phase 5 adds username/password authentication, expiring bearer sessions, atomic bid
placement, bid history, and the minimum matching frontend controls. Real-time updates
remain Phase 6 work.

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
Later starts migrate older databases to schema version 2 and leave existing data intact. To recreate
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

Real-time bid updates are not implemented yet.

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
and recent history; Phase 6 will add WebSocket or SSE updates across browser sessions.

Manual Phase 5 flow:

- Register a new username in the header dialog, then log out and log back in.
- Open an active lot and submit at least the displayed minimum next bid.
- Confirm the current price and public recent-bid history update immediately.
- Try bidding below the minimum and bidding again while already highest; both show the
  server's validation message without modifying the bid history.
- Log out and confirm the bid form is replaced by the login prompt.

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
public bid history, detail-price updates, and logout. CTest starts in an unrelated directory and uses the same resource locator.
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
public/lot.js                  Lot details, countdown, bid form and history
public/auth.js                 Shared login/register/session UI
src/main.cpp                   Load config and run Drogon's event loop
src/runtime/                   Locate configuration and assets beside the executable
src/controllers/               Thin health, catalog, authentication and bid handlers
src/database/                  SQLite RAII wrapper, prepared statements and initialization
database/schema.sql            Base schema and query indexes
database/migrations/           Versioned authentication/session migration
database/seed_*.json           Deterministic categories and auctions
database/image_manifest.json   Local-image provenance, dimensions and checksums
database/image_credits.csv     Human-readable Wikimedia author/license attribution
tools/import_commons_images.py Resumable one-time image importer and verifier
public/images/products/        One local photograph per seeded auction
src/services/                  Catalog, authentication and transactional bid logic
src/security/                  PBKDF2 password and random-token helpers
src/repositories/              Prepared catalog queries and result mapping
src/models/                    Catalog query and response-domain structures
src/websocket/                 Reserved for subscriptions and broadcasts
tests/HttpTests.cpp            Drogon HTTP integration tests
tests/DatabaseTests.cpp        SQLite, seed, constraint and image-integrity tests
tests/CatalogHttpTests.cpp      Catalog API and static product-image integration tests
tests/AuthBidHttpTests.cpp      Authentication and bidding HTTP integration tests
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

## Rules for later phases

Keep controllers thin and business logic in services. Use prepared statements, transactions for bidding,
integer cents, UTC timestamps, secure password hashing, pagination, and filtering/sorting indexes.
The target dataset is approximately 1,000 products. Keep Windows/MSVC and Linux support.
Do not commit secrets, generated builds, or local configuration. No automatic commits or pushes.
Real-time WebSocket/SSE updates remain Phase 6 work.

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
