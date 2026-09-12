# Phase 1 repository audit — September 12, 2026

## Result

The resource-location defect is fixed without embedding this computer's source path.
A fresh MSVC configure/build and all automated runtime checks pass. Phase 1 is not fully
signed off: a literal click of Visual Studio's Start button was not performed during this audit.
Equivalent executable launches from the repository, build folder, and an unrelated folder passed.
Linux/macOS implementations were reviewed, but those operating systems were not available for execution.

No Phase 2 functionality was added. Nothing was committed, staged, or pushed.
All server processes started by the audit were stopped. The existing build was preserved;
a new `build/phase1-clean` directory was used for the clean build.

## Initial state and reproduction

The current files did NOT match the reported old no-argument failure: `src/main.cpp` already
changed to `AUCTION_SOURCE_DIR`, and CMake embedded `${PROJECT_SOURCE_DIR}` in that macro.
The pre-audit `build/debug/auction_server.exe` successfully served both routes with no arguments
from `build/debug`. This result is recorded in `build/audit-evidence/before-default.log`.
It would be incorrect to claim that exact no-argument launch still failed in the inspected state.

The same missing-relative-config error was reproduced with the pre-audit executable using:

```powershell
Push-Location build/debug
try { & ./auction_server.exe config/config.example.json } finally { Pop-Location }
```

Result: exit code 1 and:

```text
Server startup failed: Config file config/config.example.json not found!
```

Cause: relative filenames use the process's working directory. In that launch the program
looked for `build/debug/config/config.example.json`; the file existed only at the repository's
`config/config.example.json`. Simply locating the JSON is insufficient because its `./public`
setting also depends on the working directory. The previous absolute-source-path workaround
hid this for no-argument launches but coupled the executable to the original checkout.

