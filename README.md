# Online Auction Platform

Internship assignment. Deadline: **September 14, 2026**.

## Implemented scope (Phases 1 and 2)

Phase 1 provides the C++20/Drogon server, CMake/CTest, a static home page, and
`GET /api/health` returning HTTP 200 with JSON `{"status":"ok"}`.

Phase 2 adds an automatically initialized SQLite database, a deterministic seed of
10 categories and 1,000 active auctions, and 1,000 local Wikimedia Commons photographs.
It intentionally does not add authentication, catalog APIs, bidding, WebSockets, auction
closing, profiles, recommendations, or new frontend pages.

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
```

Expect HTTP 200, `Content-Type: application/json`, and `{"status":"ok"}` from health
(JSON whitespace may vary). The root URL serves the HTML home page. Open
<http://127.0.0.1:8080/> in a browser. Stop the server with **Ctrl+C**.

On first startup the server creates `runtime/auction.sqlite3` beside the executable,
applies `database/schema.sql`, and inserts the deterministic seed in one transaction.
Later starts detect schema version 1 and leave the existing data unchanged. To recreate
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
checks that the static home page exactly matches `public/index.html`. Database tests cover
atomic initialization, schema constraints, foreign keys, prepared values, deterministic
idempotent seeding, disk reopen, exact seed counts, indexes, integrity, and all local image
files/checksums. CTest starts in an unrelated directory and uses the same resource locator.
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
public/index.html              Minimal static frontend
src/main.cpp                   Load config and run Drogon's event loop
src/runtime/                   Locate configuration and assets beside the executable
src/controllers/               Thin HTTP request/response handlers
src/database/                  SQLite RAII wrapper, prepared statements and initialization
database/schema.sql            Strict schema and query indexes
database/seed_*.json           Deterministic categories and auctions
database/image_manifest.json   Local-image provenance, dimensions and checksums
database/image_credits.csv     Human-readable Wikimedia author/license attribution
tools/import_commons_images.py Resumable one-time image importer and verifier
public/images/products/        One local photograph per seeded auction
src/services/                  Reserved for later business logic
src/repositories/              Reserved for later prepared database operations
src/models/                    Reserved for application data
src/websocket/                 Reserved for subscriptions and broadcasts
tests/HttpTests.cpp            Drogon HTTP integration tests
tests/DatabaseTests.cpp        SQLite, seed, constraint and image-integrity tests
CMakeLists.txt                 C++20 targets, Drogon linkage, CTest registration
CMakePresets.json              Portable debug configure/build/test commands
vcpkg.json                     Pinned manifest with Drogon ORM and SQLite features
```

The remaining reserved directories contain only `.gitkeep` placeholders. Vanilla CSS and
JavaScript ES modules will be added under `public/` when frontend functionality begins.

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

The September 12, 2026 audit configured a new `build/phase1-clean` with MSVC,
built both executables, and passed CTest (13 assertions in two test cases).
Linux and macOS resource discovery is implemented but was not executed on this Windows host.
The audit checks IDE-equivalent working directories; clicking Visual Studio's Start
button itself requires a manual IDE check.
