#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A pretend Hue bridge, for trying the app without one (e.g. in the emulator).

    tools/fakebridge.py [--port 8080] [--fixture tests/common/fixture.json] [--cert c.pem --key k.pem]

Serves the CLIP v2 resources from the fixture, applies PUTs to them (rooms follow their lights,
scenes set their lights) and sends the resulting events on /eventstream/clip/v2. Pairing answers
"press the link button" twice, then hands out a key.

In the app, enter http://10.0.2.2:8080 as the address from the emulator, or http://<pc>:8080 from
a phone. With --cert/--key it serves HTTPS instead, to try certificate pinning:
    openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=001788fffe00cafe -keyout k.pem -out c.pem
"""

import argparse
import copy
import json
import queue
import ssl
import threading
import time
import uuid
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

BRIDGE_ID = "001788fffe00cafe"


class Bridge:
    def __init__(self, fixture):
        self.lock = threading.Lock()
        self.resources = {r["id"]: r for r in copy.deepcopy(fixture["data"])}
        self.listeners = []
        self.keys = set()
        self.pair_attempts = 0

    # Relations, as the app resolves them.
    def lights_of(self, group):
        lights = []
        for child in group.get("children", []):
            if child["rtype"] == "light":
                lights.append(child["rid"])
            elif child["rtype"] == "device":
                device = self.resources.get(child["rid"], {})
                lights += [s["rid"] for s in device.get("services", []) if s["rtype"] == "light"]
        return [l for l in lights if l in self.resources]

    def groups_with_grouped_light(self):
        for res in self.resources.values():
            if res["type"] in ("room", "zone", "bridge_home"):
                for s in res.get("services", []):
                    if s["rtype"] == "grouped_light" and s["rid"] in self.resources:
                        yield res, self.resources[s["rid"]]

    def all_lights(self):
        return [r["id"] for r in self.resources.values() if r["type"] == "light"]

    def apply(self, rtype, rid, body):
        """Applies a PUT and returns the changed resources (partial, as the bridge sends them)."""
        with self.lock:
            res = self.resources.get(rid)
            if res is None or res["type"] != rtype:
                return None
            changes = []
            if rtype == "light":
                changes += self.set_light(rid, body)
            elif rtype == "grouped_light":
                owner = self.resources[res["owner"]["rid"]]
                targets = self.all_lights() if owner["type"] == "bridge_home" else self.lights_of(owner)
                for light in targets:
                    changes += self.set_light(light, body)
            elif rtype == "scene" and "recall" in body:
                for action in res.get("actions", []):
                    changes += self.set_light(action["target"]["rid"], action["action"])
                for scene in self.resources.values():
                    if scene["type"] == "scene" and scene["group"] == res["group"]:
                        active = "static" if scene is res else "inactive"
                        if scene["status"]["active"] != active:
                            scene["status"]["active"] = active
                            changes.append({"id": scene["id"], "type": "scene", "status": {"active": active}})
            elif "enabled" in body:
                res["enabled"] = body["enabled"]
                changes.append({"id": rid, "type": rtype, "enabled": body["enabled"]})
            changes += self.update_groups()
            return changes

    def set_light(self, rid, body):
        light = self.resources.get(rid)
        if light is None:
            return []
        change = {"id": rid, "type": "light", "owner": light["owner"]}
        if "on" in body:
            light["on"] = body["on"]
            change["on"] = body["on"]
        if "dimming" in body and "dimming" in light:
            light["dimming"]["brightness"] = body["dimming"]["brightness"]
            change["dimming"] = {"brightness": body["dimming"]["brightness"]}
        if "color" in body and "color" in light:
            light["color"]["xy"] = body["color"]["xy"]
            change["color"] = {"xy": body["color"]["xy"]}
            if "color_temperature" in light:
                light["color_temperature"].update(mirek=None, mirek_valid=False)
                change["color_temperature"] = {"mirek": None, "mirek_valid": False}
        if "color_temperature" in body and "color_temperature" in light:
            mirek = body["color_temperature"]["mirek"]
            light["color_temperature"].update(mirek=mirek, mirek_valid=True)
            change["color_temperature"] = {"mirek": mirek, "mirek_valid": True}
        return [change] if len(change) > 3 else []

    def update_groups(self):
        changes = []
        for group, grouped in self.groups_with_grouped_light():
            lights = self.all_lights() if group["type"] == "bridge_home" else self.lights_of(group)
            on_lights = [self.resources[l] for l in lights if self.resources[l]["on"]["on"]]
            on = bool(on_lights)
            levels = [l["dimming"]["brightness"] for l in on_lights if "dimming" in l]
            brightness = round(sum(levels) / len(levels), 2) if levels else 0.0
            if grouped["on"]["on"] != on or grouped["dimming"]["brightness"] != brightness:
                grouped["on"] = {"on": on}
                grouped["dimming"] = {"brightness": brightness}
                changes.append({"id": grouped["id"], "type": "grouped_light",
                                "on": {"on": on}, "dimming": {"brightness": brightness}})
        return changes

    def publish(self, changes, kind="update"):
        if not changes:
            return
        event = [{
            "creationtime": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "data": changes,
            "id": str(uuid.uuid4()),
            "type": kind,
        }]
        message = f"id: {int(time.time())}:0\ndata: {json.dumps(event)}\n\n".encode()
        for listener in list(self.listeners):
            listener.put(message)


def make_handler(bridge, verbose):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):
            if verbose:
                super().log_message(fmt, *args)

        def send_json(self, status, payload):
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def authorized(self):
            if self.headers.get("hue-application-key") in bridge.keys:
                return True
            self.send_json(403, {"errors": [{"description": "unauthorized user"}]})
            return False

        def read_body(self):
            length = int(self.headers.get("Content-Length") or 0)
            return json.loads(self.rfile.read(length) or b"{}")

        def do_GET(self):
            if self.path == "/api/0/config":
                self.send_json(200, {"name": "Fake bridge", "bridgeid": BRIDGE_ID.upper(),
                                     "modelid": "BSB002", "apiversion": "1.70.0", "swversion": "1970000000"})
            elif self.path == "/clip/v2/resource":
                if self.authorized():
                    with bridge.lock:
                        self.send_json(200, {"errors": [], "data": list(bridge.resources.values())})
            elif self.path == "/eventstream/clip/v2":
                if self.authorized():
                    self.stream()
            else:
                self.send_json(404, {"errors": [{"description": "not found"}]})

        def stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b": hi\n\n")
            self.wfile.flush()
            events = queue.Queue()
            bridge.listeners.append(events)
            try:
                while True:
                    try:
                        self.wfile.write(events.get(timeout=30))
                    except queue.Empty:
                        continue
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass
            finally:
                bridge.listeners.remove(events)
                self.close_connection = True

        def do_POST(self):
            if self.path != "/api":
                self.send_json(404, [])
                return
            body = self.read_body()
            bridge.pair_attempts += 1
            if bridge.pair_attempts % 3:
                print("pairing: pretending the link button wasn't pressed yet")
                self.send_json(200, [{"error": {"type": 101, "address": "",
                                                "description": "link button not pressed"}}])
                return
            key = uuid.uuid4().hex
            bridge.keys.add(key)
            print(f"pairing: gave {body.get('devicetype')} key {key}")
            self.send_json(200, [{"success": {"username": key, "clientkey": uuid.uuid4().hex.upper()}}])

        def do_PUT(self):
            parts = self.path.strip("/").split("/")
            if len(parts) != 5 or parts[:3] != ["clip", "v2", "resource"]:
                self.send_json(404, {"errors": [{"description": "not found"}]})
                return
            if not self.authorized():
                return
            rtype, rid = parts[3], parts[4]
            body = self.read_body()
            changes = bridge.apply(rtype, rid, body)
            if changes is None:
                self.send_json(404, {"errors": [{"description": f"resource {rtype}/{rid} not found"}]})
                return
            if verbose:
                print(f"PUT {rtype}/{rid} {json.dumps(body)}")
            self.send_json(200, {"errors": [], "data": [{"rid": rid, "rtype": rtype}]})
            # The real bridge sends events about a second later, batched.
            threading.Timer(0.3, bridge.publish, [changes]).start()

    return Handler


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--fixture", default=root / "tests" / "common" / "fixture.json")
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--key-for-testing", action="append", default=[],
                        help="accept this application key without pairing")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    bridge = Bridge(json.loads(Path(args.fixture).read_text()))
    bridge.keys.update(args.key_for_testing)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), make_handler(bridge, args.verbose))
    server.daemon_threads = True
    scheme = "http"
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    print(f"fake bridge on {scheme}://0.0.0.0:{args.port} (emulator: {scheme}://10.0.2.2:{args.port})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
