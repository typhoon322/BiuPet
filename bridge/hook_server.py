"""Tiny local HTTP server that receives Codex hook events."""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Awaitable, Callable

log = logging.getLogger("hook-server")

HookHandler = Callable[[dict], Awaitable[None]]


class HookServer:
    def __init__(self, host: str, port: int, handler: HookHandler):
        self._host = host
        self._port = port
        self._handler = handler
        self._server: asyncio.Server | None = None

    async def run(self, stop_event: asyncio.Event):
        self._server = await asyncio.start_server(self._handle, self._host, self._port)
        log.info("listening on http://%s:%d", self._host, self._port)
        async with self._server:
            await stop_event.wait()

    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            request_line = (await reader.readline()).decode("utf-8", "replace").strip()
            if not request_line:
                return
            parts = request_line.split(" ")
            method = parts[0] if parts else ""
            path = parts[1] if len(parts) > 1 else "/"

            headers: dict[str, str] = {}
            while True:
                line = (await reader.readline()).decode("utf-8", "replace").strip()
                if not line:
                    break
                if ":" in line:
                    k, _, v = line.partition(":")
                    headers[k.strip().lower()] = v.strip()

            body = b""
            content_length = int(headers.get("content-length", "0") or "0")
            while len(body) < content_length:
                body += await reader.read(content_length - len(body))

            if method == "POST" and path == "/api/hook":
                await self._handle_hook(body, writer)
            elif method == "GET" and path in ("/health", "/api/health"):
                await self._respond(writer, 200, b'{"ok":true}')
            else:
                await self._respond(writer, 404, b'{"error":"not found"}')
        except Exception as e:  # keep server alive on malformed input
            log.warning("hook request error: %s", e)
            try:
                await self._respond(writer, 400, b'{"error":"bad request"}')
            except Exception:
                pass
        finally:
            try:
                writer.close()
            except Exception:
                pass

    async def _handle_hook(self, body: bytes, writer: asyncio.StreamWriter):
        try:
            data = json.loads(body.decode("utf-8") or "{}")
        except json.JSONDecodeError:
            await self._respond(writer, 400, b'{"error":"invalid json"}')
            return
        if not isinstance(data, dict):
            await self._respond(writer, 400, b'{"error":"expected object"}')
            return
        log.info("hook event=%s state=%s", data.get("event"), data.get("state"))
        try:
            await self._handler(data)
        except Exception as e:
            log.warning("handler error: %s", e)
        await self._respond(writer, 200, b'{"ok":true}')

    @staticmethod
    async def _respond(writer: asyncio.StreamWriter, status: int, body: bytes):
        reason = {200: "OK", 400: "Bad Request", 404: "Not Found"}.get(status, "OK")
        writer.write(
            f"HTTP/1.1 {status} {reason}\r\n"
            "Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n\r\n".encode("utf-8")
        )
        writer.write(body)
        await writer.drain()
