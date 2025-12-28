## 1. 系统概览

### 1.1 分层结构

1. **底层（Unity 2D 物理）**

* 敌方单位使用 `Rigidbody2D (Dynamic)`
* **质量 mass 统一为 1kg**（关键点：同初始状态 + 同施力 → 运动一致）
* 推拉机制的差异主要来自：

  * 施力模式：`Impulse` vs `Force`
  * 力的大小、方向、持续时间、状态机约束

2. **中层（状态机 + 摩擦力）**

* 敌人存在多个状态，其中推拉核心是进入 **`unbalance`** 状态
* 只有在 `unbalance` 状态下，敌人“表现为刚体运动”，每帧更新速度并检查退出条件
* 存在“保护时间（unbalance 保护）”避免立即退出

3. **表层（游戏概念与推/拉规则）**

* “重量等级”“力量等级”等是游戏抽象，不等同于真实物理质量/力
* 推：瞬时 `Impulse`（直接改动量/速度）
* 拉：持续 `Force`（每帧施加，随距离比例衰减）

---

## 2. 关键概念与数据结构

### 2.1 名词表

* **力（Force）**：Unity 里给 `Rigidbody2D` 施加的力/冲量向量（大小+方向）
* **质量（mass）**：刚体质量，方舟里敌人统一 `1kg`
* **重量等级（weightLevel）**：敌人的游戏属性，下限 `0`
* **力量等级（strengthLevel）**：来源的推/拉力度等级（微小、小、中、较大、大）

力量等级映射（来自描述）：

| 描述      | strengthLevel |
| ------- | ------------: |
| 微小力（稍微） |            -1 |
| 小力      |             0 |
| 中力      |             1 |
| 较大力     |             2 |
| 大力      |             3 |

* **有效力量等级（effectiveLevel）**：
  `effectiveLevel = clamp(sourceStrengthLevel - targetWeightLevel, -3, 3)`

> 注：当计算结果超过 [-3,3] 会被截断（clamp）到边界。

---

### 2.2 常量（按文档默认值）

```text
RIGIDBODY_MASS = 1.0

GRAVITY_G = 9.81
FRICTION_COEFF = 0.5
FRICTION_FORCE = GRAVITY_G * RIGIDBODY_MASS * FRICTION_COEFF
              ≈ 4.905  (文中约 4.9)

UNBALANCE_PROTECT_DURATION = 0.1  seconds

PUSH_DIRECTIONAL_TO_RELATIVE_ANGLE_THRESHOLD = 45 degrees
PUSH_DIRECTIONAL_TO_RELATIVE_DISTANCE_THRESHOLD = 0.25 units

PUSH_RELATIVE_PENALTY = 2  (directional 转 relative 时 effectiveLevel 额外 -2)

MOVE_MULTIPLIER_DEFAULT = 0.5
PUSH_SCALE = 0.02 * MOVE_MULTIPLIER_DEFAULT  = 0.01
```

---

### 2.3 数据表（gamedata）

#### pushForces（用于推）

| effectiveLevel | pushForces值 | 力大小 = pushForces * 0.02 * MOVE_MULTIPLIER(0.5) |
| -------------: | ----------: | ---------------------------------------------: |
|             -3 |           0 |                                           0（无） |
|             -2 |         100 |                                            1.0 |
|             -1 |         200 |                                            2.0 |
|              0 |         400 |                                            4.0 |
|              1 |         450 |                                            4.5 |
|              2 |         530 |                                            5.3 |
|              3 |         580 |                                            5.8 |

#### pullForces（用于拉）

| effectiveLevel | pullForces值 | 初始力大小 = pullForces * 1 |
| -------------: | ----------: | ---------------------: |
|             -3 |           0 |                   0（无） |
|             -2 |           2 |                      2 |
|             -1 |          10 |                     10 |
|              0 |          40 |                     40 |
|              1 |          42 |                     42 |
|              2 |          44 |                     44 |
|              3 |          46 |                     46 |

---

## 3. 状态机：Unbalance

### 3.1 进入 unbalance 的触发

* 推（Push）成功时：

  * 力不为 0
  * 目标无 `UNBALANCE_IMMUNE`
  * 目标不是飞行单位
    → 切换至 `unbalance`，并施加一次 `Impulse`
    → 设置 `unbalanceProtectUntil = now + 0.1s`

* 拉（Pull / Hook）命中时：

  * 若力为 0：立即停止钩子（不进入 unbalance）
  * 否则若目标无免疫且非飞行：进入 `unbalance`，设置保护时间

---

### 3.2 unbalance 每帧更新逻辑（摩擦力与退出）

