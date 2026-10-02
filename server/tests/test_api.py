"""RETROTV Server API against synthetic channels (see conftest.py). Streaming is tested against
a real uvicorn in test_streaming.py: TestClient never hangs up on an endless response."""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient
from pydantic import ValidationError

from app.config import check_prebuffer_ms, live_profile, live_start_index
from app.models import ChannelConfig
from app.providers.base import Airing, UnavailableProvider


def test_health(client: TestClient) -> None:
    assert client.get("/health").json() == {"status": "ok"}


def test_channels_list_with_online_state(client: TestClient) -> None:
    channels = {c["number"]: c for c in client.get("/api/channels").json()["channels"]}
    assert list(channels) == [1, 2, 3, 10]
    assert channels[1]["online"] and channels[2]["online"]
    assert not channels[3]["online"]  # file missing
    assert not channels[10]["online"]  # provider not in this version


def test_status(client: TestClient) -> None:
    status = client.get("/api/status/1").json()
    assert status["online"] and status["indexed"] and status["duration_ms"] == 3000
    assert status["position_ms"] == 1000  # the indexed second at or before 1.5 s
    assert "/" not in str(status)  # no file paths over the network
    assert client.get("/api/status/3").json()["reason"].startswith("video file missing")
    assert client.get("/api/status/99").status_code == 404


def test_session_for_unknown_or_offline_channel(client: TestClient) -> None:
    assert client.post("/api/sessions/99").status_code == 404
    assert client.post("/api/sessions/3").status_code == 503
    assert client.post("/api/sessions/10").status_code == 503


def test_session_fixes_one_instant(client: TestClient) -> None:
    info = client.post("/api/sessions/1").json()
    assert info["channel"] == 1 and info["position_ms"] == 1000
    assert info["video"] == f"/session/{info['session_id']}/video"
    assert info["audio"] == f"/session/{info['session_id']}/audio"


def test_session_tells_frame_rate_and_prebuffer(client: TestClient) -> None:
    info = client.post("/api/sessions/1").json()
    assert info["fps"] == 24 and info["prebuffer_ms"] == 1500  # files are 24 fps; server default


def test_profile_and_prebuffer_are_validated() -> None:
    assert live_profile("20fps-q18") == (20, 18)
    with pytest.raises(ValueError, match="unknown live profile"):
        live_profile("30fps-q2")
    assert check_prebuffer_ms(1000) == 1000 and check_prebuffer_ms(3000) == 3000
    for bad in (999, 3001):
        with pytest.raises(ValueError):
            check_prebuffer_ms(bad)
    assert live_start_index(-3) == -3
    for bad in (0, -7):
        with pytest.raises(ValueError):
            live_start_index(bad)
    base = {"id": 1, "number": 1, "name": "X", "provider": "3cat"}
    assert ChannelConfig(**base, profile="18fps-q16", prebuffer_ms=2500).prebuffer_ms == 2500
    for bad in ({"profile": "fast"}, {"prebuffer_ms": 5000}):
        with pytest.raises(ValidationError):
            ChannelConfig(**base, **bad)


def test_bench_is_bounded(client: TestClient) -> None:
    assert client.get("/api/bench?seconds=0").status_code == 422
    assert client.get("/api/bench?seconds=31").status_code == 422


def test_unindexed_channel_starts_at_zero(client: TestClient) -> None:
    assert client.post("/api/sessions/2").json()["position_ms"] == 0


def test_unknown_session(client: TestClient) -> None:
    assert client.get("/session/nope/video").status_code == 404
    assert client.get("/session/nope/audio").status_code == 404


def test_session_nobody_opens_expires(client: TestClient, clock) -> None:
    info = client.post("/api/sessions/1").json()
    assert client.get("/api/status/1").json()["sessions"] == 1
    clock.now += 16
    assert client.get("/api/status/1").json()["sessions"] == 0
    assert client.get(info["video"]).status_code == 404


def test_oldest_session_goes_when_full(client: TestClient) -> None:
    first = client.post("/api/sessions/1").json()
    client.post("/api/sessions/1")
    client.post("/api/sessions/2")  # max_sessions = 2 in the test settings
    assert client.get(first["video"]).status_code == 404


def test_guide_lists_what_is_on_now_and_next(client: TestClient, monkeypatch) -> None:
    now = 3000 * 1000 + 1500
    assert client.get("/api/guide").json() == {"now_ms": now, "channels": []}  # no source publishes one
    airings = [Airing("OVER", now - 2000, now - 1000), Airing("NOW " + "x" * 80, now - 1000, now + 1000),
               Airing("NEXT", now + 1000, now + 2000), Airing("THEN", now + 2000, now + 3000),
               Airing("LATER", now + 3000, now + 4000)]
    monkeypatch.setattr(UnavailableProvider, "guide", lambda self, now_ms: airings)
    sx3 = next(c for c in client.get("/api/guide").json()["channels"] if c["number"] == 10)
    assert [a["title"][:4] for a in sx3["airings"]] == ["NOW ", "NEXT", "THEN"]  # ended ones dropped, 3 at most
    assert len(sx3["airings"][0]["title"]) == 64
    assert sx3["airings"][1] == {"title": "NEXT", "start_ms": now + 1000, "end_ms": now + 2000}
