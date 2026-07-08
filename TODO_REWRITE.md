# Meccha Chameleon Tools — 终结重建指南

基于当前项目状态分析, 最终敲定的重建方案。

---

## 当前项目状态摘要

```
Python 行数: ~3500 (ui.py 2000 + core.py 1100 + 其余 400)
C++ 行数:   ~700 (meccha-core.dll)
C++ 行数:   ~700 (runtime-bridge.dll, 不含 hypervision_bridge.cpp)

现有 DLL:
  meccha-core.dll    — 外部 RPM 引擎, 可用
  runtime-bridge.dll — 注入 DLL, 已有 TCP + 涂装, 需扩展
  runtime-injector.exe — 外部注入器, 有句柄问题

桥接 DLL 已具备:
  ├── TCP Server (port 50262)
  ├── paint_now / stop_paint
  ├── scan_terrain (禁用)
  └── HyperVision (禁用)
```

---

## 核心理念: 注入优先

**能注入就注入。不注入只作为降级备用。**

```
注入后:
  游戏进程内 (bridge DLL):
    ├── 直接指针读取 (无需 RPM)
    ├── 调用引擎函数 (GetActorsOfClass, Possess Hook)
    ├── 数据写入共享内存
    └── TCP 响应 Python 命令

  外部:
    ├── injector → 注入 bridge DLL
    └── Python → 通过 TCP/共享内存读取数据 → QPainter 渲染
```

---

## 重建阶段

### P0: 注入器重写 + 桥接 DLL 初始化

**目标**: 彻底解决 "Camo6" 句柄问题, 确保注入 100% 成功

**注入流程**:
```
1. CreateToolhelp32Snapshot → 找到游戏 PID
2. OpenProcess 尝试:
   ├── 首次: PROCESS_ALL_ACCESS → 失败则降级
   ├── 降级: PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
   ├── 仍失败 → 尝试启用 SeDebugPrivilege → 重试
   └── 仍失败 → 提示"请以管理员身份运行工具"
3. VirtualAllocEx → 写入 DLL 路径
4. CreateRemoteThread → LoadLibrary
5. 轮询 TCP :50262 → 确认 bridge 就绪
6. 超时 15s, 每 0.5s 轮询一次
```

**TODO**:
- [ ] 将注入逻辑从外部 EXE 移到 Python (或自包含 C++) 
- [ ] 添加权限降级 + SeDebugPrivilege
- [ ] 添加重试 + 超时机制
- [ ] 移除对 `runtime-injector.exe` 的依赖

---

### P0: 桥接 DLL — 全量游戏数据读取

**目标**: 替代整个 Python `_reader_loop`

**当前 Python reader loop 每周期做的事**:
```
1. get_camera()          → 5 次 RPM (world→pc→cam→pov→loc/rot/fov)
2. iter_players()        → 64+ 次 RPM (PlayerArray→每个 PS→Pawn→pos)
3. get_health() ×N       → 2×N 次 RPM
4. get_invincible() ×N   → 2×N 次 RPM
5. get_actor_rotation()  → 1×N 次 RPM
6. Delta 检测             → Python dict 操作
7. 写入 _cached_players  → 加锁
```

**桥接 DLL 替代方案**:
```cpp
// bridge DLL 内部, 每 100ms:
struct GameSnapshot {
    uint64_t timestamp;
    CameraData camera;        // 直接读 CameraCache
    int player_count;
    PlayerEntry players[64];  // 直接遍历 PlayerArray
};

// 所有读取都是进程内指针解引用: *(float*)(base + offset)
// 零 RPM, 零上下文切换

// 写入共享内存:
HANDLE hMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(GameSnapshot), "MecchaGameData");
void* pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GameSnapshot));
memcpy(pBuf, &snapshot, sizeof(GameSnapshot));
```

**共享内存大小**: `8 + 40 + 4 + 64*200 ≈ 13KB` — 极轻量

**Python 端**: `ctypes` 直接映射共享内存 → 零拷贝读取

