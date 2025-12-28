## 1. 术语与坐标约定

### 1.1 坐标与向量

* 场景为 2D 平面。
* 所有“距离/半径”均以**世界单位**计（通常可理解为格子大小为 1 的坐标系）。

### 1.2 中点（Center）

* **角色类单位 A**：干员/召唤物/关卡装置等（拥有阻挡半径）。
* **敌人 B**：被阻挡对象。
* 机制中使用的均是 **A.center** 与 **B.center** 的距离。

> 注意：文中提到的受击判定、物理碰撞箱等都与这里的阻挡判定无关。

---

## 2. 常量与参数

| 名称                     |                  值 | 含义                   |
| ---------------------- | -----------------: | -------------------- |
| `DEFAULT_BLOCK_RADIUS` | `0.7071`（≈ √2 / 2） | 干员/召唤物默认阻挡半径         |
| `SCAN_INTERVAL_FRAMES` |              `3` 帧 | 角色类单位扫描阻挡目标的频率       |
| `EPS`                  |          `0.00001` | 判定“中心重合”的阈值          |
| `MIN_SEPARATION`       |              `0.5` | 单目标阻挡偏移：若距离不足则延长到该长度 |
| `ENEMY_CORE_RADIUS`    |              `0.1` | 多目标偏移：冲突检测与近距离规则阈值   |
| `SOFT_PUSH_RANGE`      |              `0.4` | 多目标偏移：软推力生效距离上限      |
| `CORRECTION_FINAL_LEN` |              `0.2` | 多目标偏移：修正向量最终强制长度     |
| `FORCED_MOVE_TIME`     |             `0.2s` | 敌人被阻挡后强制移动到稳定点的时间    |

> 装置类单位可能有不同的 `blockRadius`，实现时作为数据驱动字段即可。

---

## 3. 阻挡判定规则（Eligibility）

### 3.1 可被阻挡条件

当满足：

* `distance(A.center, B.center) <= A.blockRadius`

则敌人 B **可以被判定为可阻挡**（eligible）。

### 3.2 扫描与目标选择

* 角色类单位 A **每 3 帧**扫描一次：

  * 扫描范围：以 A.center 为圆心、半径 `A.blockRadius`
  * 扫描对象：范围内**尚未被阻挡**的敌人
* 若同时存在多个可阻挡敌人：

  * **优先阻挡距离 A.center 最近**的敌人

> 现实游戏还有“阻挡数/阻挡上限”等约束；你提供的文本没展开，但工程上通常需要：`blockCapacity` 与 `blockedList`。

---

## 4. 阻挡偏移（Block Offset）规格

阻挡偏移用于解释“敌人被阻挡后位置会发生侧向/挤开变化”的现象。

### 4.1 偏移生效前提（必要条件）

若敌人 B 与阻挡者 A 的中心**重合**（或几乎重合）：

* `distance(A.center, B.center) <= EPS`
* **不产生阻挡偏移**
* 敌人位置不被该逻辑推开（例如文中 5-3 开局术士那类情况）

### 4.2 单目标（A 只阻挡一个敌人）

计算向量 `AB = B.center - A.center`，长度 `|AB|`：

* 若 `|AB| < MIN_SEPARATION (0.5)`：

  * 将其沿方向延长到长度 0.5：`AC = normalize(AB) * 0.5`
* 否则：

  * `AC = AB`

最终目标点：

* `C = A.center + AC`
* 若无多目标冲突修正（见 4.3），则稳定阻挡位置可直接取 `C`

### 4.3 多目标（A 同时阻挡多个敌人）

在完成 **4.2 的“延长得到 AC”** 后，先得到候选点 `C`。

#### 4.3.1 冲突触发条件

若在 `C` 点**半径 0.1**内存在其他已被 A 阻挡的敌人（使用其“稳定阻挡位置”）：

* 存在 `Dn` 使得：`distance(Dn.stableBlockPos, C) <= ENEMY_CORE_RADIUS (0.1)`
* 则触发“修正向量”计算

否则：

* 不触发修正，直接用 `C` 作为目标点。

#### 4.3.2 修正向量累加规则

对除当前敌人 B 以外的所有已被 A 阻挡的敌人，记为 `Dn`（使用 `Dn.stableBlockPos`）：

令：

