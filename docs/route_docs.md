## 1. 系统总览

### 1.1 运行分两段

1. **加载阶段（预处理）**

* 地图 + 路线加载完成后，为：

  * **终点**
  * **每个移动类检查点（MOVE / PATROL_MOVE）的目标地块**
    分别创建一张 **寻路地图 PathMap**。
* 多条路线/多个敌人可共享同一张 PathMap（缓存）。

2. **战斗运行阶段（每帧）**

* 敌人处于 **MOVE 状态**时，每帧进行移动计算。
* 无论敌人处于什么状态，每帧都会：

  * 更新当前检查点
  * 判断检查点是否完成并可能切换到下一个
  * 判断是否到达终点
    （与移动同帧时，通常在移动后判定完成）

---

## 2. 核心数据结构与概念

### 2.1 坐标与地块

* **世界坐标 (x,y)**：以地图左下角地块中心为 (0,0)，每格边长 1。
* **地块坐标 TileCoord**：地块中心坐标，整数点（x,y 都为整数）。
* **世界坐标 → 地块**：对 x,y 按 **四舍六入五成双（银行家舍入）**取整得到所在格。

> 注意：这是“实现细节关键点”，会影响进格/判定边界行为。

### 2.2 三种位置（非常关键）

* **实体坐标 entityPos**：敌人“身体中心”的真实位置；用于很多效果判定。
* **光标坐标 cursorPos**：用于大量移动逻辑与检查点/终点判定。
  通常 `cursorPos == entityPos`，但某些敌人生成时存在固定偏移：

  * 生成后偏移不再变化
  * 光标位置可能比实体坐标更“对齐格子”
* **足坐标 footPos**：用于避障力计算，通常是 `entityPos + (0, -0.2)`（部分敌人可能不同）。

### 2.3 地块类型（对“通行/避障/寻路”含义不同）

* **不可通行（Impassable）**：对某移动模式（地面/飞行）完全不可通过；进入/尝试进入时会触发修正等逻辑。
* **障碍物（Obstacle）**：寻路上不是“完全不可过”，而是“代价极大”（例如箱子 +1000，地穴 +1000000）。
  *飞行单位不存在任何障碍物的概念。*

### 2.4 路线 Route

包含：

* 出生点（含偏移）
* 终点（含偏移，但终点“目标地块”用地块中心）
* 检查点列表 CPs：每个 CP 有类型与参数
* 移动模式 MoveMode：Ground / Air
* 若干配置开关（后述）

### 2.5 PathMap（寻路地图）

对某个 **目标地块 targetTile**（终点或移动类检查点的目标地块）与某个 **移动模式**，生成一张表：

* `distToTarget[tile]`：到目标地块的“最短距离”（本质基于四方向 SPFA 得到的曼哈顿意义距离；障碍物有巨大惩罚）
* `nextNode[tile]`：从 tile 朝目标走的“最近路径上的下一个地块”

  * 目标地块的 nextNode 是其自身
  * 不可通行地块的 nextNode 也是其自身
* `distToEnd[tile]`：从 tile 到“终点”的最短距离（会在 route 级别按检查点倒序计算累加）

---

## 3. 加载阶段：寻路地图生成

### 3.1 缓存键

原文描述缓存命中条件是：

* 移动模式一致（地面/飞行）
* 目标地块一致

> 若你的实现里 `allowDiagonalMove` 可能因路线不同而不同，理论上也应纳入缓存键；但原文描述没提，就先按“mode+targetTile”缓存。

### 3.2 SPFA 反向扩展（从目标向外）

* 从 `targetTile` 开始入队
* 出队一个中心格 `cur`
* 按固定顺序扫描邻居：**上 → 右 → 下 → 左**
* 若“邻居 → cur”在该移动模式下可通行，则尝试松弛：

  * 普通地块：代价 +1
  * 障碍物：代价 + 1 + penalty（例如箱子额外 +1000，地穴额外 +1000000）
* 若邻居更新成功：

  * 更新 `distToTarget[neighbor]`
  * 设置 `nextNode[neighbor] = cur`
  * 邻居入队（队列去重）

### 3.3 平整化（允许斜向表现）