**TODO**:
- [ ] 桥接 DLL 添加 `read_game_snapshot()` 函数
- [ ] 按 UE5 结构体布局直读所有字段
- [ ] 写入共享内存
- [ ] Python 端映射共享内存 → 替换 `_cached_players`
- [ ] 移除所有 ctypes→DLL→RPM 调用 (保留 meccha-core.dll 作为回退)

---

### P0: 桥接 DLL — Hook `Possess()` (原身体追踪)

**目标**: 零延迟 100% 追踪观战者原身体

```cpp
// MinHook / Detours
MH_CreateHook(
    (LPVOID)APlayerController_Possess,  // 函数地址
    &HookPossess,
    (LPVOID*)&OriginalPossess
);
MH_EnableHook((LPVOID)APlayerController_Possess);

void HookPossess(APlayerController* PC, APawn* NewPawn) {
    // NewPawn 是新 pawn (可能是 SpectatePawn 或 CharacterPawn)
    APawn* OldPawn = PC->Pawn;
    if (OldPawn && NewPawn) {
        // 保存原身体
        OriginalPawnInfo info;
        info.addr = (uint64_t)OldPawn;
        info.pos = OldPawn->RootComponent->RelativeLocation;
        // 判断职业
        FString name = OldPawn->GetClass()->GetName();
        info.role = name.Contains("Survivor") ? ROLE_SURVIVOR : ROLE_HUNTER;
        g_original_pawns[(uint64_t)PC->PlayerState] = info;
    }
    OriginalPossess(PC, NewPawn);
}
```

**查找 `Possess` 函数地址**: 通过 vtable 索引或 Pattern 扫描

**UE5 虚表结构**:
```
APlayerController vtable:
  [0]  ProcessEvent
  [1]  ...
  [N]  Possess  ← 固定索引 (UE4/5 多年未变: index 208 附近)
```

**TODO**:
- [ ] 确认 UE5 `APlayerController::Possess` 的 vtable 索引
- [ ] 集成 MinHook 到 bridge DLL
- [ ] 实现 Hook + 原身体记录
- [ ] 原身体信息写入共享内存 (与游戏数据共享内存合并)

---

### P0: Python 渲染端适配

**目标**: Python 端只做 QPainter 渲染, 零数据读取

```
重写前:
  Python: RPM → ctypes → dict → QPainter

重写后:
  Python: 共享内存 → ctypes.Structure → QPainter
           ↑ 零 RPM 调用, 零拷贝
           ↑ 直接映射 C 结构体到 Python
```

**Python 共享内存读取**:
```python
import mmap
import ctypes

class PlayerData(ctypes.Structure):
    _fields_ = [
        ("pawn_addr", ctypes.c_uint64),
        ("pos_x", ctypes.c_float),
        ("pos_y", ctypes.c_float),
        ("pos_z", ctypes.c_float),
        ("health", ctypes.c_float),
        ("is_hunter", ctypes.c_bool),
        ("is_survivor", ctypes.c_bool),
        ("is_spectating", ctypes.c_bool),
        # ...
    ]

class GameSnapshot(ctypes.Structure):
    _fields_ = [
        ("timestamp", ctypes.c_uint64),
        ("camera_loc_x", ctypes.c_float),
        ("camera_loc_y", ctypes.c_float),
        ("camera_loc_z", ctypes.c_float),
        ("fov", ctypes.c_float),
        ("player_count", ctypes.c_int32),
        ("players", PlayerData * 64),
    ]

fd = os.open("MecchaGameData", os.O_RDONLY)
buf = mmap.mmap(fd, ctypes.sizeof(GameSnapshot))
snapshot = GameSnapshot.from_buffer(buf)
```

**TODO**:
- [ ] 定义共享内存结构 (C++ ↔ Python 对齐)
- [ ] Python `mmap` + `ctypes.Structure` 映射
- [ ] 移除 `_reader_loop` 中所有 ctypes→DLL 调用
- [ ] 保留 `_reader_loop` 框架, 从共享内存直接填充 `_cached_players`

---

### P1: 跨版本动态解析

**问题**: Pattern 签名和属性偏移随游戏版本变化

**方案**: 三层解析 + 缓存持久化

