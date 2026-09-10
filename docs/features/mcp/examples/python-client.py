#!/usr/bin/env python3
"""Minimal MCP client for the Unreal-NG emulator server.

Demonstrates the Streamable HTTP transport with progress streaming:
initialize, list tools, then call inspect_state with a progress callback
(the SDK attaches a progressToken and surfaces notifications/progress).

Requirements:
    python >= 3.10
    pip install "mcp>=1.9"

Usage:
    python python-client.py
    python python-client.py --url http://127.0.0.1:8092/mcp

The emulator app must be running (WebAPI :8090 + MCP :8092).
"""

import argparse
import asyncio

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client


async def run(url: str) -> None:
    async with streamablehttp_client(url) as (read_stream, write_stream, _):
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()

            tools = await session.list_tools()
            print("tools:", ", ".join(t.name for t in tools.tools))

            def on_progress(progress: float, total: float | None, message: str | None) -> None:
                total_text = f"/{total:g}" if total is not None else ""
                print(f"  progress {progress:g}{total_text} {message or ''}")

            result = await session.call_tool(
                "inspect_state",
                {"aspects": ["registers", "disasm"]},
                progress_callback=on_progress,
            )
            print(result.content[0].text)


def main() -> None:
    parser = argparse.ArgumentParser(description="Unreal-NG MCP demo client")
    parser.add_argument("--url", default="http://127.0.0.1:8092/mcp", help="MCP endpoint")
    args = parser.parse_args()
    asyncio.run(run(args.url))


if __name__ == "__main__":
    main()
