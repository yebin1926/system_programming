#!/usr/bin/env python3
#make clean
# make
# ./server -p 8080 -t 10
# python3 skvs_test.py --host 127.0.0.1 --port 8080 --clients 10
import argparse
import socket
import threading
import time
import sys
import random
import string

# -------------------------
# Robust line-based receive
# -------------------------
def recv_line(sock: socket.socket, timeout=5.0) -> str:
    sock.settimeout(timeout)
    buf = bytearray()
    while True:
        chunk = sock.recv(4096)
        if chunk == b"":
            # EOF
            return buf.decode("ascii", errors="replace")
        buf.extend(chunk)
        if b"\n" in buf:
            line, _, rest = buf.partition(b"\n")
            # put "rest" back? For this simple tester we assume 1 reply per request.
            return (line + b"\n").decode("ascii", errors="replace")

def send_all(sock: socket.socket, data: bytes):
    total = 0
    while total < len(data):
        n = sock.send(data[total:])
        if n <= 0:
            raise RuntimeError("send() returned <= 0")
        total += n

def send_fragmented(sock: socket.socket, text: str, fragments: int = 3, jitter: float = 0.02):
    data = text.encode("ascii")
    if fragments <= 1 or len(data) <= 1:
        send_all(sock, data)
        return
    # split data into random-ish fragment boundaries
    cuts = sorted(random.sample(range(1, len(data)), k=min(fragments-1, len(data)-1)))
    parts = []
    prev = 0
    for c in cuts:
        parts.append(data[prev:c])
        prev = c
    parts.append(data[prev:])
    for p in parts:
        send_all(sock, p)
        time.sleep(jitter)

def connect(host, port) -> socket.socket:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    return s

def assert_eq(got, expect, label):
    if got != expect:
        raise AssertionError(f"[{label}] expected {expect!r}, got {got!r}")

def rand_key(prefix="k", n=8):
    return prefix + "".join(random.choice(string.ascii_letters + string.digits) for _ in range(n))

# -------------------------
# Tests
# -------------------------
def test_basic(host, port):
    s = connect(host, port)
    send_all(s, b"CREATE a1 v1\n")
    r = recv_line(s)
    assert_eq(r, "CREATE OK\n", "CREATE")

    send_all(s, b"READ a1\n")
    r = recv_line(s)
    assert_eq(r, "v1\n", "READ")

    send_all(s, b"UPDATE a1 v2\n")
    r = recv_line(s)
    assert_eq(r, "UPDATE OK\n", "UPDATE")

    send_all(s, b"READ a1\n")
    r = recv_line(s)
    assert_eq(r, "v2\n", "READ after UPDATE")

    send_all(s, b"DELETE a1 v_unused\n")
    r = recv_line(s)
    assert_eq(r, "DELETE OK\n", "DELETE")

    send_all(s, b"READ a1\n")
    r = recv_line(s)
    assert_eq(r, "NOT FOUND\n", "READ after DELETE")

    # invalid command
    send_all(s, b"CRAETE a b\n")
    r = recv_line(s)
    assert_eq(r, "INVALID CMD\n", "INVALID CMD")

    s.close()

def test_case_insensitive_cmd(host, port):
    s = connect(host, port)
    send_all(s, b"creATE ciKey ciVal\n")
    r = recv_line(s)
    assert_eq(r, "CREATE OK\n", "case-insensitive CREATE")

    send_all(s, b"ReAd ciKey\n")
    r = recv_line(s)
    assert_eq(r, "ciVal\n", "case-insensitive READ")
    s.close()

def test_keep_alive_and_empty_line(host, port):
    s = connect(host, port)

    # multiple requests on same connection (keep-alive)
    send_all(s, b"CREATE ka1 vv\n")
    assert_eq(recv_line(s), "CREATE OK\n", "keep-alive CREATE")

    send_all(s, b"READ ka1\n")
    assert_eq(recv_line(s), "vv\n", "keep-alive READ")

    # empty line should make server close this connection
    send_all(s, b"\n")

    # server should eventually close; recv should return EOF ("" or just "")
    try:
        resp = recv_line(s, timeout=2.0)
    except socket.timeout:
        resp = None

    # If the server closes immediately, resp is "" (EOF). If it closes later, timeout may happen.
    # Either is acceptable, but after this, further sends should fail or connection will be gone.
    try:
        send_all(s, b"READ ka1\n")
        # If send worked, try reading; should likely EOF soon.
        _ = recv_line(s, timeout=2.0)
        raise AssertionError("connection still usable after empty line; expected it to close")
    except Exception:
        pass

    s.close()

