---
title: Qt / QtWebEngine & HTTPS
status: re
domain: client
tags: [client, qt, webengine, https, openssl, dll-swap]
related: [client_grid_patch.md, loader_and_injection.md, architecture.md]
sources: [extra/lif_client_qt515_spike/, extra/lif_client_qt515_spike/scripts/web/xsollaLightbox.html, extra/lif_client_qt515_spike/logs/2026-05-24/, source/server/cm_offsets.h]
updated: 2026-06-26
---

# Qt / QtWebEngine & HTTPS

**TL;DR.** The LiF client and server ship **Qt 5.9.0 / Chromium 56** (early 2017). For a long time the working assumption was that the in-game browser *could not* reach modern HTTPS sites (TLS 1.3 / new CAs) and that bumping Qt to 5.15 was "not realistic." **That conclusion is now disproven.** A spike under `extra/lif_client_qt515_spike/` drops the **entire Qt stack to 5.15.2 (MSVC 2019) / Chromium 83.0.4103.122** as loose DLL/EXE/`.pak` overlays, and the **stock 5.9-linked `yo_cm_client.exe` boots unmodified — through the main menu and all the way into in-world play against a local server** — Qt5 forward binary-compat held, no client rebuild, no exe patching. The 5.15 swap also fixes the server-side `Qt5Network`/OpenSSL story, but the crypto target changes: 5.15 wants **OpenSSL 1.1.x** (`libcrypto-1_1-x64.dll` / `libssl-1_1-x64.dll`), *not* the 1.0.2/`libeay32` that 5.9 expected. Tracked in **LiFxPluss #132**.

This is a client-side topic. `source/server/cm_offsets.h` contains **no** Qt/SSL/WebEngine symbols — there are no server RVAs to cite here; everything below is DLL/string/file evidence.

---

## What's bundled (stock 5.9 stack)

Detected by `strings -a Qt5Core.dll | grep -E "Qt 5\.[0-9]+\.[0-9]+"` — **Qt 5.9.0, x86_64-little_endian-llp64, MSVC 2015, dynamic/release**. Same point release on both server and client.

### Server (`lif_server_320850/`)

- `Qt5Core.dll`, `Qt5Network.dll` only.

### Client (`~/.local/share/Steam/steamapps/common/Life is Feudal Your Own/`)

The full Qt 5.9 stack plus the WebEngine modules:

- Core/network/Gui: `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Network.dll`, `Qt5Positioning.dll`, `Qt5PrintSupport.dll`, `Qt5Widgets.dll`
- QML/Quick: `Qt5Qml.dll`, `Qt5Quick.dll`, `Qt5QuickWidgets.dll`
- Web: `Qt5WebChannel.dll`, `Qt5WebSockets.dll`
- **WebEngine (Chromium)**: `Qt5WebEngine.dll`, `Qt5WebEngineCore.dll`, `Qt5WebEngineWidgets.dll`, `QtWebEngineProcess.exe`, `qtwebengine_resources*.pak`

The in-game browser is **QtWebEngine (Chromium-based)**, not QtWebKit. This distinction matters — the two have completely different network stacks. Stock `Qt5WebEngineCore.dll` reports `Chrome/56.0.2924.122`.

The single concrete consumer of in-game-browser HTTPS in the data is the Xsolla pay-station lightbox, `scripts/web/xsollaLightbox.html`, which pulls:

```
https://static.xsolla.com/embed/paystation/1.0.7/widget.min.js
```

If modern HTTPS doesn't work in the embedded browser, that store flow is what breaks.

## HTTPS for `QNetworkAccessManager` (server-side / Qt5Network, not the in-game browser)

`Qt5Network.dll` loads OpenSSL at runtime. The expected library names and the baked-in version-gate string are **Qt-version-specific** — see the table below.

| Stack | DLL names it dlopen's | Baked error string |
|---|---|---|
| **Qt 5.9.0** (stock) | `libeay32`, `ssleay32` (`libcrypto-1.0`/`libssl-1.0` family) | `OpenSSL version too old, need at least v1.0.2` |
| **Qt 5.15.2** (spike) | `libcrypto-1_1-x64`, `libssl-1_1-x64` | `Incompatible version of OpenSSL` |

