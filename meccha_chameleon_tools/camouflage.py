import os, json, socket, subprocess, sys, time, ctypes, ctypes.wintypes, hashlib, shutil
from pathlib import Path

BRIDGE_PORT = 50262
GAME_PROCESS = "PenguinHotel-Win64-Shipping.exe"
CREATE_NO_WINDOW = 0x08000000


def _resource_path(relative):
    try:
        base = sys._MEIPASS
    except AttributeError:
        base = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, relative)


def _find_game_pid(game_process=GAME_PROCESS):
    kernel32 = ctypes.windll.kernel32
    TH32CS_SNAPPROCESS = 0x00000002
    MAX_PATH = 260

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", ctypes.wintypes.DWORD),
            ("cntUsage", ctypes.wintypes.DWORD),
            ("th32ProcessID", ctypes.wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
            ("th32ModuleID", ctypes.wintypes.DWORD),
            ("cntThreads", ctypes.wintypes.DWORD),
            ("th32ParentProcessID", ctypes.wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", ctypes.wintypes.DWORD),
            ("szExeFile", ctypes.c_wchar * MAX_PATH),
        ]

    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == ctypes.wintypes.HANDLE(-1).value:
        return None
    try:
        pe = PROCESSENTRY32W()
        pe.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(pe)):
            return None
        while True:
            if pe.szExeFile.lower() == game_process.lower():
                return pe.th32ProcessID
            if not kernel32.Process32NextW(snapshot, ctypes.byref(pe)):
                return None
    finally:
        kernel32.CloseHandle(snapshot)


def bridge_ping(timeout=2):
    try:
        s = socket.create_connection(("127.0.0.1", BRIDGE_PORT), timeout)
        s.sendall(b'{"type":"ping"}\n')
        s.settimeout(timeout)
        data = s.recv(4096)
        s.close()
        if data:
            return json.loads(data.decode().strip()).get("success", False)
        return False
    except (socket.timeout, ConnectionRefusedError, OSError, json.JSONDecodeError):
        return False


def is_bridge_alive() -> bool:
    return bridge_ping(timeout=2)


def bridge_send(payload_json, timeout=30):
    try:
        s = socket.create_connection(("127.0.0.1", BRIDGE_PORT), timeout)
        data = (payload_json if payload_json.endswith("\n") else payload_json + "\n").encode("utf-8")
        s.sendall(data)
        s.settimeout(timeout)
        result = s.recv(65536)
        s.close()
        if result:
            resp = json.loads(result.decode().strip())
            return resp.get("success", False), resp.get("message", ""), resp.get("stage", "")
        return False, "No response", ""
    except (socket.timeout, ConnectionRefusedError, OSError, json.JSONDecodeError) as e:
        return False, str(e), ""


def _send_tcp(payload: dict, timeout=30) -> dict:
    ok, msg, stage = bridge_send(json.dumps(payload, separators=(",", ":")), timeout)
    return {"success": ok, "message": msg, "stage": stage}


def _find_and_kill_bridge():
    kernel32 = ctypes.windll.kernel32
    TH32CS_SNAPPROCESS = 0x00000002
    MAX_PATH = 260

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [
            ("dwSize", ctypes.wintypes.DWORD),
            ("cntUsage", ctypes.wintypes.DWORD),
            ("th32ProcessID", ctypes.wintypes.DWORD),
            ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
            ("th32ModuleID", ctypes.wintypes.DWORD),
            ("cntThreads", ctypes.wintypes.DWORD),
            ("th32ParentProcessID", ctypes.wintypes.DWORD),
            ("pcPriClassBase", ctypes.c_long),
            ("dwFlags", ctypes.wintypes.DWORD),
            ("szExeFile", ctypes.c_wchar * MAX_PATH),
        ]

    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == ctypes.wintypes.HANDLE(-1).value:
        return
    try:
        pe = PROCESSENTRY32W()
        pe.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(pe)):
            return
        while True:
            if pe.szExeFile.lower() == "runtime-injector.exe":
                handle = kernel32.OpenProcess(0x0001, False, pe.th32ProcessID)
                if handle:
                    kernel32.TerminateProcess(handle, 1)
                    kernel32.CloseHandle(handle)
            if not kernel32.Process32NextW(snapshot, ctypes.byref(pe)):
                return
    finally:
        kernel32.CloseHandle(snapshot)