SPFA 扫四方向，只会得到“直角路径”。若 `allowDiagonalMove` 开启，需要对 `nextNode` 做“向前链接”，让单位在视觉上走斜线：

* 目标：尽可能把 `nextNode[tile]` 改成“更远的后继”，减少拐角
* 判定 “tile → candidate” 之间是否穿过不可通行/障碍物：

  * 使用魔改 Bresenham 直线栅格穿越检测（两点修改规则见原文）

> 重要：平整化不会改变 `distToTarget`，也不会改变整体“曼哈顿意义的方向结构”；只是把 nextNode 链接拉长，让移动更像斜线。

### 3.4 distToEnd（到终点距离）倒序累加

对一条 route：先有终点 PathMap，然后按检查点倒序，把“到终点的距离”累加到每张 PathMap 上：

* 对某张 map(i)，其目标地块为 `Ti`
* 下一张 map(i+1) 已有 `distToEnd`
* 则 map(i) 上任意 tile：

  * `distToEnd_i[tile] = distToTarget_i[tile] + distToEnd_{i+1}[Ti]`
* 若中间存在 `APPEAR_AT_POS`（传送出现点），则累加起点改为“出现位置所在地块”，传送门过程距离不计入。

---

## 4. 运行阶段：每帧移动与判定

### 4.1 每帧流程（推荐实现顺序）

1)（可选）先做一次“基于当前位置即可完成”的检查点推进（避免朝已完成 CP 继续走）
2) 若状态机为 `MOVE`：执行移动逻辑
3) 更新当前检查点（计时类等）
4) 判断当前检查点是否完成；若完成则切换下一检查点（可能连续跳多个）
5) 判定是否到达终点

### 4.2 移动逻辑五步（对应原文）

当敌人处于 MOVE 状态时：

0. **越界处理**：若实体坐标在有效地图外，跳过其它步骤，直接以理论移速朝地图内移动
1. **确定目标**（targetPos）
   1.5 若本帧理论移速可使光标坐标直接到达目标：直接把自身设置到目标（极少发生，通常属性移速 > 3.0）
2. **确定给定方向**（givenDir，单位向量或 0）
   2.5 若被束缚：跳过避障/惯性计算，本帧位移=0
3. **计算避障力**（每 3 帧一次，缓存 3 帧）
4. **惯性/转向/速度上限**：由 givenDir + 避障力 + 惯性向量算出本帧速度与位移，并更新惯性向量
5. **不可通行修正**：若跨格进入不可通行地块，进行“反射式修正”，再更新实体坐标

### 4.3 目标与给定方向的四种模式

有三个互斥开关（优先级从高到低）：

* `visitEveryTileCenter`
* `visitEveryNodeCenter`
* `visitEveryNodeStably`

以及默认模式（都未启用）。

#### 默认模式（最常见）

* 若当前检查点是非移动类：`givenDir = (0,0)`
* 若当前是移动类检查点或终点：

  * 取当前 active PathMap（若当前 CP 非移动类，则用下一个移动类 CP/终点对应的 PathMap）
  * 令 `tile = TileAt(cursorPos)`
  * `node = nextNode[tile]`
  * 若 `node == targetTile`：改用 `targetPoint`（含偏移）
  * `givenDir = Normalize( (nodeCenterOrTargetPoint) - cursorPos )`
* 若 `cursorPos` 所在地块不可通行或“无路可达”：`givenDir = 0`

#### visitEveryNodeStably（当路径无任何检查点时自动启用）

* 目标通常是 `nextNode(TileAt(cursorPos))`
* 但若进入“上一地块的 nextNode 地块”后，尚未进入“当前地块中心半径 0.25”：

  * 目标仍是“当前地块中心”
* 方向始终为 `Normalize(target - cursorPos)`

#### visitEveryNodeCenter

* 目标通常是 `nextNode(TileAt(cursorPos))`
* 但若进入“上一地块的 nextNode 地块”后，尚未进入“当前地块中心半径 0.05”：

  * 目标仍是“当前地块中心”
* 到达 nextNode 中心半径 0.05：标记该地块“已走过”
* 若 `nextNode(TileAt(cursorPos))` 已被标记走过，则退回默认模式

#### visitEveryTileCenter