```
第一层: Pattern 签名 (多年稳定)
├── GUObjectArray: 48 8D 05 xx xx xx xx 48 89 01 45 8B D1
├── FNamePool 备选: 多组签名 + 偏移试探
└── GEngine: find_first_instance("GameEngine")

第二层: 属性偏移 (UStruct 遍历)
├── OffsetResolver 已存在, 需稳定化
├── 回退链: primary → 备选1 → 备选2
└── 缓存到 %APPDATA%/MecchaCamouflage/offsets.json

第三层: 类名匹配
├── _detect_role: 直接名 → 父类链 → 备选关键词
└── 已有, 只需扩展备选词
```

**缓存格式**:
```json
{
    "game_version": "1.0.0.0",
    "pattern_guobject": 123456,
    "pattern_fname": 789012,
    "offsets": {
        "APlayerController::AcknowledgedPawn": 0x320,
        "APlayerState::PawnPrivate": 0x330,
        ...
    },
    "created_at": "2026-07-08T..."
}
```

**TODO**:
- [ ] OffsetResolver 加回退链
- [ ] 缓存写入/读取 `offsets.json`
- [ ] 缓存失效时自动重新解析
- [ ] 游戏版本号检测(从 PE 版本信息读取)

---

### P2: 遗留桥接 API

**当前桥接 DLL 能力 (要保留)**:
```
├── TCP Server (:50262)
├── paint_now / stop_paint
├── scan_terrain (禁用, 暂不恢复)
└── HyperVision (禁用, 暂不恢复)
```

**新增 API**:
```
├── POSSESS_HOOK     — 开启/关闭 Hook
├── GAME_SNAPSHOT    — 读取完整游戏数据快照
├── FIND_ORIGINAL    — 查询指定 PS 的原身体信息
└── PING             — 保持连接
```

---

---

## 设计要求: 数据透明化

**原则**: 用户必须清楚知道工具当前处于什么状态。不允许静默降级。

### 日志透明

```
每次状态变更, 写入明确日志:

[2026-07-08 12:00:00] INFO  Bridge DLL injected successfully (PID 56532)
[2026-07-08 12:00:05] WARN  Bridge DLL connection lost — retrying...
[2026-07-08 12:00:06] WARN  Injection failed (OpenProcess denied) — falling back to external RPM
[2026-07-08 12:00:06] WARN  meccha-core.dll RPM mode: performance reduced, body tracking disabled
[2026-07-08 12:00:10] INFO  Bridge DLL reconnected successfully
[2026-07-08 12:00:10] INFO  Switched from RPM fallback to bridge DLL
```

**日志分级**:
```
INFO  — 正常状态变更 (注入成功/重连成功)
WARN  — 降级/降权 (注入失败→RPM, OpenProcess 降权)
ERROR — 功能不可用 (DLL 加载失败, 权限不足)
```

### 降级透明

```
全功能 (桥接 DLL 注入)
├── 游戏数据: DLL 进程内直读
├── 身体追踪: Hook Possess (100% 可靠)
├── 模式扫描: GetActorsOfClass (即时)
└── 状态: "BRIDGE INJECTED"

降级 (meccha-core.dll RPM)
├── 游戏数据: RPM 批量读
├── 身体追踪: Delta 检测 (100ms 延迟)
├── 模式扫描: C++ Pattern 扫描 (<0.1s)
└── 状态: "RPM MODE — 部分功能受限"
```

每次降级/升级切换, STATUS 栏闪烁 3 秒提示用户。

---

## 设计要求: 监视器 (MONITOR Tab)

**在 DEBUG 标签页旁边或替代它, 新增 MONITOR 标签页。显示所有组件的实时状态。**

### 布局