Both stacks ship **no** OpenSSL DLLs out of the box, so `QSslSocket::supportsSsl()` returns `false` and `https://` from `QNetworkAccessManager` fails until you drop the matching pair into the exe's directory.

**Fix depends on which Qt you run:**

- On the **stock 5.9** stack, drop `libeay32.dll` + `ssleay32.dll`, both **OpenSSL 1.0.2 (1.0.2u recommended), x64, MSVC build**. Trusted sources: FireDaemon, Shining Light (slproweb) "Win64 OpenSSL v1.0.2u Light", Slik. Do **not** use 1.1.x/3.x on 5.9 — different DLL names and incompatible ABI; Qt 5.9 cannot load them.
- On the **5.15.2** spike, you need the opposite: `libcrypto-1_1-x64.dll` + `libssl-1_1-x64.dll` (**OpenSSL 1.1.x**, x64, MSVC). These are **not yet dropped into the spike overlay** — open gap.

Verification (either stack):

- `QSslSocket::supportsSsl()` → `true`
- `QSslSocket::sslLibraryVersionString()` → e.g. `"OpenSSL 1.0.2u  20 Dec 2019"` (or whatever build you dropped in)

> **Superseded note (history).** An earlier revision of this page stated flatly that the OpenSSL path needs 1.0.2/`libeay32`/`ssleay32` and that "1.1.x has different DLL names … Qt 5.9 cannot load them." That is correct *for the stock Qt 5.9 stack only*. Once the stack is bumped to 5.15.2, the requirement inverts to OpenSSL 1.1.x with the `*_1_1-x64` names — confirmed by `strings -a Qt5Network.dll` on the spike DLL (`libcrypto-1_1-x64`, `libssl-1_1-x64`, `Incompatible version of OpenSSL`).

## HTTPS for the in-game browser (QtWebEngine)

QtWebEngine bundles its own networking stack (Chromium + **BoringSSL**) and ignores the OpenSSL DLLs above. The browser does not fail because HTTPS is "disabled"; it fails (on stock) because the bundled Chromium is too old.

> **SUPERSEDED CONCLUSION (kept for history).** The original page concluded that the in-game browser *cannot* reach modern HTTPS, and that the only realistic remedies were a TLS-terminating proxy, loosening Chromium's cert checks, or shelling out to the system browser — because **Qt 5.9 ships Chromium 56** (early 2017), which predates:
>
> - TLS 1.3 — many modern sites are 1.3-only.
> - Modern cipher / curve defaults — ChaCha20-Poly1305, X25519 negotiation logic has aged badly.
> - The current Chromium-internal root CA roster — some 2023+ CAs aren't recognized.
>
> Typical stock failure: the TLS handshake aborts before any certificate is seen, or chains anchored on roots Chromium 56 doesn't know about get rejected.
>
> The original page further asserted that **"Bumping Qt to 5.15 … won't bind without rebuilding the client. Not realistic."** **This is the claim the spike disproves** (see next section). The 5.15.2 / Chromium 83 swap-in *does* bind against the stock 5.9-linked `yo_cm_client.exe` and boots, so a full Qt bump is the cleaner fix and is now the recommended direction.

### Stopgaps that still apply if you stay on stock 5.9

These remain valid if, for whatever reason, you don't ship the 5.15 overlay:

1. **Local TLS-terminating proxy (most reliable).** Run mitmproxy / Squid with SSL-bump on `127.0.0.1`, install its root cert into the Windows trust store, and point QtWebEngine at it:
   ```
   QTWEBENGINE_CHROMIUM_FLAGS=--proxy-server=http://127.0.0.1:8080 --ignore-certificate-errors-spki-list=<mitm-spki>
   ```
   The proxy speaks modern TLS 1.3 to the real internet and serves pages back to Chromium 56 over its own (old) TLS. Sidesteps both the cipher problem and the CA problem at once.