* `DnC = C - Dn.stableBlockPos`
* `d = |DnC|`

累加修正向量 `corr`：

1. 若 `d < 0.1`

* 取 `ADn = Dn.stableBlockPos - A.center`
* 将 `ADn` 绕 A 点**顺时针旋转 90°**得到 `rot`
* `corr += normalize(rot)`（长度归一为 1 后累加）

2. 若 `0.1 <= d < 0.4`

* `corr += (0.4 - d) * 50 * normalize(DnC)`

3. 若 `d >= 0.4`

* 不处理

#### 4.3.3 修正向量定长与合成

* 遍历完得到 `corr` 后，将其长度**强制改为 0.2**：

  * `corr = setLength(corr, 0.2)`（若 corr 近似 0，则置 0）
* `AE = AC + corr`
* 再将 `AE` 的长度设置为 `|AC|`：

  * `AF = setLength(AE, |AC|)`
* 最终稳定点：

  * `F = A.center + AF`

#### 4.3.4 强制移动与稳定阻挡位置

* 敌人 B 会在 `0.2s` 内**强制移动**到稳定点 `F`
* `F` 会被记录为 `B.stableBlockPos`
* 后续 A 再阻挡其他敌人、进行偏移计算时：

  * 获取已被 A 阻挡敌人的位置应使用 **stableBlockPos**（文中说“获取为 F”）

---

## 5. 数据结构建议（工程实现）

### 5.1 角色类单位（Blocker）

* `center : Vector2`
* `blockRadius : float`
* `scanIntervalFrames : int = 3`
* `blockCapacity : int`（可选：如果你要还原“阻挡数”）
* `blockedEnemies : List<Enemy>`（当前被其阻挡的敌人）

### 5.2 敌人（Enemy）

* `center : Vector2`（当前坐标）
* `blockedBy : Blocker?`
* `stableBlockPos : Vector2?`（稳定阻挡位置）
* `forcedMove : ForcedMoveState?`

### 5.3 强制移动（ForcedMoveState）

* `active : bool`
* `startPos : Vector2`
* `targetPos : Vector2`
* `duration : float = 0.2`
* `elapsed : float`

---

# 6. 伪代码

> 说明：伪代码用接近 TypeScript/Python 的写法表达，重点是逻辑与边界条件。

## 6.1 向量工具

```pseudo
struct Vec2 { x: float, y: float }

function length(v: Vec2) -> float
function distance(a: Vec2, b: Vec2) -> float = length(b - a)

function normalize(v: Vec2) -> Vec2
    len = length(v)
    if len <= EPS: return Vec2(0, 0)
    return v / len

function setLength(v: Vec2, newLen: float) -> Vec2
    len = length(v)
    if len <= EPS: return Vec2(0, 0)
    return v * (newLen / len)

# 顺时针旋转90度：若坐标系 y 向上，则 (x, y) -> (y, -x)
function rotate90CW(v: Vec2) -> Vec2
    return Vec2(v.y, -v.x)
```

---

## 6.2 阻挡可行性判定

```pseudo
function isEligibleToBlock(blockerA, enemyB) -> bool
    return distance(blockerA.center, enemyB.center) <= blockerA.blockRadius
```

---

## 6.3 每 3 帧扫描并选择目标

```pseudo
function updateBlocker(blockerA, allEnemies, frameIndex)
    if frameIndex % SCAN_INTERVAL_FRAMES != 0:
        return

    # 如果实现了阻挡上限
    if blockerA.blockCapacity is defined and
       size(blockerA.blockedEnemies) >= blockerA.blockCapacity:
        return

    candidates = []
    for enemy in allEnemies:
        if enemy.blockedBy != null: 
            continue  # “未被阻挡”的敌人
        if isEligibleToBlock(blockerA, enemy):
            candidates.append(enemy)

    sort candidates by distance(blockerA.center, enemy.center) ascending

    for enemy in candidates:
        if blockerA.blockCapacity is defined and
           size(blockerA.blockedEnemies) >= blockerA.blockCapacity:
            break
        engageBlock(blockerA, enemy)
```

---

## 6.4 建立阻挡关系 + 计算稳定阻挡位置