```
┌─────────────────────────────────────────┐
│ INJECTION                              │
│  Bridge DLL: ● LOADED (PID 56532)      │
│  TCP :50262:  ● CONNECTED              │
│  Uptime:     01h23m                    │
│  Fallback:   NO (全功能)               │
├─────────────────────────────────────────┤
│ GAME DATA                              │
│  Reader:     ● ACTIVE (100ms cycle)    │
│  Source:     bridge DLL (direct read)  │
│  Players:    13 (4H / 7S / 2?)        │
│  Camera:     ● VALID (fov=90.0)        │
│  Body cache: 3 entries                 │
├─────────────────────────────────────────┤
│ PERFORMANCE                            │
│  Read cycle: 3.5ms                     │
│  RPM calls:  0 (direct read)           │
│  Render FPS: 30                        │
│  Memory:     47MB                      │
├─────────────────────────────────────────┤
│ CORE DLL                               │
│  meccha-core.dll: ● LOADED             │
│  Mode:         fallback (standby)      │
│  Process:      ● ATTACHED (PID 56532)  │
├─────────────────────────────────────────┤
│ LOG                                     │
│ [12:00:00] Bridge DLL injected          │
│ [12:00:00] Game data reader started     │
│ [12:00:05] Body capture: P7 → Hunter   │
└─────────────────────────────────────────┘
```

### 状态指示器

```
● GREEN  = 正常运行
● YELLOW = 降级运行 (有 fallback 但功能受限)
● RED    = 故障/断开
```

### 监视器数据源

所有监视数据通过**共享内存**从 bridge DLL 获取 (或降级时从 `_reader_loop` 获取):

```
struct MonitorData {
    uint64_t timestamp;
    // 注入状态
    bool bridge_injected;
    uint32_t bridge_pid;
    uint64_t bridge_uptime_ms;
    // 游戏数据状态
    bool reader_active;
    uint32_t read_cycle_ms;
    uint32_t player_count;
    uint32_t hunter_count;
    uint32_t survivor_count;
    bool camera_valid;
    float camera_fov;
    // 性能
    uint32_t render_fps_setting;
    uint32_t memory_mb;
    // 身体追踪
    uint32_t body_cache_count;
    uint32_t body_misses;
};
```

### 日志面板

- 最多保留 100 条
- 自动滚动到底部
- 支持 Ctrl+C 复制
- 日志文件路径显示 + 打开文件夹按钮 (已有)

---

## 质量门禁 (Quality Gates)

每个阶段完成后需验证:

| 门禁 | P0-1 | P0-2 | P0-3 | P0-4 | P1 | P2 |
|---|---|---|---|---|---|---|
| 注入成功率 > 95% | ✓ | | | | | |
| 降级日志明确 | ✓ | ✓ | ✓ | ✓ | | |
| 共享内存读取 < 0.1ms | | ✓ | | | | |
| Hook 捕获率 100% | | | ✓ | | | |
| Python 端零 RPM 调用 | | | | ✓ | | |
| 偏移量缓存加载 < 0.1s | | | | | ✓ | |
| 监视器数据正确 | | | | | | ✓ |
| MONITOR Tab 所有指示器正常 | | | | | | ✓ |

---

## 总结: 工作量估算

| 阶段 | 内容 | 涉及文件 | 估时 |
|---|---|---|---|
| P0-1 | 注入器重写 | `camouflage.py`, 新 `injector.cpp` | 3 天 |
| P0-2 | 桥接 DLL 游戏数据读取 + 共享内存 | `bridge.cpp`, 新 `game_reader.cpp` | 5 天 |
| P0-3 | Hook Possess | 新 `hook_possess.cpp`, `bridge.cpp` | 3 天 |
| P0-4 | Python 渲染端适配 | `ui.py` (重写 _reader_loop) | 2 天 |
| P1 | 跨版本解析 + 缓存 | `core.py` (修改 OffsetResolver) | 2 天 |
| P2 | 桥接 API 整合 | `bridge.cpp` | 1 天 |

**总计**: ~16 天。P0 核心 13 天, P1-P2 3 天。

**重写后架构**:
```
桥接 DLL (游戏内)
├── TCP Server (:50262)
├── GameSnapshot → 共享内存 (13KB, 100ms)
├── Hook Possess → 原身体追踪
├── MinHook 引擎
└── 涂装命令 (保留)

Python (外部)
├── 注入器 (重写, 自包含)
├── 共享内存读取 → _cached_players (零 RPM)
├── QPainter 渲染
└── 涂装命令 (保留)
```

**不注入降级**: meccha-core.dll (RPM) + Python 回退路径。保留当前代码作为 fallback。
