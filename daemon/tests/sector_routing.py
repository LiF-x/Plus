#!/usr/bin/env python3
"""
Smoke test for chunk 10a (issue #89): sector ownership claims + resolve.

Two peers connect; A claims 442..450, B claims 451..459. Each then resolves
sectors across both ranges and outside, asserting the correct owner / unknown.

Run:
    LIFXD_MYSQL_URL='mysql://NEWROOT:NEWPASS@127.0.0.1:3306/lifx_dispatcher' \
        ./target/release/lifxd &
    python3 daemon/tests/sector_routing.py
"""

import json
import socket
import struct
import sys

HOST = ("127.0.0.1", 7400)


def frame(obj):
    body = json.dumps(obj).encode()
    return struct.pack("<I", len(body)) + body


def recv_frame(sock):
    hdr = b""
    while len(hdr) < 4:
        chunk = sock.recv(4 - len(hdr))
        if not chunk:
            raise EOFError("eof on length prefix")
        hdr += chunk
    n = struct.unpack("<I", hdr)[0]
    body = b""
    while len(body) < n:
        chunk = sock.recv(n - len(body))
        if not chunk:
            raise EOFError("eof on body")
        body += chunk
    return json.loads(body)


def hello(world_id, server_uuid):
    s = socket.create_connection(HOST)
    s.sendall(frame({
        "type": "hello",
        "world_id": world_id,
        "server_uuid": server_uuid,
        "proto_version": 1,
    }))
    ack = recv_frame(s)
    assert ack["type"] == "hello_ack", f"expected hello_ack, got {ack}"
    return s, ack["assigned_peer_id"]


def claim(sock, world_sector_id):
    sock.sendall(frame({"type": "claim_sector", "world_sector_id": world_sector_id}))
    r = recv_frame(sock)
    assert r["type"] == "sector_claimed" and r["world_sector_id"] == world_sector_id, r
    return r["peer_id"]


def resolve(sock, world_sector_id):
    sock.sendall(frame({"type": "resolve_sector", "world_sector_id": world_sector_id}))
    return recv_frame(sock)


def main():
    sa, a = hello(1001, "00000000-0000-0000-0000-0000aaaa10a1")
    sb, b = hello(1002, "00000000-0000-0000-0000-0000bbbb10a2")
    print(f"peer A = {a}")
    print(f"peer B = {b}")

    # === Claims ============================================================
    for s in range(442, 451):
        owner = claim(sa, s)
        assert owner == a, f"A claim {s} returned {owner}, expected {a}"
    for s in range(451, 460):
        owner = claim(sb, s)
        assert owner == b, f"B claim {s} returned {owner}, expected {b}"
    print("OK  claims: A=442..450, B=451..459")

    # === Resolve owned (A asks about its own range, expects A) ============
    for s in (442, 446, 450):
        r = resolve(sa, s)
        assert r["type"] == "sector_resolved" and r["peer_id"] == a, r
    print("OK  resolve own (A): 442/446/450 -> A")

    # === Resolve foreign (A asks about B's range, expects B) ==============
    for s in (451, 455, 459):
        r = resolve(sa, s)
        assert r["type"] == "sector_resolved" and r["peer_id"] == b, r
    print("OK  resolve foreign (A->B): 451/455/459 -> B")

    # === Symmetric from B side ============================================
    for s in (442, 450):
        r = resolve(sb, s)
        assert r["type"] == "sector_resolved" and r["peer_id"] == a, r
    print("OK  resolve foreign (B->A): 442/450 -> A")

    # === Unknown ==========================================================
    for s in (1, 99, 441, 460, 1_000_000):
        r = resolve(sa, s)
        assert r["type"] == "sector_unknown" and r["world_sector_id"] == s, r
    print("OK  resolve unknown: 1/99/441/460/1000000 -> sector_unknown")

    # === Re-claim is idempotent and reassigns ownership ===================
    # B claims 442 (previously A's). Subsequent resolve should return B.
    owner = claim(sb, 442)
    assert owner == b
    r = resolve(sa, 442)
    assert r["type"] == "sector_resolved" and r["peer_id"] == b, r
    print("OK  re-claim: B re-claims 442, resolve now -> B")

    sa.close()
    sb.close()
    print("\nALL OK")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ConnectionError, EOFError) as e:
        print(f"FAIL: {e}")
        sys.exit(1)
