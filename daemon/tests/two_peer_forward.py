#!/usr/bin/env python3
"""
Smoke test for chunk 5 (issue #78): two peers connect, A forwards to B,
self-route refusal, unknown-peer error.

Run:
    LIFXD_MYSQL_URL='mysql://NEWROOT:NEWPASS@127.0.0.1:3306/lifx_dispatcher' \
        ./target/release/lifxd &
    python3 tests/two_peer_forward.py
"""

import base64
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


def connect_and_hello(world_id, server_uuid):
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


def main():
    a_uuid = "00000000-0000-0000-0000-0000aaaa0001"
    b_uuid = "00000000-0000-0000-0000-0000bbbb0002"

    sa, a_id = connect_and_hello(1001, a_uuid)
    sb, b_id = connect_and_hello(1002, b_uuid)
    print(f"peer A id = {a_id}")
    print(f"peer B id = {b_id}")
    assert a_id != b_id, "two different uuids should produce two different peer_ids"

    # === Test 1: A -> B succeeds ============================================
    payload = b"hello B, this is A"
    sa.sendall(frame({
        "type": "forward",
        "target_peer_id": b_id,
        "payload_b64": base64.b64encode(payload).decode(),
    }))
    delivery = recv_frame(sb)
    assert delivery["type"] == "delivery", f"expected delivery, got {delivery}"
    assert delivery["from_peer_id"] == a_id
    assert base64.b64decode(delivery["payload_b64"]) == payload
    print("OK  test 1: A -> B delivered:", delivery)

    # === Test 2: A -> A returns self_route_refused ==========================
    sa.sendall(frame({
        "type": "forward",
        "target_peer_id": a_id,
        "payload_b64": base64.b64encode(b"self").decode(),
    }))
    err = recv_frame(sa)
    assert err["type"] == "forward_error", f"expected forward_error, got {err}"
    assert err["reason"] == "self_route_refused", f"reason was {err['reason']}"
    assert err["target_peer_id"] == a_id
    print("OK  test 2: A -> A refused:", err)

    # === Test 3: A -> nonexistent returns unknown_peer ======================
    sa.sendall(frame({
        "type": "forward",
        "target_peer_id": "ffffffff-ffff-ffff-ffff-ffffffffffff",
        "payload_b64": base64.b64encode(b"nowhere").decode(),
    }))
    err = recv_frame(sa)
    assert err["type"] == "forward_error", f"expected forward_error, got {err}"
    assert err["reason"] == "unknown_peer", f"reason was {err['reason']}"
    print("OK  test 3: A -> nonexistent rejected:", err)

    sa.close()
    sb.close()
    print("\nALL OK")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, ConnectionError, EOFError) as e:
        print(f"FAIL: {e}")
        sys.exit(1)