* 目标默认为“当前地块中心”
* 进入该中心半径 0.05：标记该地块“已走过”
* 若当前地块已走过，则退回默认模式

---

## 5. 检查点系统规格

### 5.1 检查点类型枚举（整理成程序形态）

* MOVE
* WAIT_FOR_SECONDS
* WAIT_FOR_PLAY_TIME
* WAIT_CURRENT_FRAGMENT_TIME
* WAIT_CURRENT_WAVE_TIME
* DISAPPEAR
* APPEAR_AT_POS
* ALERT
* PATROL_MOVE
* WAIT_BOSSRUSH_WAVE

### 5.2 通用规则

* 每帧仅对“当前检查点”做 update & 完成判定
  → 不能通过把敌人推到“下一个检查点位置”来跳过当前检查点
* `ignoreAllButMoveCp` 为 true 时，忽略除：

  * MOVE / PATROL_MOVE / DISAPPEAR / APPEAR_AT_POS
    之外的检查点（可视为直接跳过或不进入列表）
* PATROL_MOVE 的下一检查点：

  * 若其为最后一个且不为首个，则回到首个（循环）

### 5.3 移动类检查点完成条件要点

MOVE / PATROL_MOVE：

* `Distance(cursorPos, targetPoint) <= radius`（通常/最小 0.05）
* 或 `cursorTile` 无法通往 `targetTile/targetPoint`（例如 distToTarget=INF）
* 原文强调：敌人进入不可通行地块时，若当前为移动类检查点会直接判定完成
  → 本质上可通过“卡墙导致无路”触发跳检查点

### 5.4 终点判定

* `Distance(cursorPos, endPoint) <= 0.05` 即到达终点
* 若 `visitEveryCheckPoint == true`：必须完成全部检查点才算到达终点（否则进了终点范围也不算）

---

## 6. 伪代码（可直接当实现蓝图）

> 伪代码偏 C#/Unity 风格，但保持语言无关；`Vector2`、`ClampMagnitude`、`Normalize`、`Dot` 等为通用数学函数。

### 6.1 基础工具：坐标→地块（银行家舍入）

```csharp
int RoundHalfToEven(float v) {
    // 银行家舍入：x.5 时向偶数取整
    // 具体实现可用语言自带 round-to-even，或手写
}

TileCoord TileAt(Vector2 pos) {
    int tx = RoundHalfToEven(pos.x);
    int ty = RoundHalfToEven(pos.y);
    return new TileCoord(tx, ty);
}

Vector2 TileCenter(TileCoord t) => new Vector2(t.x, t.y);
```

---

### 6.2 PathMap 构建（SPFA）

```csharp
PathMap BuildPathMap(Grid grid, MoveMode mode, TileCoord targetTile) {
    PathMap pm = new PathMap(grid.width, grid.height);
    pm.targetTile = targetTile;

    for each tile in grid:
        pm.distToTarget[tile] = INF;
        pm.nextNode[tile] = tile; // 默认指向自己

    Queue<TileCoord> q;
    HashSet<TileCoord> inQueue;

    pm.distToTarget[targetTile] = 0;
    pm.nextNode[targetTile] = targetTile;
    q.Enqueue(targetTile);
    inQueue.Add(targetTile);

    TileCoord[] order = { Up, Right, Down, Left };

    while (q not empty) {
        TileCoord cur = q.Dequeue();
        inQueue.Remove(cur);

        foreach dir in order {
            TileCoord nb = cur + dir;
            if (!grid.InBounds(nb)) continue;

            // 注意：这里是“从 nb 走到 cur”是否可通行（反向扩展）
            if (!grid.CanTraverse(nb, cur, mode)) continue;

            // 代价：普通 +1，障碍物额外惩罚（飞行一般无障碍）
            int penalty = grid.ObstaclePenalty(nb, mode); 
            int cost = 1 + penalty;

            int newDist = pm.distToTarget[cur] + cost;
            if (newDist < pm.distToTarget[nb]) {
                pm.distToTarget[nb] = newDist;
                pm.nextNode[nb] = cur;

                if (!inQueue.Contains(nb)) {
                    q.Enqueue(nb);
                    inQueue.Add(nb);
                }
            }
        }
    }

    // 不可通行 tile 的 nextNode 视为自身（与原文一致）
    for each tile in grid:
        if (!grid.Passable(tile, mode)) {
            pm.nextNode[tile] = tile;
            // distToTarget 可保留 INF
        }

    return pm;
}
```