```pseudo
function engageBlock(blockerA, enemyB)
    enemyB.blockedBy = blockerA

    # existingBlocked：当前已被 A 阻挡的其他敌人（不含 B）
    existingBlocked = blockerA.blockedEnemies

    targetPos = computeStableBlockPos(blockerA, enemyB, existingBlocked)

    enemyB.stableBlockPos = targetPos
    startForcedMove(enemyB, targetPos, FORCED_MOVE_TIME)

    blockerA.blockedEnemies.append(enemyB)
```

---

## 6.5 计算稳定阻挡位置（核心：偏移规则）

```pseudo
function computeStableBlockPos(A, B, existingBlockedEnemies) -> Vec2
    Apos = A.center
    Bpos = B.center

    # 前提：中心几乎重合 -> 不产生偏移
    if distance(Apos, Bpos) <= EPS:
        return Bpos

    AB = Bpos - Apos
    abLen = length(AB)

    # 先做“长度延长”得到 AC
    AC = AB
    if abLen < MIN_SEPARATION:
        AC = setLength(AB, MIN_SEPARATION)

    C = Apos + AC

    # 若 C 点 0.1 半径内没有其他已阻挡敌人 -> 不触发修正
    conflict = false
    for D in existingBlockedEnemies:
        Dpos = D.stableBlockPos  # 必须用稳定阻挡位置
        if Dpos is null: continue
        if distance(Dpos, C) <= ENEMY_CORE_RADIUS:
            conflict = true
            break

    if conflict == false:
        return C

    # 触发修正：遍历所有已阻挡敌人累加 corr
    corr = Vec2(0, 0)

    for D in existingBlockedEnemies:
        Dpos = D.stableBlockPos
        if Dpos is null: continue

        DnC = C - Dpos
        d = length(DnC)

        if d < ENEMY_CORE_RADIUS:  # d < 0.1
            ADn = Dpos - Apos
            rot = rotate90CW(ADn)
            corr += normalize(rot)  # 长度 1 的方向向量
        else if d < SOFT_PUSH_RANGE:  # 0.1 <= d < 0.4
            corr += (SOFT_PUSH_RANGE - d) * 50.0 * normalize(DnC)
        else:
            # d >= 0.4 -> do nothing
            pass

    # corr 强制定长为 0.2
    if length(corr) > EPS:
        corr = setLength(corr, CORRECTION_FINAL_LEN)
    else:
        corr = Vec2(0, 0)

    AE = AC + corr

    # 将 AE 长度设为 |AC| 得到 AF
    if length(AE) <= EPS:
        # 极端情况下 AE 抵消为 0，回退到 AC
        AE = AC

    AF = setLength(AE, length(AC))
    F = Apos + AF
    return F
```

---

## 6.6 敌人强制移动到稳定点（0.2s）

```pseudo
function startForcedMove(enemy, targetPos, duration)
    enemy.forcedMove.active = true
    enemy.forcedMove.startPos = enemy.center
    enemy.forcedMove.targetPos = targetPos
    enemy.forcedMove.duration = duration
    enemy.forcedMove.elapsed = 0

function updateEnemy(enemy, dt)
    if enemy.forcedMove.active:
        enemy.forcedMove.elapsed += dt
        t = clamp(enemy.forcedMove.elapsed / enemy.forcedMove.duration, 0, 1)

        enemy.center = lerp(enemy.forcedMove.startPos, enemy.forcedMove.targetPos, t)

        if t >= 1:
            enemy.forcedMove.active = false
```

---

## 7. 实现备注与常见坑

1. **“稳定阻挡位置”要立即记录**
   文档描述的效果是：后续计算 Dn 时读取的是 F（稳定点），所以建议在 `engageBlock` 时就把 `stableBlockPos = F` 写入，而不是等强制移动结束。

2. **冲突触发是“先判断 C 周围 0.1 内是否有人”**
   没有这个触发条件就不做复杂修正（否则会出现无意义的横向挤压）。

3. **旋转方向与坐标系有关**
   文中写“顺时针 90 度”。如果你的坐标系是 y 向下（很多 UI/2D 引擎如此），`rotate90CW` 的实现要相应调整（常见为 `(x,y)->(-y,x)` 或 `(y,-x)` 取决于坐标定义）。

4. **阈值要做浮点安全处理**
   比如 `EPS`、`<= 0.1`、`< 0.4` 等边界，建议统一用 `<=`/`<` 严格按规格走，并避免 `normalize(0)`。