* 每帧根据“摩擦力”计算新速度并重设刚体速度
* 记录新速度
* 若满足以下任一条件则退出 `unbalance`：

  1. `(speed <= 0.1) AND (已超过保护时间) AND (无有效拉力来源)`
  2. `拥有 UNBALANCE_IMMUNE 效果`

> “无有效拉力来源”可以实现为：当前没有任何仍在链接/生效的 hook（或 pull source）绑定在该目标上。

---

## 4. 推（Push）机制规格

### 4.1 推的物理模式

* 使用 `Impulse`：瞬时改变动量
* 在 `mass = 1` 下可近似：**速度增量 Δv = impulseVector**（因为冲量=Δp=mΔv）

### 4.2 推的方向模式

* **relative**：方向 = 从来源指向目标
  `dir = normalize(targetPos - sourcePos)`

* **directional**：方向 = 来源朝向
  `dir = normalize(sourceFacingDir)`

#### directional → relative 的转换条件

若满足任一条件，则将 directional 转为 relative：

1. `angle( (targetPos - sourcePos), sourceFacingDir ) > 45°`
2. `distance(sourcePos, targetPos) < 0.25`

并且当发生转换时：

* **effectiveLevel 额外减少 2**（默认值）

---

### 4.3 推的力度计算

1. `effectiveLevel = clamp(sourceStrengthLevel - targetWeightLevel, -3, 3)`
2. 若发生 directional→relative 转换：
   `effectiveLevel = clamp(effectiveLevel - 2, -3, 3)`
3. 从 `pushForces` 查表得到 `pushVal`
4. `pushMagnitude = pushVal * 0.02 * MOVE_MULTIPLIER`（默认 `*0.01`）

---

### 4.4 推的执行流程

若满足：

* `pushMagnitude != 0`
* 目标不持有 `UNBALANCE_IMMUNE`
* 目标不为飞行单位

则：

1. 进入 `unbalance`
2. `Rigidbody2D.AddForce(dir * pushMagnitude, Impulse)`
3. 设置 `unbalanceProtectUntil = now + 0.1s`

---

## 5. 拉（Pull / Hook）机制规格

### 5.1 拉的物理模式

* 使用 `Force`：持续施力，逐渐改变动量

### 5.2 拉的方向

* 拉力方向固定：**从目标指向来源**
  `dir = normalize(sourcePos - targetPos)`

### 5.3 拉的力度（含距离衰减）

1. `effectiveLevel = clamp(sourceStrengthLevel - targetWeightLevel, -3, 3)`
2. 查表：`basePull = pullForces[effectiveLevel]`
3. 钩子命中时记录：
   `initialDistance = distance(sourcePos, targetPos)`
4. 某一时刻距离：`currDistance = distance(sourcePos, targetPos)`
5. `ratio = clamp(currDistance / initialDistance, 0, 1)`

默认 `easeType = 6 (easeInQuart)`：

* `ease(ratio) = ratio^4`

因此某一瞬间的力大小：

* `pullMagnitude(t) = (ratio^4) * basePull`

> 由于 ratio 最大为 1，随着距离变小 ratio 迅速变小，`ratio^4` 衰减非常快。

---

### 5.4 拉的执行流程（钩子）

#### 钩子命中（OnHit）

* 若 `basePull == 0`：立刻停止钩子
* 否则：

  * 若目标无免疫且非飞行：目标进入 `unbalance`，并设置保护时间 `+0.1s`
  * 记录 `initialDistance`

#### 钩子持续时间调整

* 钩子是 projectile，有最大持续时间 `maxDuration`
* 命中后：

  * 若 `effectiveLevel >= -1`：`maxDuration = linkDuration`（默认 1s）
  * 否则：`maxDuration = 0.5s`

#### 钩子链接期间（Update）

* 每帧对目标施加 `Force`：
  `AddForce(dir * pullMagnitude(t), Force)`
* 同时检查目标是否位于来源“阻挡范围内”

  * 若“位于范围内”且“只有单个拉力来源”：立刻清空目标速度（置 0）

---

## 6. 伪代码

> 风格说明：伪代码偏“Unity/C#”但不依赖具体 API；你可以很容易翻译到 C#/Lua/TS 等。

### 6.1 基础工具与数据

