# Dispatcher — sector handoff

Commands for moving characters and forwarding frames between server peers
through the LiFx dispatcher daemon. These drive the multi-shard / seamless
sector-handoff path: a peer resolves which neighbour owns a sector, then
forwards a handoff payload to it.

> **Experimental.** This subsystem is still under active development. The
> commands below are stable enough to script against, but the underlying
> handoff protocol may change between LiFx releases. Test in a private cluster
> before relying on them in production.

## `LifxDispatcher::sendTo(string targetUuid, string payloadText)`

Send a forward frame to a named peer via the dispatcher daemon. `payloadText`
is sent as raw bytes. Returns `1` if the frame was queued, `0` if it was
dropped (no route, daemon down, etc.).

```tcl
%ok = LifxDispatcher::sendTo("peer-b7f2", "hello-from-shard-1");
```

## `LifxDispatcher::resolveSector(int worldSectorId)`

Ask the dispatcher which peer currently owns a given world sector. The reply
is logged asynchronously (it does not return the owner synchronously).

```tcl
LifxDispatcher::resolveSector(184);
```

## `LifxDispatcher::triggerHandoff(int charID, int worldSectorId, float worldX, float worldY)`

Resolve the destination peer for `worldSectorId` and forward a v2
`SectorHandoff` payload for the character at the given world coordinates.
Returns `1` if the resolve+forward was queued, `0` otherwise.

```tcl
LifxDispatcher::triggerHandoff(1234, 184, 1520.5, 980.0);
```

## `LifxDispatcher::testTransfer(int peerWorldId, string host, int port)`

Manually fire a client redirect to a specific peer endpoint. Intended for
testing the redirect path without going through sector resolution.

```tcl
LifxDispatcher::testTransfer(2, "10.0.0.12", 28100);
```

## `Lifx::sectorEdgeStatus()`

Dump the hook-captured `Player` position and the per-trigger sector-edge state
to the server log. Useful for confirming that a character has crossed into the
handoff zone before calling `triggerHandoff`.

```tcl
Lifx::sectorEdgeStatus();
```

## Notes

- `sendTo`, `resolveSector`, and `triggerHandoff` all require the dispatcher
  daemon to be running; otherwise frames are dropped (`sendTo` returns `0`).
- Replies from `resolveSector` arrive asynchronously in the server log — there
  is no synchronous return value carrying the owner peer.
