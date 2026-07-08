"""Data channel abstraction layer.

Channel chain (priority order):
  1. BRIDGE — injected bridge DLL (shared memory, fastest)
  2. RPM   — external ReadProcessMemory via meccha-core.dll (fallback)

The channel manager auto-selects the best available channel.
"""
import enum
import time
from typing import Optional, Dict, Any

class ChannelType(enum.Enum):
    NONE = "none"
    BRIDGE = "bridge"
    RPM = "rpm"

class ChannelState(enum.Enum):
    DISABLED = "disabled"
    ACTIVE = "active"
    FAILED = "failed"
    STANDBY = "standby"

channel_chain: list[ChannelType] = [ChannelType.BRIDGE, ChannelType.RPM]
channel_states: dict[ChannelType, ChannelState] = {
    ChannelType.BRIDGE: ChannelState.DISABLED,
    ChannelType.RPM: ChannelState.STANDBY,
}
active_channel: ChannelType = ChannelType.NONE
last_switch_time: float = 0.0


def get_status() -> list[dict]:
    """Return channel chain status for MONITOR display."""
    result = []
    for ch in channel_chain:
        state = channel_states.get(ch, ChannelState.DISABLED)
        result.append({
            "name": ch.value.upper(),
            "state": state.value,
            "active": ch == active_channel,
        })
    return result


def get_active() -> ChannelType:
    return active_channel


def is_bridge_available() -> bool:
    """Check if bridge DLL is connected via TCP."""
    try:
        from meccha_chameleon_tools.camouflage import is_bridge_alive
        return is_bridge_alive()
    except Exception:
        return False


def try_switch_to(target: ChannelType) -> bool:
    """Attempt to switch to the specified channel."""
    global active_channel, last_switch_time

    if target == ChannelType.BRIDGE:
        if is_bridge_available():
            channel_states[ChannelType.BRIDGE] = ChannelState.ACTIVE
            channel_states[ChannelType.RPM] = ChannelState.STANDBY
            active_channel = ChannelType.BRIDGE
            last_switch_time = time.time()
            return True
        channel_states[ChannelType.BRIDGE] = ChannelState.FAILED
        return False

    elif target == ChannelType.RPM:
        from meccha_chameleon_tools.core import _USE_CORE
        if _USE_CORE:
            channel_states[ChannelType.RPM] = ChannelState.ACTIVE
            channel_states[ChannelType.BRIDGE] = ChannelState.STANDBY
            active_channel = ChannelType.RPM
            last_switch_time = time.time()
            return True
        channel_states[ChannelType.RPM] = ChannelState.FAILED
        return False

    return False


def ensure_channel() -> ChannelType:
    """Auto-select best available channel."""
    for ch in channel_chain:
        if try_switch_to(ch):
            return ch
    active_channel = ChannelType.NONE
    return ChannelType.NONE
