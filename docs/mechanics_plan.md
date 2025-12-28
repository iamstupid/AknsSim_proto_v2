# 机制实现计划（route / block / attack / unbalance）

本文件把 `docs/(route|block|attack|unbalance)_docs.md` 的“程序化规格”与当前额外约束整理成一份可执行的实现计划，并用于后续逐步落地（含单测）。

---

## 0. 统一约定（来自澄清）

### 0.1 坐标系

- 世界坐标：`x` 向右、`y` 向上。
- 地块中心为整数点，格边界在半整数。
- `TileAt(pos)` 必须使用 **银行家舍入**（round-half-to-even）。

### 0.2 Tick 与时间

- `SimState::tick_`：从开始到现在累计 tick 数。
- `1 tick = 1 / 2^30 s`。
- `tick_rate_`：每次 `SimState::step()` 前进的 tick 数，且**由 `set_tick_rate(num, denom)` 向上取整**得到（现有实现）。
- 本阶段所有“每 3 帧”机制统一实现成 **0.1s 周期**（即 `Tick k = ceil((1/10) * 2^30)`，用 tick 倒计时即可；无需额外处理长期漂移）。

### 0.3 飞行单位

- **飞行单位不需要 PathMap**：直接朝目标直飞（givenDir = normalize(targetPoint - cursorPos)）。

---

## 1. 地图（Map）与地块信息（Tile）

### 1.1 每格必须存的类型信息

每个 tile 至少需要以下字段：

- `melee_deployable`：可部署近战
- `ranged_deployable`：可部署远程
- `is_obstacle`：障碍物（寻路代价额外 +1000）
- `is_unpassable`：不可通行（寻路距离视为 `max_int`，且不可穿越）
- `is_hole`：地穴（寻路代价额外 +1_000_000；并且任何**非飞行**单位站上去会直接死亡）

> 注：同一格可以同时具有 deployable 与 obstacle/unpassable/hole 等属性；最终行为由查询函数决定（例如 `Passable`、`ObstaclePenalty`、`IsHole`）。

### 1.2 Map 必须支持的操作

- 查询：地块类型（flags）
- 改变：地块类型（变更时 `++mapVersion`）
- 查询：某 tile 在某 PathMap 下的寻路距离（`distToTarget` / `distToEnd`）

### 1.3 与寻路相关的查询接口（建议）

- `bool Passable(tile, MoveMode)`：`is_unpassable` 为 false 且（地面/飞行规则）满足
- `int ObstaclePenalty(tile, MoveMode)`：
  - Ground：`is_obstacle => 1000`，`is_hole => 1_000_000`，否则 0
  - Air：恒 0（且通常不需要 PathMap）
- `bool IsHole(tile)`：运行阶段用于“站上即死”的判定

---

## 2. 空间加速：SpatialGrid（两张）

目标：把大量 “范围找对象 / 最近对象” 从 O(N) 降到 O(格子数 + 候选数)。

### 2.1 Entry 格式（建议）

每条 entry 至少包含：

- `type_flags`（enemy/ally/ground/air/...）
- `entity_idx`（索引；真正访问组件时仍用 `idx+gen` 校验）
- `center_position`（世界坐标）

### 2.2 两张 grid

- `occupation_grid`：实体面积与地面投影相交就加入（用于大多数索敌/阻挡）
- `center_grid`：
  - circle：仅当中心落在该格时加入（中心格由 TileAt 的银行家舍入确定）
  - rectangle：与 occupation 相同

### 2.3 构建时机

每次移动结算结束后重建：

1) 运行移动系统（RouteMove / ForcedMove / Unbalance 等）
2) **build SpatialGrids**
3) 运行依赖空间查询的系统（block/attack/targeting...）

---

## 3. 线段穿越缓存：BresenhamCache（0.4 宽线）

### 3.1 需求

- 缓存的是 **宽 0.4** 的线段穿越结果，不是理想线段。
- 实现方式：取两条平行线的“穿越 tile 集合”的并集：
  - `(0,  +0.2) -> (dx, dy + 0.2)`
  - `(0,  -0.2) -> (dx, dy - 0.2)`
- 需要“标记穿过的每一个 tile”（建议实现为 supercover / grid traversal）。

### 3.2 使用场景

- PathMap 的 `nextNode` 平整化（LineClear）
- 未来可能用于“缩地板索敌”等直线检查

---

## 4. 寻路缓存：PathMapCache（支持地图动态变化）

### 4.1 缓存键与失效

- 地图存在可改变路径的箱子等：**必须有 `mapVersion` 并做 cache invalidation**。
- 最小缓存键建议：
  - `mapVersion`
  - `MoveMode`（Ground / Air）
  - `targetTile`
  - `allowDiagonalMove`

最简单的正确实现：`mapVersion` 变化时清空所有缓存。

### 4.2 构建流程（route_docs）

1) SPFA 反向扩展：邻居顺序固定 `上→右→下→左`
2) 生成 `nextNode_raw`
3) 若允许平整化：用 BresenhamCache 的 0.4 宽线进行 `LineClear`，生成 `nextNode_smooth`
4) route 级别倒序计算 `distToEnd`

### 4.3 Air（飞行）处理

- Air 不需要 PathMap：运行时直接朝目标点直飞。
- 若仍需要做“终点距离估算”，可用欧氏距离或忽略（先不做）。

---

## 5. RouteRuntime（移动与检查点）

按 `route_docs.md` 的“每帧流程”落地（本项目以 step/tick 为单位）：

1) 预推进检查点（避免朝已完成 CP 移动）
2) MOVE 状态执行移动五步
3) 更新当前检查点（计时类）
4) 再推进检查点（允许一帧跳多个）
5) 终点判定

避障力缓存、阻挡扫描频率等 “每 3 帧” 都使用 `TickTimer(0.1s)`。

---

## 6. Block（阻挡）

按 `block_docs.md` 落地：

- 0.1s 扫描一次（SpatialGrid）
- 选择最近未被阻挡敌人
- 计算稳定阻挡点（单目标 MIN_SEPARATION + 多目标冲突修正）
- 0.2s 强制移动到稳定点（`stableBlockPos` 立即记录）

---

## 7. AttackAbility（攻击离散化）

按 `attack_docs.md` 落地：

- 以 tick 作为权威时间
- 更新顺序强约束：
  1) TriggerCheck（先于冷却）
  2) CooldownUpdate（且第 1 tick 跳过一次）
  3) CastUpdate（PreDelay/PostDelay/CastEnd；CastEnd 内 resetCDStrategy 后 FinishCallback）

---

## 8. Unbalance（推/拉/钩）

按 `unbalance_docs.md` 落地：

- mass=1 的“伪物理”可直接用速度向量模拟（便于可存档）
- Push = Impulse（直接改变速度）
- Pull/Hook = Force（每 step 施力），ratio^4 衰减
- 摩擦力匀减速，保护时间 0.1s，退出条件包含“无有效拉力来源”

---

## 9. 推荐落地顺序（含单测）

1) `Map` + `TileFlags` + `TileAt`（含银行家舍入测试）
2) `BresenhamCache`（0.4 宽线，supercover，含对称性/边界测试）
3) `PathMap` + `PathMapCache`（含 mapVersion 失效测试，小地图 golden tests）
4) `SpatialGrid`（occupation + center，含覆盖与 bankers rounding 测试）
5) `RouteFollower`（移动五步 + CP，先用最小子集跑通）
6) `Block`（扫描/稳定点/forced move）
7) `AttackAbility`（tick 离散 + “多 1 tick”现象单测）
8) `Unbalance`（push/pull/hook + 摩擦 + protect）

