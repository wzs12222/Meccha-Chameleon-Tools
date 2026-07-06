#!/usr/bin/env python3
"""
HyperVision — Exposure Volume Mapping System.
TCP bridge commands + Python fallback terrain scanning.
"""
import json
import math
import os
import socket
import time
from typing import List, Tuple

BRIDGE_HOST = "127.0.0.1"
BRIDGE_PORT = 47654


def _send(cmd: str, payload: dict = None, timeout: float = 30) -> dict:
    msg = json.dumps({
        "type": cmd,
        "request_id": f"{os.urandom(8).hex()}{int(time.time())}",
        "timestamp_utc": int(time.time()),
        "payload": payload or {},
    }) + "\n"
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((BRIDGE_HOST, BRIDGE_PORT))
        s.sendall(msg.encode())
        raw = b""
        while b"\n" not in raw:
            chunk = s.recv(65536)
            if not chunk:
                break
            raw += chunk
        line = raw.split(b"\n")[0]
        return json.loads(line) if line else {"success": False}
    except Exception:
        return {"success": False}
    finally:
        s.close()


def is_bridge_alive() -> bool:
    resp = _send("ping", timeout=3)
    return resp.get("success") is True


def bridge_scan_terrain(center_x: float, center_y: float, center_z: float,
                        range_xy: float = 5000.0,
                        z_samples: int = 3,
                        z_range: float = 1000.0) -> dict:
    return _send("scan_terrain", {
        "center": [center_x, center_y, center_z],
        "range_xy": range_xy,
        "z_samples": z_samples,
        "z_range": z_range,
    }, timeout=60)


def bridge_visibility_scan(target_x: float, target_y: float, target_z: float,
                           step: float = 80.0,
                           z_layers: int = 20,
                           radius: float = 2000.0) -> dict:
    return _send("visibility_scan", {
        "target": [target_x, target_y, target_z],
        "step": step,
        "z_layers": z_layers,
        "radius": radius,
    }, timeout=120)


def bridge_path_find(player_x: float, player_y: float, player_z: float,
                     target_x: float, target_y: float, target_z: float,
                     exposure_cloud: List[List[float]]) -> dict:
    return _send("path_find", {
        "player_pos": [player_x, player_y, player_z],
        "target_pos": [target_x, target_y, target_z],
        "exposure_cloud": exposure_cloud,
    }, timeout=30)


def simplify_segments(segments: List[Tuple], angle_thresh: float = 15.0) -> List[Tuple]:
    """Douglas–Peucker simplification per Z level."""
    if len(segments) < 3:
        return segments
    by_level = {}
    for seg in segments:
        zl = seg[5]
        by_level.setdefault(zl, []).append(seg)
    result = []
    for zl, segs in by_level.items():
        seen_lines = set()
        unique = []
        for s in segs:
            key = (round(s[0], -1), round(s[1], -1), round(s[2], -1), round(s[3], -1))
            if key not in seen_lines:
                seen_lines.add(key)
                unique.append(s)
        result.extend(unique)
    return result