2. **Loosen Chromium's checks** (CA issues only; can't help against TLS-1.3-only servers). Set in env (Steam launch options: `QTWEBENGINE_CHROMIUM_FLAGS=… %command%`) *before* the client launches:
   ```
   QTWEBENGINE_CHROMIUM_FLAGS=--ignore-certificate-errors --allow-running-insecure-content
   ```
3. **Hijack outbound clicks to the system browser.** Intercept URL navigation in the in-game browser (or in LiFx if it ever hooks the relevant Torque API) and shell out to the user's default browser for `https://` links. Bypasses Chromium 56 entirely, at the cost of breaking the "in-game" UX.

The original page also noted, correctly, that you **cannot** just swap `Qt5WebEngineCore.dll` for a newer one in isolation: QtWebEngine is tightly coupled to `Qt5Core/Network/Gui/Widgets/Qml/Quick` of the *same* point release plus a matching `QtWebEngineProcess.exe` and resource `.pak` files; mixing versions crashes on load. The spike respects exactly this constraint — it swaps the **whole set together**, which is why it works.

---

## The 5.15.2 / Chromium 83 spike (current direction)

`extra/lif_client_qt515_spike/` is a full client tree where the entire Qt 5.9.0 stack has been replaced with **Qt 5.15.2 (MSVC 2019)** and **Chromium 83.0.4103.122**, delivered as loose-file overlay drop-ins. The stock `yo_cm_client.exe` (still 5.9-linked, untouched, **no exe patching** — see [loader_and_injection.md](loader_and_injection.md)) loads against the newer DLLs and runs.

### Evidence (verified on disk)

| Check | Command | Stock (`*.orig`) | Spike |
|---|---|---|---|
| Qt version | `strings -a Qt5Core.dll \| grep -aoE "Qt 5\.[0-9.]+"` | `Qt 5.9.0` | `Qt 5.15.2` |
| Chromium | `strings -a Qt5WebEngineCore.dll \| grep -aoE "Chrome/[0-9.]+"` | `Chrome/56.0.2924.122` | `Chrome/83.0.4103.122` |
| Qt5Network crypto | `strings -a Qt5Network.dll \| grep -i openssl` | `libeay32` / `ssleay32`, `OpenSSL version too old, need at least v1.0.2` | `libcrypto-1_1-x64` / `libssl-1_1-x64`, `Incompatible version of OpenSSL` |

The original 5.9 binaries are preserved alongside as `*.orig` (e.g. `Qt5Core.dll.orig`, `Qt5WebEngineCore.dll.orig`, `QtWebEngineProcess.exe.orig`, `qwindows.dll.orig`, `qtwebengine_resources*.pak.orig`), so the overlay is reversible.

### What the swap touches

The complete set replaced together (all dated in the spike at the swap time; `.orig` siblings retained):

- **Core/Gui/Net/Widgets:** `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Network.dll`, `Qt5Positioning.dll`, `Qt5PrintSupport.dll`, `Qt5Widgets.dll`
- **QML/Quick:** `Qt5Qml.dll`, `Qt5Quick.dll`, `Qt5QuickWidgets.dll`, plus the **new** `Qt5QmlModels.dll` (Qt 5.14+ splits `QtQml.Models` into its own DLL — no 5.9 counterpart, no `.orig`)
- **Web:** `Qt5WebChannel.dll`, `Qt5WebSockets.dll`, `Qt5WebEngine.dll`, `Qt5WebEngineCore.dll`, `Qt5WebEngineWidgets.dll`
- **Out-of-process renderer:** `QtWebEngineProcess.exe`
- **Resources:** `qtwebengine_resources.pak`, `qtwebengine_resources_100p.pak`, `qtwebengine_resources_200p.pak`, plus the **new** `qtwebengine_devtools_resources.pak`; `icudtl.dat`
- **Platform plugin:** `platforms/qwindows.dll`

### What is proven vs. not