---

### 6.3 平整化：尽量把 nextNode 往前连（斜线表现）

```csharp
void SmoothNextNodes(Grid grid, MoveMode mode, PathMap pm, bool allowDiagonalMove) {
    if (!allowDiagonalMove) {
        // 原文提到即便不允许斜角也可能平整化（但基本没用）
        // 这里可直接不做，或做“连到下一个拐点”的版本
        return;
    }

    for each tile s in grid:
        if (!grid.Passable(s, mode)) continue;
        if (pm.distToTarget[s] == INF) continue;

        TileCoord cur = s;
        TileCoord best = pm.nextNode[cur];
        if (best == cur) continue;

        // 尝试不断向前跳
        while (true) {
            TileCoord next = pm.nextNode[best];
            if (next == best) break; // 到目标或断链
            if (next == cur) break;  // 防止环

            if (LineClear_ModifiedBresenham(grid, mode, cur, next)) {
                best = next;
            } else {
                break;
            }
        }

        pm.nextNode[cur] = best;
    }
}
```

#### 魔改 Bresenham（占位式，保留原文两条规则）

```csharp
bool LineClear_ModifiedBresenham(Grid grid, MoveMode mode, TileCoord a, TileCoord b) {
    // 返回：a 到 b 的连线经过的所有需要检查的格子里，不存在不可通行/障碍物
    // 原文规则：
    // (1) 若行差或列差为 1，则 2*x 长方形范围内所有地块都要判定
    // (2) Bresenham 中“短轴发生改变”时，额外判定斜角的 2 个地块

    List<TileCoord> tilesToCheck = ModifiedBresenhamTiles(a, b);

    foreach t in tilesToCheck {
        if (!grid.InBounds(t)) return false;

        // 平整化判定“不可通行 或 障碍物”
        if (!grid.Passable(t, mode)) return false;
        if (grid.IsObstacle(t, mode)) return false; // 飞行通常恒为 false
    }
    return true;
}
```

---

### 6.4 路线级 distToEnd 倒序计算

```csharp
void ComputeDistToEndForRoute(Route route) {
    // route.segmentMaps：按“移动类检查点 + 终点”形成的序列
    // 例如：CP0(MOVE), CP3(MOVE), End => [Map(CP0), Map(CP3), Map(End)]
    // 还需要知道每段的目标地块 Ti

    int n = route.segmentMaps.Count;

    // 最后一张：终点图
    PathMap endMap = route.segmentMaps[n-1];
    for each tile:
        endMap.distToEnd[tile] = endMap.distToTarget[tile];

    // 倒序累加
    for (int i = n-2; i >= 0; --i) {
        PathMap cur = route.segmentMaps[i];
        PathMap next = route.segmentMaps[i+1];

        TileCoord linkTile = cur.targetTile; 
        // 若 i 与 i+1 之间有 APPEAR_AT_POS，linkTile 应替换为“出现地块”
        if (route.HasAppearTeleportBetween(i, i+1)) {
            linkTile = route.GetAppearTileBetween(i, i+1);
        }

        int baseToEnd = next.distToEnd[linkTile];

        for each tile:
            if (cur.distToTarget[tile] == INF || baseToEnd == INF) {
                cur.distToEnd[tile] = INF;
            } else {
                cur.distToEnd[tile] = cur.distToTarget[tile] + baseToEnd;
            }
        }
    }
}
```

---

## 6.5 运行时：敌人每帧更新

### 6.5.1 敌人结构（关键字段）