def _to_unit(b):
    return round(b / 255.0, 8)


def _parse_color(hex_color):
    if hex_color.startswith("#") and len(hex_color) == 7:
        return int(hex_color[1:3], 16), int(hex_color[3:5], 16), int(hex_color[5:7], 16)
    return 255, 255, 255


def _enable_debug_privilege() -> bool:
    """Enable SeDebugPrivilege for this process. Helps OpenProcess succeed."""
    try:
        class LUID_AND_ATTRIBUTES(ctypes.Structure):
            _fields_ = [("Luid", ctypes.wintypes.LUID), ("Attributes", ctypes.wintypes.DWORD)]
        class TOKEN_PRIVILEGES(ctypes.Structure):
            _fields_ = [("PrivilegeCount", ctypes.wintypes.DWORD), ("Privileges", LUID_AND_ATTRIBUTES * 1)]
        hToken = ctypes.wintypes.HANDLE()
        TOKEN_ADJUST_PRIVILEGES = 0x0020
        TOKEN_QUERY = 0x0008
        SE_PRIVILEGE_ENABLED = 0x2
        if not ctypes.windll.advapi32.OpenProcessToken(
            ctypes.windll.kernel32.GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
            ctypes.byref(hToken)
        ):
            return False
        try:
            luid = ctypes.wintypes.LUID()
            ctypes.windll.advapi32.LookupPrivilegeValueW(None, "SeDebugPrivilege", ctypes.byref(luid))
            tp = TOKEN_PRIVILEGES()
            tp.PrivilegeCount = 1
            tp.Privileges[0].Luid = luid
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED
            ctypes.windll.advapi32.AdjustTokenPrivileges(hToken, False, ctypes.byref(tp), 0, None, None)
            return True
        finally:
            ctypes.windll.kernel32.CloseHandle(hToken)
    except Exception:
        return False


def inject_bridge(game_process=GAME_PROCESS) -> str:
    native_dir = Path(_resource_path("native"))
    bridge_dll = native_dir / "runtime-bridge.dll"
    loader_dll = native_dir / "bridge-loader.dll"
    injector_exe = native_dir / "runtime-injector.exe"

    if not bridge_dll.exists() or not loader_dll.exists() or not injector_exe.exists():
        return f"Native files not found in {native_dir}"

    pid = _find_game_pid(game_process)
    if pid is None:
        return f"Game process '{game_process}' not found"

    runtime_dir = Path(os.environ.get("LOCALAPPDATA", ".")) / "MecchaCamouflage" / "lite" / "runtime"
    runtime_dir.mkdir(parents=True, exist_ok=True)

    hash_val = hashlib.sha256(bridge_dll.read_bytes()).hexdigest()[:16]
    bridge_name = f"runtime-bridge-{hash_val}-{BRIDGE_PORT}.dll"

    loader_dir = runtime_dir / "loaders"
    bridge_dir = runtime_dir / "bridges"
    loader_dir.mkdir(parents=True, exist_ok=True)
    bridge_dir.mkdir(parents=True, exist_ok=True)

    dest_loader = loader_dir / "bridge-loader.dll"
    dest_bridge = bridge_dir / bridge_name
    dest_injector = loader_dir / "runtime-injector.exe"

    shutil.copy2(str(loader_dll), str(dest_loader))
    shutil.copy2(str(injector_exe), str(dest_injector))
    shutil.copy2(str(bridge_dll), str(dest_bridge))

    (bridge_dir / (bridge_name + ".port")).write_text(str(BRIDGE_PORT) + "\n")
    progress_path = runtime_dir / f"bridge-{hash_val}-{BRIDGE_PORT}.progress.json"
    (bridge_dir / (bridge_name + ".progress.path")).write_text(str(progress_path) + "\n")

    mesh_dir = Path(_resource_path("mesh-profiles"))
    profiles_target = bridge_dir / "mesh-profiles"
    profiles_target.mkdir(parents=True, exist_ok=True)
    if mesh_dir.exists():
        for pf in mesh_dir.glob("*.json"):
            shutil.copy2(str(pf), str(profiles_target / pf.name))

    status_path = bridge_dir / f"bridge-loader-{pid}-{BRIDGE_PORT}.status.json"
    config_path = bridge_dir / f"bridge-loader-{pid}-{BRIDGE_PORT}.config.json"
    config = {
        "protocol": 1,
        "gamePid": pid,
        "pipeName": f"\\\\.\\pipe\\MecchaCamouflage.Loader.{pid}.v1",
        "statusPath": str(status_path),
        "path": str(dest_bridge),
        "sha256": hashlib.sha256(bridge_dll.read_bytes()).hexdigest(),
        "buildId": Path(bridge_name).stem,
        "runtimeDir": str(bridge_dir),
        "logDir": str(runtime_dir / "logs"),
        "port": BRIDGE_PORT,
        "progressPath": str(progress_path),
    }
    config_path.write_text(json.dumps(config, separators=(",", ":")))

    try:
        result = subprocess.run(
            [str(dest_injector), game_process, str(dest_loader), str(config_path)],
            capture_output=True, text=True, timeout=15,
            creationflags=CREATE_NO_WINDOW,
        )
        if result.returncode != 0:
            return f"Injector failed ({result.returncode}): {result.stderr.strip()}"
        return ""
    except subprocess.TimeoutExpired:
        return "Injector timed out"
    except Exception as e:
        return str(e)


