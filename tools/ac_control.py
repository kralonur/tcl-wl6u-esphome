#!/usr/bin/env python3
"""Encrypted API action runner with raw TX/RX logs. No action means logs only.

Example: python -m tools.ac_control --host YOUR_DEVICE_IP ac_set \
    '{"property":"targetTemperature","value":24.5}'
A successful API call does NOT mean the AC accepted the command.
"""
import argparse
import asyncio
import json
from pathlib import Path

from aioesphomeapi import APIClient
import yaml


async def run(args):
    secrets = yaml.safe_load((Path(__file__).resolve().parents[1] / "firmware/secrets.yaml").read_text())
    client = APIClient(args.host, 6053, "", noise_psk=secrets["api_encryption_key"])
    try:
        await client.connect(login=True)
        _, services = await client.list_entities_services()
        print("Actions:", ", ".join(service.name for service in services) or "none")
        client.subscribe_logs(
            lambda log: print(log.message.decode(errors="replace") if isinstance(log.message, bytes) else log.message),
            log_level=5, dump_config=True,
        )
        if args.action:
            service = next((s for s in services if s.name == args.action), None)
            if service is None:
                raise ValueError("Action is not advertised by this firmware; no command sent")
            values = json.loads(args.values)
            if not isinstance(values, dict) or set(values) != {arg.name for arg in service.args}:
                raise ValueError("Supply exactly these named arguments: " + ", ".join(arg.name for arg in service.args))
            await client.execute_service(service, values)
            print("API action delivered; check TX/RX logs. AC acceptance is NOT confirmed.")
        await asyncio.sleep(args.seconds)
    finally:
        await client.disconnect()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?")
    parser.add_argument("values", nargs="?", default="{}", help="JSON object of action arguments")
    parser.add_argument("--host", required=True, help="Explicit device IP address or hostname")
    parser.add_argument("--seconds", type=float, default=12)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