```pseudo
enum ForceMode2D { Force, Impulse }

const MASS = 1.0
const G = 9.81
const FRICTION_COEFF = 0.5
const FRICTION_FORCE = G * MASS * FRICTION_COEFF      // ≈ 4.905
const UNBALANCE_PROTECT = 0.1

const ANGLE_TH = 45.0
const DIST_TH  = 0.25
const DIR_TO_REL_PENALTY = 2

const MOVE_MULTIPLIER = 0.5
const PUSH_SCALE = 0.02 * MOVE_MULTIPLIER             // 0.01

// 查表（gamedata）
pushForces = map {
  -3: 0,
  -2: 100,
  -1: 200,
   0: 400,
   1: 450,
   2: 530,
   3: 580
}

pullForces = map {
  -3: 0,
  -2: 2,
  -1: 10,
   0: 40,
   1: 42,
   2: 44,
   3: 46
}

function clamp(x, lo, hi):
  if x < lo: return lo
  if x > hi: return hi
  return x

function effectiveLevel(sourceStrengthLevel, targetWeightLevel):
  return clamp(sourceStrengthLevel - targetWeightLevel, -3, 3)

function normalize(v):
  if length(v) <= 0: return (0,0)
  return v / length(v)

function angleDeg(a, b):
  // 返回向量夹角（0~180），按常规点积计算
  return arccos( dot(normalize(a), normalize(b)) ) * RAD2DEG
```

---

### 6.2 敌人对象（核心状态字段）

```pseudo
class Enemy:
  Rigidbody2D rb

  bool isFlying
  bool hasUnbalanceImmune

  bool inUnbalance
  float unbalanceProtectUntilTime

  // 用于判断“无有效拉力来源”
  set<PullLink> activePullLinks

  // 可选：记录速度（如果系统需要）
  Vector2 recordedVelocity
```

---

### 6.3 Unbalance 状态：进入/退出与每帧更新

```pseudo
function enterUnbalance(enemy, nowTime):
  enemy.inUnbalance = true
  enemy.unbalanceProtectUntilTime = max(enemy.unbalanceProtectUntilTime, nowTime + UNBALANCE_PROTECT)

function exitUnbalance(enemy):
  enemy.inUnbalance = false
  // 退出时是否清速度由实际实现决定；文中未强制
```

#### 摩擦力导致的减速（实现建议）

文档只说“每帧根据摩擦力计算新速度并重设刚体速度”，没给出精确公式；一个最贴近直觉且可实现的版本是“沿速度反方向匀减速”：

```pseudo
function applyFriction(vel, dt):
  speed = length(vel)
  if speed <= 0: return vel

  // friction 是力；mass=1，则减速度 a = F/m = FRICTION_FORCE
  decel = FRICTION_FORCE * dt
  newSpeed = max(0, speed - decel)

  return normalize(vel) * newSpeed
```

#### 每帧更新

```pseudo
function updateUnbalance(enemy, dt, nowTime):
  if not enemy.inUnbalance:
    return

  // 若中途获得免疫效果：立即退出
  if enemy.hasUnbalanceImmune:
    exitUnbalance(enemy)
    return

  // 计算并重设速度（摩擦）
  vOld = enemy.rb.velocity
  vNew = applyFriction(vOld, dt)
  enemy.rb.velocity = vNew
  enemy.recordedVelocity = vNew

  // 退出判定
  speed = length(vNew)
  noValidPullSource = (enemy.activePullLinks.size == 0)
  protectExpired = (nowTime >= enemy.unbalanceProtectUntilTime)

  if (speed <= 0.1) and protectExpired and noValidPullSource:
    exitUnbalance(enemy)
```

---

### 6.4 推（Push）实现

#### 推方向选择与 directional→relative 转换

```pseudo
enum PushDirectionMode { Relative, Directional }

function computePushDirection(sourcePos, targetPos, sourceFacingDir, mode):
  vecToTarget = targetPos - sourcePos
  dist = length(vecToTarget)

  if mode == Relative:
    return normalize(vecToTarget), false   // false = 未发生转换

  // mode == Directional
  // 判断是否需要转 relative
  a = angleDeg(vecToTarget, sourceFacingDir)
  if (a > ANGLE_TH) or (dist < DIST_TH):
    return normalize(vecToTarget), true    // true = 发生转换
  else:
    return normalize(sourceFacingDir), false
```

#### 推力度计算与执行