def inject_dll_simple(dll_path: str, game_process=GAME_PROCESS) -> str:
    """Inject a DLL into the game process using CreateRemoteThread."""
    pid = _find_game_pid(game_process)
    if pid is None:
        return f"Game process '{game_process}' not found"
    dll_path = str(Path(dll_path).resolve())
    if not os.path.isfile(dll_path):
        return f"DLL not found: {dll_path}"
    kernel32 = ctypes.windll.kernel32
    PROCESS_ALL_ACCESS = 0x1F0FFF
    handle = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not handle:
        return f"OpenProcess failed (PID {pid})"
    try:
        path_bytes = dll_path.encode("utf-8")
        alloc = kernel32.VirtualAllocEx(handle, None, len(path_bytes) + 1, 0x3000, 0x04)
        if not alloc:
            return "VirtualAllocEx failed"
        written = ctypes.c_size_t(0)
        kernel32.WriteProcessMemory(handle, alloc, path_bytes, len(path_bytes) + 1, ctypes.byref(written))
        thread_id = ctypes.c_ulong(0)
        thread = kernel32.CreateRemoteThread(handle, None, 0,
            kernel32.GetProcAddress(kernel32.GetModuleHandleW("kernel32.dll"), "LoadLibraryW"),
            alloc, 0, ctypes.byref(thread_id))
        if not thread:
            return "CreateRemoteThread failed"
        kernel32.WaitForSingleObject(thread, 5000)
        kernel32.CloseHandle(thread)
        kernel32.VirtualFreeEx(handle, alloc, 0, 0x8000)
        return ""
    finally:
        kernel32.CloseHandle(handle)


def inject_game_reader(game_process=GAME_PROCESS) -> str:
    """Inject the game-reader.dll for shared memory game data."""
    from meccha_chameleon_tools import logger as log
    native_dir = Path(_resource_path("native"))
    dll_path = native_dir / "game-reader.dll"
    if not dll_path.exists():
        return f"game-reader.dll not found at {dll_path}"
    err = inject_dll_simple(str(dll_path), game_process)
    if err:
        log.warn(f"game-reader injection failed: {err}")
        return err
    # Open shared memory
    from meccha_chameleon_tools import game_data
    time.sleep(0.5)
    log.info("game-reader.dll injected, shared memory ready")
    return ""


def ensure_bridge_ready(game_process=GAME_PROCESS) -> str:
    """Inject bridge DLL with retry + privilege escalation."""
    from meccha_chameleon_tools import logger as log
    if is_bridge_alive():
        log.info("Bridge already connected")
        return ""
    _enable_debug_privilege()
    for attempt in range(3):
        log.info(f"Bridge injection attempt {attempt+1}/3")
        err = inject_bridge(game_process)
        if err:
            log.warn(f"Injection attempt {attempt+1} failed: {err}")
            time.sleep(1)
            continue
        for _ in range(20):
            if is_bridge_alive():
                log.info("Bridge DLL injected successfully")
                return ""
            time.sleep(0.25)
    log.warn("Bridge injection failed after 3 attempts — will use external RPM fallback")
    return "Bridge did not become ready after 3 attempts"