```csharp
class Enemy {
    Route route;

    Vector2 entityPos;
    Vector2 cursorPos;
    Vector2 cursorOffset;  // cursorPos - entityPos (生成后固定)
    Vector2 footOffset;    // 通常 (0, -0.2)

    Vector2 inertiaVel;    // “惯性向量”
    Vector2 cachedAvoid;   // 每3帧更新一次
    int avoidFrameCounter; // 0..2

    float attrSpeed;       // 属性移速（下限0.1）
    float moveMultiplier;  // 通常0.5
    float theoreticalSpeed => max(floorOrClamp(attrSpeed), 0.1f) * moveMultiplier;

    float steeringFactor;
    float maxSteeringForce;

    bool isBound;          // 束缚
    State stateMachine;    // MOVE/ATTACK/BLOCKED/...

    int cpIndex;           // 当前检查点下标
    HashSet<TileCoord> passedTiles; // visitEveryNodeCenter / visitEveryTileCenter
    float waitedSeconds;   // WAIT_FOR_SECONDS用
}
```

### 6.5.2 主循环

```csharp
void UpdateEnemyPerFrame(Enemy e, float dt = 1f/30f) {
    // A) 预推进：若当前位置已满足“位置型完成条件”，避免朝已完成点移动
    AdvanceCheckpointsIfComplete(e);

    // B) 移动（仅 MOVE 状态）
    if (e.stateMachine == State.MOVE) {
        DoMove(e, dt);
    }

    // C) 更新当前检查点（计时类等）
    UpdateCurrentCheckpoint(e, dt);

    // D) 再推进（移动后可能进入完成半径）
    AdvanceCheckpointsIfComplete(e);

    // E) 终点判定
    CheckReachEnd(e);
}
```

---

## 6.6 移动：五步实现

```csharp
void DoMove(Enemy e, float dt) {
    // Step 0: 越界直接拉回
    if (!e.route.grid.InWorldBounds(e.entityPos)) {
        Vector2 inside = e.route.grid.ClampToBounds(e.entityPos);
        Vector2 dirIn = NormalizeOrZero(inside - e.entityPos);
        Vector2 disp = dirIn * e.theoreticalSpeed * dt;
        ApplyDisplacement(e, disp); // 同步 entity/cursor
        return;
    }

    // Step 1/2: 目标与给定方向
    (Vector2 targetPos, Vector2 givenDir) = DetermineTargetAndGivenDir(e);

    // Step 1.5: 一帧直达（极少）
    float maxMove = e.theoreticalSpeed * dt;
    if (Distance(e.cursorPos, targetPos) <= maxMove) {
        SetCursorPos(e, targetPos); // 同步实体位置 = cursorPos - offset
        return;
    }

    Vector2 disp;

    // Step 2.5: 束缚直接不动（且不更新惯性）
    if (e.isBound) {
        disp = Vector2.zero;
        ApplyDisplacement(e, disp);
        return;
    }

    // Step 3: 避障力（每3帧）
    if (e.avoidFrameCounter == 0) {
        e.cachedAvoid = ComputeAvoidanceForce(e, givenDir);
    }
    e.avoidFrameCounter = (e.avoidFrameCounter + 1) % 3;

    // Step 4: 惯性/转向
    disp = SteeringAndIntegrate(e, givenDir, e.cachedAvoid, dt);

    // Step 5: 修正 + 应用
    ApplyDisplacementWithCorrection(e, disp);
}
```

---

## 6.7 目标与给定方向（四种模式统一入口）

