# Phase 7 demo guide

## Purpose and scope

Aurelian Auctions demonstrates browsing approximately 1,000 photographed lots across
10 categories, authenticated bidding, and automatic winner selection. It is an internship
demo with seeded inventory, not a payment or fulfillment service.

## Prepare the demo

1. Follow the Windows prerequisites and Developer PowerShell setup in README.md.
2. Build and run the checks shown in README.md's Phase 7 section.
3. Install/use Node 22.13+ for the optional demo helper and JavaScript tests.
4. From the repository root, run:

```powershell
node tools/demo.cjs start build/phase7-verified
```

Keep this terminal running. The helper uses port **18857**, writes a uniquely named
configuration and SQLite database beneath the selected build directory, and prints a
ready-to-copy command for shortening a demo auction. It never resets the normal database.
Stop any earlier demo occupying that port before starting another. Ctrl+C stops the server;
the isolated files remain available for inspection and are ignored by Git.

Open **http://127.0.0.1:18857/** in one tab and **http://localhost:18857/** in another.
These are the same local server but different origins, so their login storage is separate.
Alternatively, use two browser profiles at the same address. Log in as `alice` / `Alice123!`
and `bob` / `Bob123!`. These are public demonstration credentials.

To demonstrate closing, use the exact command printed by the helper, choosing lot 1 and
30 seconds. It only accepts a matching isolated demo configuration and an active lot.
Reload both lot pages after changing the deadline so their displayed countdowns agree.
Start a fresh demo for another run; closed auctions are intentionally not reopened.

## Five-minute demo script

| Time | Show | Explain |
| --- | --- | --- |
| 0:00–0:40 | Catalog, category filter, title search, price sorting | The server filters and paginates the catalog; URLs retain the selection. |
| 0:40–1:10 | Next/previous page, empty search, clear filters | Loading, empty, and retry states are part of the normal interface. |
| 1:10–1:50 | Register a disposable user or log in to a demo account | Passwords are hashed; an expiring bearer session authorizes bids. |
| 1:50–2:45 | Lot 1 in both sessions; Alice bids the minimum, then Bob raises it | Both pages update price, minimum bid, and history without reloading. Money is integer cents. |
| 2:45–3:30 | Run the printed close command; reload both pages; watch the countdown | The server closes the auction and persists the highest bidder as winner. |
| 3:30–4:00 | Both final results, hidden bidding form, reload the closed lot | The final result survives reload; late bids are rejected by the server. |
| 4:00–4:30 | Narrow browser layout and logout | The same pages support mobile layouts and accessible labeled controls. |
| 4:30–5:00 | Architecture and verification results | Explain transaction safety, test coverage, and demo limitations. |

## Presentation outline

1. **Problem and scope:** discover lots, compete fairly, see a reliable result.
2. **User experience:** catalog controls, authentication, bidding, live history, closure.
3. **Architecture:** browser → thin Drogon controllers → services/repositories → SQLite.
4. **Correctness:** prepared statements, integer cents, UTC deadlines, serialized writes.
5. **Real-time delivery:** accepted transactions publish per-lot WebSocket events; reconnect
   fetches authoritative state to recover events missed during disconnection.
6. **Evidence and next steps:** tests and browser checks below; production features are separate work.

## Architecture explanation

The C++20 executable serves static HTML/CSS/JavaScript and JSON APIs with Drogon.
Controllers validate requests and call services. SQLite stores users, hashed sessions,
auctions, bids, and closure timestamps. `BidService` and `AuctionCloseService` use
`BEGIN IMMEDIATE`, so concurrent writers cannot accept a late bid after closure. The
one-second event-loop scheduler persists the winner, then broadcasts a `lot_closed`
event. Accepted bids publish `bid_updated` only after the transaction commits.

The frontend renders untrusted text as text, sends bids through authenticated HTTP, and
uses WebSockets only for updates. It ignores stale lower-price events and protects the
final result from delayed bid replies. Local countdowns are advisory; the database is
authoritative. No browser timestamp can authorize a late bid.

## Verification record — September 14, 2026

- Fresh MSVC application build: `build/phase7-verified`; reused installed Phase 5 vcpkg
  dependencies. All six CTest suites passed (24.74 seconds).
- Node frontend regression checks: stale bid handling, reconnect snapshots, final results,
  and snapshot buffering passed in a simulated DOM, not a browser renderer.
- Raw TCP and native Node WebSocket integration modes passed: two subscribers, bid delivery,
  automatic closing, persisted winner, and per-lot isolation.
- Actual in-app Chromium browser sessions at separate localhost/127.0.0.1 origins verified
  registration, login, logout, search, category selection, sorting, previous/next pagination,
  empty results, invalid/missing lots, network failure and retry recovery.
- Browser bidding: `phase7reviewer` bid $31.75; Bob raised to $36.75. Both pages received
  updates, displayed the $41.75 next minimum, then agreed Bob won for $36.75.
- Restart recovery: lot 2 expired while the server was stopped; both open pages reconnected
  and recovered the no-winner closure without manual refresh.
- Desktop screenshots inspected at 1280px. Catalog and closed-lot layouts inspected in
  390px browser iframes (375px content width after scrollbar), with no horizontal overflow.
  The browser viewport override did not affect the actual viewport, so iframes were used.
- Demo helper start and close commands exercised against its fresh isolated database.

## Node handshake investigation

On Node 24.19.0, the first native WebSocket opened but a subsequent sequential connection
failed when the readiness HTTP request left a pooled keep-alive connection available.
Fresh direct connections and browser connections worked. Sending `Connection: close` on
that readiness probe resolved the integration test; both native and raw modes now pass.
This isolates the failure to the pooled HTTP-to-WebSocket upgrade path in this environment;
it does not establish a general framework defect or require weakening handshake validation.
The native regression mode preserves coverage of the previously failing test flow.

## Known limitations

- Demo credentials and localStorage bearer tokens are for this local demonstration.
  Production deployment needs separate security, abuse prevention, and TLS work.
- WebSocket subscriptions are process-local; multiple server processes would require a
  shared event broker. SQLite serializes writers; this is not a high-volume deployment benchmark.
- Catalog prices do not stream live; open a lot or reload the catalog for current prices.
- Reconnect makes five attempts with capped backoff, then asks for a page refresh.
- Seed end dates are fixed; after they expire the active catalog can be empty. The helper
  shortens active lots for the current demo, not a permanent seed-date reset mechanism.
- Source photo documentation may include literal markup displayed safely as text.
- Browser checks cover the available Chromium engine, not Firefox, Safari, physical phones,
  or a complete screen-reader audit. Linux/macOS builds were not run on this Windows host.
- No payments, shipping, seller administration, or production deployment were added.

## Continuation verification — September 14, 2026

The Phase 7 continuation added a submission-handler guard against duplicate bids while
an HTTP request is pending. A regression check verifies that live UI refreshes keep the
button disabled, repeated submissions send only one request, and the button becomes
available again after the response.

The existing MSVC Phase 7 build was rebuilt successfully. All six CTest suites passed
(10.51 seconds), along with the frontend regression check, raw TCP transport check,
native Node WebSocket transport check, and `git diff --check`. Browser checks above
are the earlier recorded results; they were not repeated for this continuation.