def _build_tuning(config=None):
    r, g, b = _parse_color(getattr(config, "fill_color", "#FFFFFF") if config else "#FFFFFF")
    return {
        "stroke_size_texels": getattr(config, "stroke_size_texels", 9.0) if config else 9.0,
        "coverage_step_texels": getattr(config, "coverage_step_texels", 9.0) if config else 9.0,
        "side_source_max_uv": getattr(config, "side_source_max_uv", 0.08) if config else 0.08,
        "front_back_source_max_uv": getattr(config, "front_back_source_max_uv", 0.45) if config else 0.45,
        "server_batch_limit": getattr(config, "server_batch_limit", 50) if config else 50,
        "server_batch_delay_ms": getattr(config, "server_batch_delay_ms", 150) if config else 150,
        "auto_material": getattr(config, "auto_material", False) if config else False,
        "auto_material_properties": False,
        "metallic": getattr(config, "metallic", 0.0) if config else 0.0,
        "roughness": getattr(config, "roughness", 1.0) if config else 1.0,
        "front_region_mode": getattr(config, "front_region_mode", "fill") if config else "fill",
        "side_region_mode": getattr(config, "side_region_mode", "paint") if config else "paint",
        "back_region_mode": getattr(config, "back_region_mode", "paint") if config else "paint",
        "enable_front_paint": getattr(config, "enable_front_paint", False) if config else False,
        "enable_side_paint": getattr(config, "enable_side_paint", True) if config else True,
        "enable_back_paint": getattr(config, "enable_back_paint", True) if config else True,
        "fill_color": getattr(config, "fill_color", "#FFFFFF") if config else "#FFFFFF",
        "fill_color_r": _to_unit(r),
        "fill_color_g": _to_unit(g),
        "fill_color_b": _to_unit(b),
        "fill_metallic": getattr(config, "fill_metallic", 1.0) if config else 1.0,
        "fill_roughness": getattr(config, "fill_roughness", 0.0) if config else 0.0,
    }


def paint_now(config=None) -> dict:
    game = getattr(config, "game_process_name", GAME_PROCESS) if config else GAME_PROCESS
    pid = _find_game_pid(game)
    payload = {
        "type": "paint_full_route",
        "native_apply_mode": "mesh_first_paint",
        "route": "f10_mesh_first_paint",
        "preview_only": False,
        "unpreview_only": False,
        "research_artifacts": False,
        "process": {"pid": pid, "name": game},
        "tuning": _build_tuning(config),
    }
    return _send_tcp(payload)


def stop_paint() -> dict:
    return _send_tcp({"type": "cancel_paint"})


def shutdown_bridge() -> dict:
    resp = _send_tcp({"type": "shutdown"})
    _find_and_kill_bridge()
    return resp


def paint_start(config=None) -> dict:
    return paint_now(config)


def paint_single(config=None) -> dict:
    return paint_now(config)


def send_preview(config=None) -> dict:
    game = getattr(config, "game_process_name", GAME_PROCESS) if config else GAME_PROCESS
    pid = _find_game_pid(game)
    payload = {
        "type": "paint_full_route",
        "native_apply_mode": "mesh_first_paint",
        "route": "f10_mesh_first_paint",
        "preview_only": True,
        "unpreview_only": False,
        "research_artifacts": False,
        "process": {"pid": pid, "name": game},
        "tuning": _build_tuning(config),
    }
    return _send_tcp(payload)


def send_unpreview(config=None) -> dict:
    game = getattr(config, "game_process_name", GAME_PROCESS) if config else GAME_PROCESS
    pid = _find_game_pid(game)
    payload = {
        "type": "paint_full_route",
        "native_apply_mode": "mesh_first_paint",
        "route": "f10_mesh_first_paint",
        "preview_only": False,
        "unpreview_only": True,
        "research_artifacts": False,
        "process": {"pid": pid, "name": game},
        "tuning": _build_tuning(config),
    }
    return _send_tcp(payload)