```csharp
(Vector2 targetPos, Vector2 givenDir) DetermineTargetAndGivenDir(Enemy e) {
    // 先定位“用于寻路的 active segment”
    // 若当前 CP 非移动类，则使用“下一个移动类 CP/终点”的 PathMap
    Segment seg = e.route.GetActiveSegmentFromCpIndex(e.cpIndex);
    PathMap pm = seg.pathMap;
    TileCoord targetTile = seg.targetTile;
    Vector2 targetPoint = seg.targetPoint; // 含偏移

    TileCoord curTile = TileAt(e.cursorPos);

    // 若当前光标格不可通行或无路：givenDir=0（目标仍可给个默认值）
    if (!e.route.grid.Passable(curTile, e.route.moveMode) || pm.distToTarget[curTile] == INF) {
        return (targetPoint, Vector2.zero);
    }

    // 三个互斥开关（优先级高->低）
    if (e.route.visitEveryTileCenter) {
        Vector2 tileCenter = TileCenter(curTile);
        if (!e.passedTiles.Contains(curTile)) {
            if (Distance(e.cursorPos, tileCenter) <= 0.05f) e.passedTiles.Add(curTile);
            Vector2 dir = NormalizeOrZero(tileCenter - e.cursorPos);
            return (tileCenter, dir);
        }
        // 已走过 => 回退默认
    }
    else if (e.route.visitEveryNodeCenter) {
        TileCoord node = pm.nextNode[curTile];
        Vector2 nodeCenter = TileCenter(node);

        // “进入上一地块的 nextNode 后，但未到当前地块中心0.05” 的稳定规则
        if (EnteredPrevNextNodeButNotNearCenter(e, curTile, radius: 0.05f)) {
            Vector2 center = TileCenter(curTile);
            return (center, NormalizeOrZero(center - e.cursorPos));
        }

        // 到达 nextNode 中心 0.05 标记走过
        if (Distance(e.cursorPos, nodeCenter) <= 0.05f) e.passedTiles.Add(node);

        // 若 nextNode 已走过 => 回退默认
        if (!e.passedTiles.Contains(node)) {
            return (nodeCenter, NormalizeOrZero(nodeCenter - e.cursorPos));
        }
    }
    else if (e.route.visitEveryNodeStably) {
        TileCoord node = pm.nextNode[curTile];
        Vector2 nodeCenter = TileCenter(node);

        if (EnteredPrevNextNodeButNotNearCenter(e, curTile, radius: 0.25f)) {
            Vector2 center = TileCenter(curTile);
            return (center, NormalizeOrZero(center - e.cursorPos));
        }
        return (nodeCenter, NormalizeOrZero(nodeCenter - e.cursorPos));
    }

    // 默认模式：
    TileCoord next = pm.nextNode[curTile];

    // 若 next == targetTile：进入目标地块后，方向改为指向“目标点（含偏移）”
    if (next.Equals(targetTile)) {
        Vector2 dir = NormalizeOrZero(targetPoint - e.cursorPos);
        return (targetPoint, dir);
    } else {
        Vector2 nextCenter = TileCenter(next);
        Vector2 dir = NormalizeOrZero(nextCenter - e.cursorPos);
        return (nextCenter, dir);
    }
}
```

> `EnteredPrevNextNodeButNotNearCenter` 属于“复现原文行为”的辅助判断：它依赖你记录“上一帧 tile/上一目标 node”等状态。原文描述的是一种稳定切向条件，用于控制“什么时候允许拐弯”。

---

## 6.8 避障力（每 3 帧一次）

```csharp
Vector2 ComputeAvoidanceForce(Enemy e, Vector2 givenDir) {
    Grid grid = e.route.grid;
    MoveMode mode = e.route.moveMode;

    TileCoord centerTile = TileAt(e.cursorPos);

    // 情况A：中心格不可通行
    if (!grid.Passable(centerTile, mode)) {
        TileCoord best = FindNearestPassableIn8Neighbors(grid, centerTile, mode);
        Vector2 a = TileCenter(best) - TileCenter(centerTile);
        return RemoveProjection(NormalizeOrZero(a), givenDir);
    }

    // 情况B：中心格可通行，扫描周围8格
    Vector2 sum = Vector2.zero;

    Vector2 footPos = e.entityPos + e.footOffset;
    float halfBodyWidth = e.route.halfBodyWidthDefaultOrPerEnemy; // 默认0.2
    // 定义“最近点”：footPos 水平线段长度0.4（左右各0.2）
    Vector2 leftP  = footPos + new Vector2(-halfBodyWidth, 0);
    Vector2 midP   = footPos;
    Vector2 rightP = footPos + new Vector2(+halfBodyWidth, 0);

    foreach neighborTile in grid.Neighbors8(centerTile) {
        if (!grid.InBounds(neighborTile)) continue;

        bool blocked = !grid.Passable(neighborTile, mode) || grid.IsObstacle(neighborTile, mode);
        if (!blocked) continue;

        Vector2 rel = TileCenter(neighborTile) - TileCenter(centerTile); 
        // rel.x, rel.y ∈ {-1,0,1}（相对格方向）

        // 选最近点：按列（x方向）决定 left/mid/right
        Vector2 nearestPoint =
            (rel.x < 0) ? leftP :
            (rel.x > 0) ? rightP :
                          midP;

        // v = 最近点相对中心格中心
        Vector2 v = nearestPoint - TileCenter(centerTile);

        // 取“朝 neighbor 方向的正向偏移”
        float ox = max(v.x * rel.x, 0);
        float oy = max(v.y * rel.y, 0);

        // 有效偏移量：分别 -0.25，再乘 abs(rel轴)
        float ex = (ox - 0.25f) * abs(rel.x);
        float ey = (oy - 0.25f) * abs(rel.y);

        Vector2 contrib = Vector2.zero;

        bool isSide = (abs(rel.x) + abs(rel.y) == 1);
        bool isDiag = (abs(rel.x) == 1 && abs(rel.y) == 1);

        if (isSide && (ex > 0 || ey > 0)) {
            // 贡献：取负有效偏移，再按相对方向分量乘回去
            contrib = new Vector2(-ex * rel.x, -ey * rel.y);
        }
        else if (isDiag && (ex > 0 && ey > 0)) {
            float avg = (ex + ey) * 0.5f;
            contrib = -avg * new Vector2(rel.x, rel.y);
        }

        sum += contrib;
    }

    Vector2 a2 = NormalizeOrZero(sum);
    // 去掉在 givenDir 上的投影
    return RemoveProjection(a2, givenDir);
}

Vector2 RemoveProjection(Vector2 v, Vector2 dirUnit) {
    if (dirUnit == Vector2.zero) return v;
    Vector2 proj = Dot(v, dirUnit) * dirUnit;
    return v - proj;
}
```

