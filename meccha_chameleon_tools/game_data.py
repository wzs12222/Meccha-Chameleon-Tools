"""Game data structures shared between channels and rendering."""
import ctypes
from typing import Optional, List, Tuple


class PlayerData(ctypes.Structure):
    """Mirrors C++ PlayerEntry struct. 200 bytes per player."""
    _fields_ = [
        ("pawn_addr", ctypes.c_uint64),
        ("ps_addr", ctypes.c_uint64),
        ("pos_x", ctypes.c_float),
        ("pos_y", ctypes.c_float),
        ("pos_z", ctypes.c_float),
        ("health", ctypes.c_float),
        ("shield", ctypes.c_float),
        ("is_hunter", ctypes.c_bool),
        ("is_survivor", ctypes.c_bool),
        ("is_enemy", ctypes.c_bool),
        ("is_local", ctypes.c_bool),
        ("is_spectating", ctypes.c_bool),
        ("is_invincible", ctypes.c_bool),
        ("rot_x", ctypes.c_float),
        ("rot_y", ctypes.c_float),
        ("rot_z", ctypes.c_float),
        ("role_name", ctypes.c_char * 32),
        ("_pad", ctypes.c_byte * (200 - 8 - 8 - 12*4 - 5*1 - 32)),
    ]


class CameraData(ctypes.Structure):
    _fields_ = [
        ("loc_x", ctypes.c_float),
        ("loc_y", ctypes.c_float),
        ("loc_z", ctypes.c_float),
        ("rot_x", ctypes.c_float),
        ("rot_y", ctypes.c_float),
        ("rot_z", ctypes.c_float),
        ("fov", ctypes.c_float),
    ]


class GameSnapshot(ctypes.Structure):
    """Complete game state snapshot from bridge DLL shared memory."""
    _fields_ = [
        ("timestamp", ctypes.c_uint64),
        ("camera", CameraData),
        ("player_count", ctypes.c_int32),
        ("players", PlayerData * 64),
    ]


SHARED_MEM_NAME = "MecchaGameData"
SNAPSHOT_SIZE = ctypes.sizeof(GameSnapshot)


def snapshot_to_dict(snap: GameSnapshot) -> dict:
    """Convert a GameSnapshot to the dict format expected by paintEvent."""
    cam = snap.camera
    camera = {
        "loc": (cam.loc_x, cam.loc_y, cam.loc_z),
        "rot": (cam.rot_x, cam.rot_y, cam.rot_z),
        "fov": cam.fov,
    }

    players = []
    for i in range(min(snap.player_count, 64)):
        p = snap.players[i]
        if p.pawn_addr == 0:
            continue
        players.append({
            "pos": (p.pos_x, p.pos_y, p.pos_z),
            "actor": p.pawn_addr,
            "player_state": p.ps_addr,
            "idx": i,
            "role": p.role_name.decode("utf-8", errors="replace").strip("\x00"),
            "is_hunter": bool(p.is_hunter),
            "is_survivor": bool(p.is_survivor),
            "is_enemy": bool(p.is_enemy),
            "is_local": bool(p.is_local),
            "is_spectating": bool(p.is_spectating),
            "_health_info": (float(p.health), float(p.shield)),
            "_invincible": bool(p.is_invincible),
            "_rot": (float(p.rot_x), float(p.rot_y), float(p.rot_z)),
        })

    return {"cam": camera, "players": players}