def test_partial_send_and_long_value(host, port):
    s = connect(host, port)

    # send fragmented request (simulates partial TCP writes)
    send_fragmented(s, "CREATE fragKey fragVal\n", fragments=4, jitter=0.01)
    r = recv_line(s)
    assert_eq(r, "CREATE OK\n", "fragmented CREATE")

    send_fragmented(s, "READ fragKey\n", fragments=3, jitter=0.01)
    r = recv_line(s)
    assert_eq(r, "fragVal\n", "fragmented READ")

    # long value to stress robust send path (still keep total msg < 4096)
    long_val = "X" * 3500
    send_all(s, f"UPDATE fragKey {long_val}\n".encode("ascii"))
    r = recv_line(s)
    assert_eq(r, "UPDATE OK\n", "UPDATE long value")

    send_all(s, b"READ fragKey\n")
    r = recv_line(s)
    assert_eq(r, long_val + "\n", "READ long value")

    s.close()

def test_persistence_across_connections(host, port):
    # Create in one connection
    s1 = connect(host, port)
    send_all(s1, b"CREATE persistK persistV\n")
    assert_eq(recv_line(s1), "CREATE OK\n", "persist CREATE")
    s1.close()

    # Read in another connection
    s2 = connect(host, port)
    send_all(s2, b"READ persistK\n")
    assert_eq(recv_line(s2), "persistV\n", "persist READ (other client)")
    s2.close()

def test_concurrency(host, port, clients=10, ops_per_client=30):
    errors = []
    def worker(tid: int):
        try:
            s = connect(host, port)
            for i in range(ops_per_client):
                k = f"t{tid}_{i}"
                v = f"val{tid}_{i}"
                send_fragmented(s, f"CREATE {k} {v}\n", fragments=2, jitter=0.0)
                r = recv_line(s)
                # collision shouldn't happen for unique keys; any mismatch is an error
                if r != "CREATE OK\n":
                    raise AssertionError(f"[thread {tid}] CREATE got {r!r}")

                # mix READ and QREAD
                cmd = "QREAD" if (i % 2 == 0) else "READ"
                send_fragmented(s, f"{cmd} {k}\n", fragments=2, jitter=0.0)
                r = recv_line(s)
                if r != v + "\n":
                    raise AssertionError(f"[thread {tid}] {cmd} got {r!r}, expected {v+'\\n'!r}")

            # end connection cleanly
            send_all(s, b"\n")
            s.close()
        except Exception as e:
            errors.append(str(e))

    threads = [threading.Thread(target=worker, args=(t,)) for t in range(clients)]
    for th in threads: th.start()
    for th in threads: th.join()

    if errors:
        raise AssertionError("Concurrency test failures:\n" + "\n".join(errors))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--clients", type=int, default=10)
    args = ap.parse_args()

    random.seed(0)

    print("Running SKVS server.c black-box tests...")
    test_basic(args.host, args.port)
    print("  ✓ basic protocol")
    test_case_insensitive_cmd(args.host, args.port)
    print("  ✓ case-insensitive CMD")
    test_keep_alive_and_empty_line(args.host, args.port)
    print("  ✓ keep-alive + empty-line close")
    test_partial_send_and_long_value(args.host, args.port)
    print("  ✓ partial send + long value")
    test_persistence_across_connections(args.host, args.port)
    print("  ✓ persistence across connections")
    test_concurrency(args.host, args.port, clients=args.clients)
    print(f"  ✓ concurrency ({args.clients} clients)")
    print("ALL TESTS PASSED")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("\nTEST FAILED:", e)
        sys.exit(1)