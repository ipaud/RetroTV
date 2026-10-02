"""The test HTTP client decodes every framing to the same bytes (so streaming tests do not
depend on how a given uvicorn version frames a response)."""

from __future__ import annotations

import socket
import threading

import pytest

from http_client import HttpResponse

BODY = b"\xff\xd8\xff" + bytes(range(256)) * 40 + b"\xff\xd9"


def serve_once(raw: bytes) -> socket.socket:
    """A one-shot server that sends `raw` in small pieces and closes; returns a client socket."""
    server = socket.socket()
    server.bind(("127.0.0.1", 0))
    server.listen(1)

    def run() -> None:
        conn, _ = server.accept()
        conn.recv(4096)
        for i in range(0, len(raw), 777):  # awkward splits across the framing
            conn.sendall(raw[i:i + 777])
        conn.close()
        server.close()

    threading.Thread(target=run, daemon=True).start()
    client = socket.create_connection(server.getsockname(), timeout=5)
    client.sendall(b"GET / HTTP/1.1\r\n\r\n")
    return client


def chunked(body: bytes) -> bytes:
    parts = [body[i:i + 1000] for i in range(0, len(body), 1000)]
    out = b"".join(b"%x;ext=1\r\n" % len(p) + p + b"\r\n" for p in parts)
    return out + b"0\r\n\r\n"


@pytest.mark.parametrize("raw", [
    b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(BODY) + BODY,
    b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(BODY),
    b"HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n" + BODY,
], ids=["content-length", "chunked", "until-close"])
def test_every_framing_gives_the_same_body(raw: bytes) -> None:
    with serve_once(raw) as s:
        r = HttpResponse(s)
        assert r.status == 200
        assert r.read(10) == BODY[:10]
        assert r.read(len(BODY)) == BODY[10:]  # fewer when the body ends
        assert r.read(10) == b""


def test_broken_chunked_framing_is_an_error() -> None:
    raw = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nabcdeXX"
    with serve_once(raw) as s, pytest.raises(AssertionError):
        HttpResponse(s).read(100)