- **Proven (runtime, May 24 log):** the spike client boots through the intro video to the **main menu** (`GuiCanvas::setContentControl( GuiMainMenuMultiplayerWindow("MainMenuMultiplayerWindow") )`) and then **all the way into the world**. Over the ~4-hour run (`logs/2026-05-24/C_2026-05-24-14-50-11.log`) it repeatedly connects to a local server (`127.0.0.1:28000`), authorises, loads, and reaches `GuiCanvas::setContentControl( GameTSCtrl("PlayGui") )` with a server-assigned controlling Player ghost (`GID:0x02000003`) and active input — more than a dozen connect → world → disconnect cycles, with `art/mainmenu/Menu_Day.ogv` looping at the menu between sessions. Qt 5.9→5.15 forward binary compatibility held with no client rebuild.
- **Not yet proven:** (a) the in-game browser actually *renders* a modern HTTPS page (e.g. the Xsolla widget URL above) under Chromium 83 — needs a launch + log read; (b) *sustained* in-**world** stability — world entry (`PlayGui`) is reached, but every session ends in a `_disconnectedCleanupMakeQuit` back to the menu after a few-to-~30 minutes, so long-running in-world play isn't demonstrated; (c) the `Qt5Network`/OpenSSL path, because the 1.1.x DLLs aren't dropped in yet.

### Delivery model

Modpack overlay — loose DLL/EXE/`.pak` drop-ins over the Steam client tree, with `.orig` backups for rollback. **No binary patching of `yo_cm_client.exe`** (hard project rule; cf. the client mods in [client_grid_patch.md](client_grid_patch.md) going through DLL/loose-file routes, never byte edits). Scope and follow-ups tracked in **LiFxPluss issue #132**.

### Open gaps (to close #132)

1. Drop in **OpenSSL 1.1.x** (`libcrypto-1_1-x64.dll`, `libssl-1_1-x64.dll`) so `Qt5Network` HTTPS works on 5.15.
2. Launch and read logs to confirm the embedded browser renders HTTPS (Xsolla pay-station flow).
3. Confirm *sustained* in-world stability — sessions already reach `PlayGui` but keep disconnecting; nail down why and whether it holds long-term.

---

## Detection recipes

```bash
# Qt version
strings -a Qt5Core.dll | grep -E "Qt 5\.[0-9]+\.[0-9]+"

# Bundled Chromium version (in-game browser)
strings -a Qt5WebEngineCore.dll | grep -aoE "Chrome/[0-9.]+"

# What crypto Qt5Network expects to find
strings -a Qt5Network.dll | grep -iE "openssl|libeay|ssleay|libcrypto|libssl"

# Is the in-game browser WebEngine or WebKit?
ls *.dll *.exe 2>/dev/null | grep -iE "webengine|webkit"
```

---

## Status & provenance

- **Runtime-verified:** stock stack is Qt 5.9.0 / Chromium 56.0.2924.122; spike stack is Qt 5.15.2 / Chromium 83.0.4103.122 (both confirmed via `strings` on the actual DLLs on disk). The spike client **boots and enters the world** with the unmodified 5.9-linked `yo_cm_client.exe` (May 24 client log shows it reaching `GuiMainMenuMultiplayerWindow` and then `GameTSCtrl("PlayGui")` with a controlling Player ghost against a local server at `127.0.0.1:28000`). `Qt5Network` DLL-name/error-string expectations confirmed by `strings` for both 5.9 (`libeay32`/`ssleay32`, "version too old, need at least v1.0.2") and 5.15 (`libcrypto-1_1-x64`/`libssl-1_1-x64`, "Incompatible version of OpenSSL"). `scripts/web/xsollaLightbox.html` confirmed to load the `https://static.xsolla.com/...` widget.
- **Inferred / not yet verified:** that the embedded Chromium 83 browser successfully completes a modern TLS 1.3 handshake and renders the page (high confidence from the version bump, but no runtime log yet); *sustained* in-world stability (sessions reached `PlayGui` but repeatedly disconnected); the exact OpenSSL 1.1.x build needed for the `Qt5Network` path.
- **Superseded:** the prior "the in-game browser cannot reach modern HTTPS / a 5.15 bump is not realistic" conclusion is retained above under a clear marker but is no longer current — the spike supersedes it.
- **No server RVAs apply:** this topic is client-side DLL/string evidence; `source/server/cm_offsets.h` has no Qt/SSL/WebEngine constants to cross-reference.