---

## 6.9 惯性/转向/速度上限

```csharp
Vector2 SteeringAndIntegrate(Enemy e, Vector2 givenDir, Vector2 avoidForce, float dt) {
    float theo = e.theoreticalSpeed;

    // 实际避障力 = 避障力 * max(|惯性|/理论移速, 0.5)
    float inertiaMag = Magnitude(e.inertiaVel);
    float scale = max(inertiaMag / max(theo, 1e-6f), 0.5f);
    Vector2 actualAvoid = avoidForce * scale;

    // 加速度 = ClampMagnitude( (givenDir*theo - inertia)*steeringFactor + actualAvoid, maxSteeringForce )
    Vector2 desiredVel = givenDir * theo;
    Vector2 accel = (desiredVel - e.inertiaVel) * e.steeringFactor + actualAvoid;
    accel = ClampMagnitude(accel, e.maxSteeringForce);

    // 移动速度 = ClampMagnitude( inertia + accel*dt, theo )
    Vector2 vel = e.inertiaVel + accel * dt;
    vel = ClampMagnitude(vel, theo);

    // 更新惯性（注意：仅在“非束缚 MOVE 状态”下更新）
    e.inertiaVel = vel;

    return vel * dt;
}
```

---

## 6.10 位移应用与不可通行修正（反射）

```csharp
void ApplyDisplacementWithCorrection(Enemy e, Vector2 disp) {
    Vector2 start = e.entityPos;
    Vector2 dest = start + disp;

    TileCoord startTile = TileAt(start);
    TileCoord destTile  = TileAt(dest);

    if (!startTile.Equals(destTile)) {
        if (!e.route.grid.Passable(destTile, e.route.moveMode)) {
            // 修正：位移向量在“朝不可通行地块方向”上的投影做反射
            Vector2 n = NormalizeOrZero(TileCenter(destTile) - start);
            Vector2 proj = Dot(disp, n) * n;
            Vector2 corrected = disp - 2f * proj;

            dest = start + corrected;
        }
    }

    e.entityPos = dest;
    e.cursorPos = e.entityPos + e.cursorOffset;
}

void SetCursorPos(Enemy e, Vector2 newCursorPos) {
    e.cursorPos = newCursorPos;
    e.entityPos = e.cursorPos - e.cursorOffset;
}
```

---

## 6.11 检查点：更新、完成判定、切换