The `VS_DEBUGGER_WORKING_DIRECTORY` CMake property is ignored by non-Visual-Studio generators.
This repository uses Ninja, even when opened inside the Visual Studio IDE.
See [CMake's property documentation](https://cmake.org/cmake/help/v3.25/prop_tgt/VS_DEBUGGER_WORKING_DIRECTORY.html).

## Changes explained

- `CMakeLists.txt`: removed the compiled source-directory macro. Added `auction_resources`, which
  copies the example JSON and `public/` beside the executable each time the server or tests build.
  Rebuilding after an HTML edit refreshes the copied page even when C++ does not need recompiling.
  The target's output directory expression supports Ninja and multi-configuration layouts.
  Local `config/config.json` is never copied. The optional Visual Studio-generator debugger
  directory now points to the executable folder; correctness does not depend on that setting.
- `src/runtime/RuntimePaths.h` and `.cpp`: added a shared resource locator. Windows asks
  `GetModuleFileNameW` for the running executable, Linux resolves `/proc/self/exe`, and macOS
  uses `_NSGetExecutablePath`. This works independently of the caller's directory or `argv[0]`.
  The locator checks the config exists, reports its resolved path if missing, and switches to
  the executable folder before Drogon loads relative settings.
- `src/main.cpp`: calls that shared locator before loading the configuration. An explicit config
  argument is resolved against the caller's original directory. Paths inside the configuration
  use the executable directory, consistently with the default config.
- `tests/HttpTests.cpp` and CTest settings: tests now use the production locator and start in an
  unrelated directory with no config or public files. The home-page check compares the complete
  file contents instead of searching for one phrase. Health checks still validate HTTP status,
  JSON content type, the exact member count, and `status: ok`.
- `.gitignore`: added `.env.*` and common private-key/certificate file patterns, while allowing
  `.env.example`. Existing rules cover builds, DLLs, executables, IDE data, local configuration,
  vcpkg installations, and runtime files. Ignore rules cannot recognize arbitrary secrets placed
  in otherwise ordinary source files; none were found in the inspected project files.
- `README.md`: documents resource copying, launch methods, custom-config semantics, rebuilding
  after asset edits, and clean-build commands. Removed stale claims that runtime testing remained
  blocked by Application Control. Recorded the new verified results and manual/platform limits.
- `docs/phase1-audit.md`: this review record. Temporary audit scripts, logs, and the relocation
  fixture are under ignored `build/`; they are not application features or build dependencies.

The controller, HTML page, example configuration, vcpkg manifest, and presets already satisfied
those parts of Phase 1 and were left unchanged. The example upload path resolves under the runtime
folder outside `public/`, confirmed on disk after execution. Drogon's pinned implementation resolves
relative upload paths against the document root:
[Drogon 1.9.13 implementation](https://github.com/drogonframework/drogon/blob/v1.9.13/lib/src/HttpAppFrameworkImpl.cc#L653-L665).

## Exact configure, build, and test commands

Run from the repository root. The scripts actually executed are
`build/audit-evidence/clean-build.ps1` and `build/audit-evidence/debug-build.ps1`.
They also check each native command's exit code and stop on failure.

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ workload not found.' }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VCPKG_ROOT = "$vs\VC\vcpkg"
$env:PATH = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;$env:PATH"

# This directory did not exist before the audit.
cmake --version
cmake --preset debug -B build/phase1-clean
cmake --build build/phase1-clean --parallel
ctest --test-dir build/phase1-clean --output-on-failure --verbose

# Also update the normal IDE/README output directory.
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
cmake --build build/debug --target auction_server
```

Results (all commands exited 0):

```text
cmake version 4.3.1-msvc1
The CXX compiler identification is MSVC 19.51.36256.0
Drogon 1.9.13 installed with core, orm, sqlite3
SQLite 3.53.1#2 installed
Clean build: seven build steps, both executables linked
All tests passed (13 assertions in 2 tests cases).
100% tests passed, 0 tests failed out of 1
Normal debug preset: configure, build, and CTest passed
Server-only build: Copying configuration and public files beside auction_server
```

The new build had its own CMake cache, compiled objects, executables, and vcpkg installation.
vcpkg restored 11 dependency packages from its binary cache; dependencies were not rebuilt from source.
The generated Ninja rules use `-std:c++20`, and the clean cache has `VCPKG_MANIFEST_MODE:BOOL=ON`.
Full outputs: `build/audit-evidence/clean-build.log` and `build/audit-evidence/debug-build.log`.

## Exact runtime commands and results

The README interactive command is `./build/debug/auction_server.exe` from the repository root.
For the audit, `build/audit-evidence/readme-runtime.ps1` starts the same executable as a background
process, checks the port is initially free, and stops only that process afterward:

```powershell
$serverAudit = Start-Process -FilePath .\build\debug\auction_server.exe -WorkingDirectory $PWD -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 1
    curl.exe --fail --include http://127.0.0.1:8080/api/health
    curl.exe --fail --include http://127.0.0.1:8080/
} finally {
    if (-not $serverAudit.HasExited) { Stop-Process -Id $serverAudit.Id }
}
```

Captured in `build/audit-evidence/readme-runtime.log`:

```text
HTTP/1.1 200 OK
content-length: 15
content-type: application/json; charset=utf-8
{"status":"ok"}

HTTP/1.1 200 OK
content-length: 383
content-type: text/html; charset=utf-8
<!doctype html>
<html lang="en">
...the complete public/index.html contents...
```

Additional checks used this audit-only Python script. It owns and stops each child process,
rejects an already-used port, asserts HTTP status/content types, parses the JSON, and compares
HTML response bytes with the source file. Python is not required for the application or CTest.

```powershell
python build/audit-evidence/check-runtime.py build/phase1-clean/auction_server.exe .
python build/audit-evidence/check-runtime.py build/phase1-clean/auction_server.exe build/phase1-clean
python build/audit-evidence/check-runtime.py build/phase1-clean/auction_server.exe build/phase1-clean/test-working-directory
python build/audit-evidence/check-runtime.py build/phase1-clean/auction_server.exe . config/config.example.json
python build/audit-evidence/check-runtime.py build/debug/auction_server.exe .
python build/audit-evidence/check-runtime.py build/debug/auction_server.exe build/debug
```

Every invocation returned HTTP 200, exact JSON `{"status":"ok"}`, and a byte-for-byte match
with the 383-byte source `public/index.html`. See the `runtime-*.log` files in `build/audit-evidence`.

A copy of the clean executable, DLLs, and config was also placed in `build/relocated bundle`.
Its public page was replaced with a unique 73-byte marker page. The copied server was launched
from the unrelated test folder and returned that marker, proving it used the relocated public
folder rather than the source checkout. Paths containing spaces worked.

```powershell
$env:AUCTION_AUDIT_EXPECTED_PAGE = "$PWD/build/relocated bundle/public/index.html"
python build/audit-evidence/check-runtime.py 'build/relocated bundle/auction_server.exe' build/phase1-clean/test-working-directory
Remove-Item Env:AUCTION_AUDIT_EXPECTED_PAGE
```

Result: HTTP 200 on both routes; exact relocated-page match. See `runtime-relocated.log`.

## Acceptance checklist

PASS means supported by file inspection and the execution described above. FAIL for criterion 4
means its literal UI acceptance test remains unverified, not that the executable was observed failing.

| # | Criterion | Result | Evidence / limit |
|---|---|---|---|
| 1 | C++20, CMake, manifest mode | PASS | Required C++20; clean MSVC compile flags; manifest mode ON. |
| 2 | Drogon with SQLite declared correctly | PASS | Manifest requests orm/sqlite3; clean vcpkg install confirms both. |
| 3 | MSVC Windows build | PASS | New clean configure and build succeeded. |
| 4 | Visual Studio Start button | FAIL | Build-folder and server-only-target checks pass; literal IDE click not performed. |
| 5 | GET /api/health | PASS | HTTP 200, JSON content type, exactly {"status":"ok"}. |
| 6 | public/index.html served | PASS | HTTP 200, text/html, byte-for-byte match in all tested launch folders. |
| 7 | Valid example Drogon config | PASS | JSON parses, Drogon loads it, listener and static serving work. |
| 8 | Reliable cross-platform resource location | PASS | OS executable lookup, CMake copying, relocation check; Windows executed, Linux/macOS reviewed only. |
| 9 | Basic CTest tests | PASS | One CTest entry runs two cases, 13 assertions; unrelated starting directory. |
| 10 | Exact Windows README commands | PASS | Configure/build/test and executable/curl commands exercised. |
| 11 | Generated files and secrets ignored | PASS | git check-ignore confirms build, IDE, env, local config, key, and runtime patterns. |
| 12 | No Phase 2 implemented | PASS | Only health, static page, startup helper, and tests; reserved folders contain .gitkeep only. |

## Git state and diff

`git ls-files` reports only `.gitignore` as tracked. The Phase 1 scaffold was already untracked
when this audit began. Therefore plain `git diff` shows only the ignore-file change; it does not
show the untracked CMake/source files. They were read directly and reviewed with no-index diffs.
No staging was used to alter that state.

```text
 M .gitignore
?? CMakeLists.txt
?? CMakePresets.json
?? README.md
?? config/
?? docs/
?? public/
?? src/
?? tests/
?? vcpkg.json
```

Tracked diff: `.gitignore | 12 insertions(+)` relative to HEAD. Six of those lines (local CMake,
config, and runtime ignores) already existed at the start of this audit; six secret-pattern
lines were added during this audit. The other audit modifications are explained above.
`git diff --check` passes. Generated build and evidence directories are ignored.

## Review remaining

In Visual Studio, choose the `debug` preset and `auction_server.exe`, then press Start/F5 or
Ctrl+F5. Open `http://127.0.0.1:8080/` and `/api/health`. The running console may be blank;
keep it open until stopping the server. This is the one remaining manual acceptance check.
