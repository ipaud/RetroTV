"""A raw HTTP/1.1 client for the streaming tests, reading the way the TV does: status line and
headers, then the body decoded whatever its framing (Content-Length, chunked, or until the
server closes). The tests then check the real bytes, never the transport."""

from __future__ import annotations

import socket


class HttpResponse:
    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._raw = b""
        head = self._until(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        self.status_line = lines[0]
        self.status = int(lines[0].split()[1])
        self.headers = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            self.headers[name.strip().lower()] = value.strip()
        self._chunked = "chunked" in self.headers.get("transfer-encoding", "").lower()
        length = self.headers.get("content-length")
        self._left = int(length) if length is not None and not self._chunked else None
        self._chunk_left = 0
        self._done = False

    def _fill(self) -> bool:
        data = self._sock.recv(65536)
        self._raw += data
        return bool(data)

    def _until(self, marker: bytes) -> bytes:
        while marker not in self._raw:
            if not self._fill():
                raise ConnectionError("connection closed inside the HTTP framing")
        before, _, self._raw = self._raw.partition(marker)
        return before

    def _take(self, n: int) -> bytes:
        while len(self._raw) < n:
            if not self._fill():
                break
        data, self._raw = self._raw[:n], self._raw[n:]
        return data

    def read(self, n: int) -> bytes:
        """The next n bytes of the body, fewer only if the body ends first."""
        out = b""
        while len(out) < n and not self._done:
            if self._chunked:
                if self._chunk_left == 0:
                    size = int(self._until(b"\r\n").split(b";")[0], 16)
                    if size == 0:
                        self._done = True
                        break
                    self._chunk_left = size
                data = self._take(min(n - len(out), self._chunk_left))
                self._chunk_left -= len(data)
                if self._chunk_left == 0:
                    assert self._take(2) == b"\r\n", "chunk not followed by CRLF"
            elif self._left is not None:
                data = self._take(min(n - len(out), self._left))
                self._left -= len(data)
                self._done = self._left == 0
            else:  # until the server closes
                data = self._take(n - len(out))
                self._done = len(data) < n - len(out)
            if not data:
                self._done = True
            out += data
        return out


def get(host: str, port: int, path: str, timeout: float = 5) -> tuple[socket.socket, HttpResponse]:
    s = socket.create_connection((host, port), timeout=timeout)
    s.sendall(f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nConnection: close\r\n\r\n".encode())
    return s, HttpResponse(s)
