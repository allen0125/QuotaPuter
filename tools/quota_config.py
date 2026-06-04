#!/usr/bin/env python3
"""QuotaPuter provisioning tool.

Writes Wi-Fi and provider credentials to a Cardputer running QuotaPuter over its
USB-Serial-JTAG port, using the newline-delimited JSON protocol implemented by
components/provisioner. Secrets are read without echo and sent device-inward
only; nothing sensitive is written to disk or printed back.

Examples
--------
    python tools/quota_config.py --port /dev/tty.usbmodem1101 list
    python tools/quota_config.py --port /dev/tty.usbmodem1101 set-wifi
    python tools/quota_config.py --port /dev/tty.usbmodem1101 add-provider minimax_cn
    python tools/quota_config.py --port /dev/tty.usbmodem1101 add-provider kimi --key sk-...
    python tools/quota_config.py --port /dev/tty.usbmodem1101 add-provider openai \\
        --relay-url https://your-relay.example.com/openai
    python tools/quota_config.py --port /dev/tty.usbmodem1101 remove-provider minimax_cn
    python tools/quota_config.py --port /dev/tty.usbmodem1101 factory-reset
"""
import argparse
import getpass
import json
import sys
import time

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError:
    sys.exit("This tool requires pyserial. Install it with: pip install pyserial")

QP_PREFIX = "#QP "
RELAY_PROVIDERS = {"openai", "anthropic", "gemini", "codex"}
DIRECT_PROVIDERS = {"minimax_cn", "minimax_global", "kimi"}
ALL_PROVIDERS = sorted(RELAY_PROVIDERS | DIRECT_PROVIDERS)


def autodetect_port():
    candidates = [p.device for p in list_ports.comports()
                  if "usbmodem" in p.device or "ttyACM" in p.device or "ttyUSB" in p.device]
    return candidates[0] if len(candidates) == 1 else None


def send(ser, obj, timeout=15):
    """Send one JSON command and return the device's parsed #QP reply."""
    ser.reset_input_buffer()
    ser.write((json.dumps(obj) + "\n").encode())
    ser.flush()
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if text.startswith(QP_PREFIX):
            return json.loads(text[len(QP_PREFIX):])
    raise TimeoutError(
        "No response from device. Is QuotaPuter firmware running and the port correct?")


def cmd_hello(ser, _args):
    resp = send(ser, {"cmd": "hello"})
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    print(f"Connected to {resp.get('device')} ({resp.get('mode')} mode)")
    print("Providers:", ", ".join(resp.get("providers", [])))
    return 0


def cmd_list(ser, _args):
    resp = send(ser, {"cmd": "list"})
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    print(f"Wi-Fi configured: {'yes' if resp.get('wifi') else 'no'}")
    print(f"{'PROVIDER':<16} {'MODE':<7} {'CONFIGURED':<11} ENABLED")
    for p in resp.get("providers", []):
        print(f"{p['id']:<16} {p['mode']:<7} "
              f"{'yes' if p['configured'] else 'no':<11} "
              f"{'yes' if p['enabled'] else 'no'}")
    return 0


def cmd_set_wifi(ser, args):
    ssid = args.ssid or input("Wi-Fi SSID: ").strip()
    if not ssid:
        print("ERROR: SSID required")
        return 1
    password = args.password
    if password is None:
        password = getpass.getpass("Wi-Fi password (blank for open network): ")
    resp = send(ser, {"cmd": "set_wifi", "ssid": ssid, "password": password})
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    print("Wi-Fi credentials saved; device is connecting.")
    return 0


def cmd_add_provider(ser, args):
    pid = args.provider
    if pid not in ALL_PROVIDERS:
        print(f"ERROR: unknown provider '{pid}'. Known: {', '.join(ALL_PROVIDERS)}")
        return 1
    mode = args.mode or ("relay" if pid in RELAY_PROVIDERS else "direct")
    payload = {"cmd": "add_provider", "id": pid, "mode": mode,
               "enabled": not args.disabled}
    if mode == "relay":
        relay_url = args.relay_url or input("Relay URL: ").strip()
        if not relay_url:
            print("ERROR: relay URL required")
            return 1
        token = args.relay_token
        if token is None:
            token = getpass.getpass("Relay device token (blank if none): ")
        payload["relay_url"] = relay_url
        payload["relay_token"] = token
    else:
        key = args.key
        if not key:
            key = getpass.getpass(f"Paste key for {pid}: ").strip()
        if not key:
            print("ERROR: key required")
            return 1
        payload["secret"] = key

    print(f"Provisioning {pid} ({mode} mode) and running a test query...")
    resp = send(ser, payload, timeout=25)
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    if resp.get("connected"):
        print("CONNECTED — test query succeeded.")
    else:
        print(f"Saved, but the test query returned: {resp.get('test_message')}")
    return 0


def cmd_remove_provider(ser, args):
    resp = send(ser, {"cmd": "remove_provider", "id": args.provider})
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    print(f"Removed {args.provider}.")
    return 0


def cmd_factory_reset(ser, args):
    if not args.yes:
        confirm = input("This erases ALL credentials and Wi-Fi config. Type 'reset' to confirm: ")
        if confirm.strip() != "reset":
            print("Aborted.")
            return 1
    resp = send(ser, {"cmd": "factory_reset"}, timeout=20)
    if not resp.get("ok"):
        print("ERROR:", resp.get("error"))
        return 1
    print("Factory reset complete.")
    return 0


def build_parser():
    parser = argparse.ArgumentParser(description="QuotaPuter provisioning tool")
    parser.add_argument("--port", help="Serial port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (ignored by USB-Serial-JTAG, default 115200)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("hello", help="Handshake with the device")
    sub.add_parser("list", help="List providers and their status")

    sw = sub.add_parser("set-wifi", help="Set Wi-Fi credentials")
    sw.add_argument("--ssid")
    sw.add_argument("--password")

    ap = sub.add_parser("add-provider", help="Add/update a provider credential")
    ap.add_argument("provider", help=f"one of: {', '.join(ALL_PROVIDERS)}")
    ap.add_argument("--mode", choices=["direct", "relay"], help="override default auth mode")
    ap.add_argument("--key", help="direct-mode API key (omit to enter without echo)")
    ap.add_argument("--relay-url", dest="relay_url", help="relay base URL")
    ap.add_argument("--relay-token", dest="relay_token", help="relay device token")
    ap.add_argument("--disabled", action="store_true", help="store but leave disabled")

    rp = sub.add_parser("remove-provider", help="Remove a provider credential")
    rp.add_argument("provider")

    fr = sub.add_parser("factory-reset", help="Erase all credentials and Wi-Fi config")
    fr.add_argument("--yes", action="store_true", help="skip confirmation prompt")

    return parser


HANDLERS = {
    "hello": cmd_hello,
    "list": cmd_list,
    "set-wifi": cmd_set_wifi,
    "add-provider": cmd_add_provider,
    "remove-provider": cmd_remove_provider,
    "factory-reset": cmd_factory_reset,
}


def main():
    args = build_parser().parse_args()
    port = args.port or autodetect_port()
    if not port:
        sys.exit("Could not auto-detect a port; pass --port /dev/tty.usbmodemXXXX")
    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except serial.SerialException as exc:
        sys.exit(f"Failed to open {port}: {exc}")
    # Give the USB-Serial-JTAG link a moment to settle after open.
    time.sleep(0.3)
    try:
        return HANDLERS[args.command](ser, args)
    except (TimeoutError, json.JSONDecodeError) as exc:
        print("ERROR:", exc)
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())
