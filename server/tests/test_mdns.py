"""The mDNS announcement follows the computer to another network (office -> home)."""

from __future__ import annotations

import asyncio

import zeroconf.asyncio

from app import mdns


class FakeZeroconf:
    registered: list[bytes] = []

    def __init__(self, **_kwargs) -> None:
        self.closed = False

    async def async_register_service(self, info) -> None:
        FakeZeroconf.registered.append(info.addresses[0])

    async def async_unregister_service(self, info) -> None:
        pass

    async def async_close(self) -> None:
        self.closed = True


def test_announces_again_when_the_address_changes(monkeypatch) -> None:
    addresses = iter(["192.168.1.29", "192.168.1.29", "192.168.1.178"])
    monkeypatch.setattr(mdns, "lan_ip", lambda: next(addresses, "192.168.1.178"))
    monkeypatch.setattr(zeroconf.asyncio, "AsyncZeroconf", FakeZeroconf)
    FakeZeroconf.registered = []

    async def run() -> None:
        stop = await mdns.announce(8080, check_s=0.01)
        await asyncio.sleep(0.1)
        await stop()

    asyncio.run(run())
    assert FakeZeroconf.registered == [bytes([192, 168, 1, 29]), bytes([192, 168, 1, 178])]
