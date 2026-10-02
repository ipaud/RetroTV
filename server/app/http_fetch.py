"""Small HTTP GETs for providers (playlists, official APIs), bounded in time and size. Providers
take it as a parameter, so tests replace the network with canned answers."""

from __future__ import annotations

import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Callable

USER_AGENT = "Mozilla/5.0 (compatible; RETROTV-Server/0.2; personal TV at home)"

Fetch = Callable[[str], str]


class FetchError(Exception):
    """The reason, ready for the API ("HTTP 404", "timed out", ...)."""


def fetch_text(url: str, timeout_s: float = 5.0, max_bytes: int = 2_000_000, headers: dict | None = None) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
    try:
        with urllib.request.urlopen(request, timeout=timeout_s) as response:
            data = response.read(max_bytes + 1)
    except urllib.error.HTTPError as e:
        raise FetchError(f"HTTP {e.code}") from e
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        reason = getattr(e, "reason", e)
        raise FetchError(f"unreachable: {reason}") from e
    if len(data) > max_bytes:
        raise FetchError("answer too large")
    return data.decode("utf-8", "replace")


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):  # noqa: ANN002, ANN003 - urllib's signature
        return None


def fetch_redirect(url: str, timeout_s: float = 5.0) -> str:
    """Where a redirecting official endpoint points to, without following it (so the caller can
    check the host before anything else is fetched)."""
    opener = urllib.request.build_opener(_NoRedirect)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with opener.open(request, timeout=timeout_s) as response:
            raise FetchError(f"no redirect (HTTP {response.status})")
    except urllib.error.HTTPError as e:
        location = e.headers.get("Location") if 300 <= e.code < 400 else None
        if not location:
            raise FetchError(f"HTTP {e.code}") from e
        return urllib.parse.urljoin(url, location)
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        raise FetchError(f"unreachable: {getattr(e, 'reason', e)}") from e