```pseudo
function push(sourcePos, targetPos, sourceFacingDir,
              sourceStrengthLevel, targetWeightLevel,
              directionMode,
              enemy, nowTime):

  if enemy.isFlying: return
  if enemy.hasUnbalanceImmune: return

  lvl = effectiveLevel(sourceStrengthLevel, targetWeightLevel)

  dir, converted = computePushDirection(sourcePos, targetPos, sourceFacingDir, directionMode)
  if converted:
    lvl = clamp(lvl - DIR_TO_REL_PENALTY, -3, 3)

  pushVal = pushForces[lvl]
  pushMagnitude = pushVal * PUSH_SCALE

  if pushMagnitude <= 0:
    return

  // 进入 unbalance + 施加冲量
  enterUnbalance(enemy, nowTime)

  impulse = dir * pushMagnitude
  enemy.rb.addForce(impulse, ForceMode2D.Impulse)

  // 保护时间刷新到 now+0.1s（文中描述：最后将期限调至0.1s后）
  enemy.unbalanceProtectUntilTime = nowTime + UNBALANCE_PROTECT
```

---

### 6.5 拉（Pull / Hook）实现

#### 缓动函数（默认 easeInQuart）

```pseudo
function easeInQuart(x):
  // x in [0,1]
  return x * x * x * x
```

#### Hook / PullLink 数据结构

```pseudo
class PullLink:
  Enemy target
  Vector2 sourcePos
  float initialDistance
  float basePull
  float startTime
  float maxDuration
  float linkDuration   // 默认 1.0
  bool active
```

#### 钩子命中（OnHit）

```pseudo
function onHookHit(hook, enemy, sourcePos,
                   sourceStrengthLevel, targetWeightLevel,
                   nowTime):

  if enemy.isFlying:
    hook.active = false
    return

  lvl = effectiveLevel(sourceStrengthLevel, targetWeightLevel)
  basePull = pullForces[lvl]

  if basePull <= 0:
    // 力为0：立刻停止钩子
    hook.active = false
    return

  // 记录基础数据
  hook.target = enemy
  hook.sourcePos = sourcePos
  hook.initialDistance = max(0.0001, distance(sourcePos, enemy.rb.position))
  hook.basePull = basePull
  hook.startTime = nowTime
  hook.active = true

  // 持续时间调整
  if lvl >= -1:
    hook.maxDuration = hook.linkDuration   // 默认 1s
  else:
    hook.maxDuration = 0.5

  // 进入 unbalance（若不免疫）
  if not enemy.hasUnbalanceImmune:
    enterUnbalance(enemy, nowTime)
    enemy.unbalanceProtectUntilTime = nowTime + UNBALANCE_PROTECT

  // 记为有效拉力来源
  enemy.activePullLinks.add(hook)
```

#### Hook 更新（每帧）

```pseudo
function updateHook(hook, dt, nowTime,
                    isTargetInBlockRangeFunc,
                    countActivePullSourcesFunc):
  if not hook.active:
    return

  // 超时结束
  if nowTime - hook.startTime >= hook.maxDuration:
    endHook(hook)
    return

  enemy = hook.target
  if enemy == null:
    endHook(hook)
    return

  // 若目标中途获得免疫：可以选择直接断开或仅停止施力
  if enemy.hasUnbalanceImmune:
    endHook(hook)
    return

  // 计算当前拉力
  currDist = distance(hook.sourcePos, enemy.rb.position)
  ratio = clamp(currDist / hook.initialDistance, 0, 1)
  magnitude = easeInQuart(ratio) * hook.basePull

  dir = normalize(hook.sourcePos - enemy.rb.position)

  // 每帧持续施加 Force
  enemy.rb.addForce(dir * magnitude, ForceMode2D.Force)

  // 阻挡范围内速度清零逻辑
  // 文中：检查位于范围内且只有单个拉力来源时会立刻清空目标速度
  if isTargetInBlockRangeFunc(enemy, hook.sourcePos):
    if countActivePullSourcesFunc(enemy) == 1:
      enemy.rb.velocity = (0,0)
```

#### Hook 结束

```pseudo
function endHook(hook):
  if not hook.active: return
  hook.active = false

  enemy = hook.target
  if enemy != null:
    enemy.activePullLinks.remove(hook)
```

---

## 7. 实现要点与“容易踩坑”的程序化备注

1. **mass=1 的意义**

* 推的 `Impulse` 可近似视为直接加速度/加速度变化（速度增量 = impulse 向量）
* 这也是为什么表层用“力量等级/重量等级”去做离散控制会非常稳定

2. **unbalance 的退出依赖“是否还有有效拉力来源”**

* 拉的存在会“黏住”unbalance：即使速度很低，只要还有 active hook，就不满足退出条件（按文档描述）

3. **directional 推可能在“角度过大或太近”时转 relative**

* 这会影响：方向 + 额外的有效力量等级惩罚（-2）

4. **拉力的衰减非常剧烈**（`ratio^4`）

* 距离越接近，ratio 越小，力会快速衰减，越拉到近处越“软”
