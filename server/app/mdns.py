"""Announces the server on the LAN as retrotv-server.local (mDNS), so channels.json on the TV can say
http://retrotv-server.local:8080/... instead of an IP that DHCP may change."""

from __future__ import annotations

import asyncio
import contextlib
import logging
import socket
from collections.abc import Awaitable, Callable

log = logging.getLogger("SERVER")

HOSTNAME = "retrotv-server.local."
IP_CHECK_S = 15.0


def lan_ip() -> str:
    """The address this computer uses on the LAN (no packet is sent)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect(("192.0.2.1", 9))  # TEST-NET: only picks the outgoing interface
        return s.getsockname()[0]


async def announce(port: int, check_s: float = IP_CHECK_S) -> Callable[[], Awaitable[None]] | None:
    """Registers retrotv-server.local; returns what undoes it, or None when mDNS is not possible.
    Async: uvicorn's event loop is already running, and zeroconf's sync API refuses to block it.
    A laptop moves between networks: the address is checked every `check_s` and announced
    again when it changes, or the TV would keep looking for the server at the old one."""
    try:
        from zeroconf import IPVersion, ServiceInfo
        from zeroconf.asyncio import AsyncZeroconf
    except ImportError as e:
        log.warning("mDNS unavailable (%r): use the IP address", e)
        return None

    async def register(ip: str):
        info = ServiceInfo(
            "_http._tcp.local.",
            "RETROTV Server._http._tcp.local.",
            addresses=[socket.inet_aton(ip)],
            port=port,
            server=HOSTNAME,
            properties={"api": "/api/channels"},
        )
        zc = AsyncZeroconf(ip_version=IPVersion.V4Only)
        await zc.async_register_service(info)
        log.info("announced %s -> %s:%d", HOSTNAME.rstrip("."), ip, port)
        return zc, info

    async def unregister(zc, info) -> None:
        try:
            await zc.async_unregister_service(info)
            await zc.async_close()
        except Exception as e:  # the old network may be gone: nobody left to tell
            log.info("mDNS: old announcement dropped (%r)", e)

    try:
        ip = lan_ip()
        zc, info = await register(ip)
    except Exception as e:  # mDNS is a convenience: the IP always works
        log.warning("mDNS announce failed (%r): use the IP address", e)
        return None

    async def follow() -> None:
        nonlocal ip, zc, info
        while True:
            await asyncio.sleep(check_s)
            try:
                now = lan_ip()
            except OSError:
                continue  # no network at the moment
            if now == ip:
                continue
            log.info("LAN address changed %s -> %s: announcing again", ip, now)
            await unregister(zc, info)
            ip = now
            try:
                zc, info = await register(now)
            except Exception as e:
                log.warning("mDNS announce failed (%r): trying again", e)
                ip = ""  # a different address next time: another try

    task = asyncio.create_task(follow())

    async def stop() -> None:
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task
        await unregister(zc, info)

    return stop
