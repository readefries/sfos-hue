#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Dump the retained zigbee2mqtt bridge topics (devices, groups) as JSON.

Minimal MQTT 3.1.1 client, no dependencies. The password is read from
MQTT_PASSWORD or prompted for.

    tools/z2mdump.py <host> [user] [--port 1883] [--base zigbee2mqtt] [--out z2m.json]
"""
import argparse
import getpass
import json
import os
import socket
import struct
import sys


def varint(n):
    out = bytearray()
    while True:
        byte, n = n % 128, n // 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def string(s):
    data = s.encode()
    return struct.pack("!H", len(data)) + data


def packet(kind, body):
    return bytes([kind]) + varint(len(body)) + body


def read_exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("connection closed by broker")
        data += chunk
    return bytes(data)


def read_packet(sock):
    header = read_exact(sock, 1)[0]
    length, shift = 0, 0
    while True:
        byte = read_exact(sock, 1)[0]
        length |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            break
    return header, read_exact(sock, length)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("user", nargs="?")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--base", default="zigbee2mqtt")
    parser.add_argument("--out", default="z2m.json")
    args = parser.parse_args()

    password = None
    if args.user:
        password = os.environ.get("MQTT_PASSWORD") or getpass.getpass("MQTT password: ")

    flags = 0x02  # clean session
    payload = string("lampje-dump-%d" % os.getpid())
    if args.user:
        flags |= 0x80
        payload += string(args.user)
    if password:
        flags |= 0x40
        payload += string(password)
    connect = string("MQTT") + bytes([4, flags]) + struct.pack("!H", 30) + payload

    wanted = {args.base + "/bridge/devices": "devices", args.base + "/bridge/groups": "groups"}
    sock = socket.create_connection((args.host, args.port), timeout=10)
    sock.sendall(packet(0x10, connect))
    header, body = read_packet(sock)
    if header >> 4 != 2 or body[1] != 0:
        codes = {1: "bad protocol", 2: "client id rejected", 3: "server unavailable",
                 4: "bad user name or password", 5: "not authorized"}
        sys.exit("connect refused: %s" % codes.get(body[1], body[1]))

    topics = b"".join(string(t) + b"\x00" for t in wanted)
    sock.sendall(packet(0x82, struct.pack("!H", 1) + topics))

    result = {}
    while len(result) < len(wanted):
        try:
            header, body = read_packet(sock)
        except socket.timeout:
            break
        if header >> 4 == 9 and any(code == 0x80 for code in body[2:]):
            sys.exit("subscribe refused (check the ACL for %s/#)" % args.base)
        if header >> 4 != 3:
            continue
        (length,) = struct.unpack("!H", body[:2])
        topic = body[2:2 + length].decode()
        offset = 2 + length + (2 if header & 0x06 else 0)
        if topic in wanted:
            result[wanted[topic]] = json.loads(body[offset:])
    sock.sendall(b"\xe0\x00")  # DISCONNECT
    sock.close()

    if not result:
        sys.exit("no retained bridge topics received (is the base topic %r?)" % args.base)
    with open(args.out, "w") as f:
        json.dump(result, f, indent=2)
    devices = result.get("devices", [])
    print("%d devices, %d groups -> %s" % (len(devices), len(result.get("groups", [])), args.out))
    for device in devices:
        definition = device.get("definition") or {}
        print("  %-28s %-20s %s" % (device.get("friendly_name"), definition.get("model", "-"),
                                    definition.get("description", "")))


if __name__ == "__main__":
    main()