```csharp
void UpdateCurrentCheckpoint(Enemy e, float dt) {
    CheckPoint cp = e.route.GetCurrentCp(e.cpIndex);
    if (cp == null) return;

    switch (cp.type) {
        case WAIT_FOR_SECONDS:
            e.waitedSeconds += dt;
            break;
        default:
            break;
    }
}

bool IsCheckpointComplete(Enemy e, CheckPoint cp) {
    switch (cp.type) {
        case MOVE:
        case PATROL_MOVE: {
            float r = max(cp.radius, 0.05f);
            if (Distance(e.cursorPos, cp.targetPoint) <= r) return true;

            // 无路 / 不可通行触发完成（原文强调卡墙跳点）
            PathMap pm = e.route.GetPathMapForThisMoveCpOrNext(e.cpIndex);
            TileCoord t = TileAt(e.cursorPos);
            if (!e.route.grid.Passable(t, e.route.moveMode)) return true;
            if (pm.distToTarget[t] == INF) return true;

            return false;
        }
        case WAIT_FOR_SECONDS:
            return e.waitedSeconds >= cp.waitSeconds;
        case WAIT_FOR_PLAY_TIME:
            return e.route.gameTime >= cp.playTime;
        case WAIT_CURRENT_FRAGMENT_TIME:
            return e.route.fragmentTime >= cp.fragmentTime;
        case WAIT_CURRENT_WAVE_TIME:
            return e.route.waveTime >= cp.waveTime;
        case DISAPPEAR:
        case APPEAR_AT_POS:
        case ALERT:
            return true; // 默认完成
        case WAIT_BOSSRUSH_WAVE:
            return e.route.regionIndex >= (cp.startRegion + cp.waitRegions);
        default:
            return false;
    }
}

void OnEnterCheckpoint(Enemy e, CheckPoint cp) {
    switch (cp.type) {
        case WAIT_FOR_SECONDS:
            e.waitedSeconds = 0;
            break;
        case DISAPPEAR:
            e.stateMachine = State.DISAPPEARED;
            break;
        case APPEAR_AT_POS:
            e.stateMachine = State.MOVE; // 解除消失
            // 出现在指定位置（注意同步 cursor/entity）
            SetCursorPos(e, cp.appearCursorPosOrDerived);
            break;
        case ALERT:
            SpawnAlertFx(cp);
            break;
        case WAIT_BOSSRUSH_WAVE:
            cp.startRegion = e.route.regionIndex;
            break;
    }
}

void AdvanceCheckpointsIfComplete(Enemy e) {
    // ignoreAllButMoveCp: 跳过指定集合外的CP
    while (true) {
        CheckPoint cp = e.route.GetCurrentCp(e.cpIndex);
        if (cp == null) break;

        if (e.route.ignoreAllButMoveCp && !IsMoveOrTeleportCp(cp.type)) {
            e.cpIndex++;
            continue;
        }

        if (!IsCheckpointComplete(e, cp)) break;

        // 切换到下一个
        e.cpIndex = GetNextCpIndexWithPatrolRule(e.route, e.cpIndex);
        CheckPoint nextCp = e.route.GetCurrentCp(e.cpIndex);
        if (nextCp != null) OnEnterCheckpoint(e, nextCp);

        // 循环继续：允许一帧跳多个
    }
}

void CheckReachEnd(Enemy e) {
    if (e.route.visitEveryCheckPoint && !e.route.AllCpsCompleted(e.cpIndex)) return;

    float r = 0.05f;
    if (Distance(e.cursorPos, e.route.endPoint) <= r) {
        e.route.OnEnemyReachEnd(e);
    }
}
```

---

## 7. 你可以直接怎么用这份“程序化文档”

如果你要把它落到工程里，我建议最小切分为 4 个模块：

1. `PathfindingPrecompute`

* `BuildPathMap_SPFA()`
* `SmoothNextNodes()`
* `ComputeDistToEndForRoute()`
* 全局 `PathMapCache`

2. `RouteRuntime`

* route 的 cp 管理、segment 选择、配置开关处理

3. `EnemyMovement`

* `DetermineTargetAndGivenDir()`
* `ComputeAvoidanceForce()`（每 3 帧缓存）
* `SteeringAndIntegrate()`
* `ApplyDisplacementWithCorrection()`

4. `EnemyUpdateLoop`

* 每帧顺序编排（移动前/移动后检查点推进、终点判定）