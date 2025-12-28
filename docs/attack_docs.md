> 约定：你们模拟器会支持改变帧率，因此我统一用 **Tick（离散更新步）** 和 **dt（每 tick 的秒数）** 来表述，不绑定 30fps。
> 只保留“会导致行为差异”的离散化/顺序/阈值机制；具体“多少帧”不重要。

---

## 1. 名词与数据结构

### 1.1 Tick 时间系统

* `dt`：每次模拟更新的时间步长（秒），例如 `dt = 1 / tickRate`
* `tickIndex`：整数 tick 计数（推荐作为“权威时间”）
* `timeSec`：可选的秒时间（建议用 double；若用 float 可能复现文中精度问题）

> **强烈建议**：内部使用 `tickIndex` 或 `double`，避免 float 累加造成的边界抖动（文档里 128s 附近的问题本质是 float 精度 + 四舍五入边界）。

### 1.2 攻击能力 AttackAbility 的核心状态

攻击被视为一种 ability（能力），包含一个**施放（cast）状态机**与一个**冷却倒计时（cooldown）**。

**状态机阶段**（一次完整攻击）：

1. `CastStart`：攻击开始
2. `PreDelay`：前摇（到“造成效果事件”之前）
3. `SpellOn` / `OnHitEvent`：造成效果（命中/发射/结算点）
4. `PostDelay`：后摇（到攻击结束）
5. `CastEnd`：攻击结束

**关键字段（示例）**：

* `phase`：`Idle / PreDelay / PostDelay`
* `castStartTick`：本次攻击开始的 tick
* `castDurationSec`：本次攻击总持续时间（秒）（受攻速、动画长度、最大拉伸比例影响）
* `preDelayTicksRemaining`：前摇剩余 tick
* `postDelayTicksRemaining`：后摇剩余 tick
* `cooldownSec`：冷却剩余时间（秒，倒计时）
* `skipCooldownUpdateOnce`：布尔值（能力执行第 1 tick 不更新冷却）
* `pendingAnimAttackEvent`：是否已收到动画攻击事件（若采用动画事件驱动）
* `resetCDThresholdSec`：冷却重置阈值（通常为 `0.5 * dt`，白金为 0 —— 文档原话）

---

## 2. 需要精确复刻的“行为规则”

### 2.1 攻击开始时：计算持续时间与动画播放速度（可插拔）

文档描述要点：

* `CastStart` 时，根据：

  * 当前攻速
  * 攻击动画长度
  * 动画最大拉伸比例（最大变速幅度）
* 计算本次攻击持续时间 `castDurationSec`
* 以此反推动画播放速度 `animSpeed = animLengthSec / castDurationSec`（概念上如此）

> 具体公式文档未给出，建议你们把它做成可替换策略：`ComputeCastDurationSec(...)`。

### 2.2 前摇 PreDelay：记录开始时间，并“离散化到 tick”

文档描述要点：

* 前摇开始时记录 `startTime`（游戏时间）
* 前摇持续时间会被“化为整帧”（离散化为整数 tick）
* 若攻击依赖动画：收到 spine 动画攻击事件时结束前摇并进入结算

抽象成模拟器规则：

* 前摇持续可以用两种驱动方式：

  1. **时间驱动**：倒计时 tick 到 0 触发 `SpellOn`
  2. **动画事件驱动**：先等事件到达；事件到达的 tick 上触发 `SpellOn`
* 无论哪种，事件/触发点都落在 tick 边界（离散化不可避免）

### 2.3 后摇 PostDelay：用“总时长 - 已耗时”计算，并四舍五入到 tick

文档描述要点：

* 后摇开始时再取一次当前游戏时间
* `postDelaySec = castDurationSec - (nowTime - startTime)`
* 然后对后摇“**四舍五入至整帧**”（Round to nearest tick）

抽象成模拟器规则：

* `elapsedSec` 用 **tick 差**算更稳：`elapsedSec = (currentTick - castStartTick) * dt`
* `postDelayTicks = RoundToNearestTick(postDelaySec, dt)`
* 可选：实现“后摇至少 1 tick”的保底（文档在“其它问题”提到某技能存在 1 帧保底后摇）

### 2.4 冷却 cooldown：倒计时，且更新顺序会导致“多 1 tick”现象

文档描述的两个关键逻辑（需要体现为“顺序规则”）：

1. **每个 tick 内：攻击触发判定（状态机 tick）在冷却更新之前**
2. **能力执行的第 1 tick 不更新冷却**

由此会产生一个普遍现象：

* 如果某次攻击结束时，冷却还没到 0
  那么即使它在“本 tick 的冷却更新之后”变为 0，也要等到**下一个 tick 的触发判定**才能开打
  → 体感就是攻击间隔“多 1 tick”。

### 2.5 FinishCallback：攻击结束后“立刻尝试触发下一次攻击”

文档描述要点：

* 有个特殊机制：攻击结束后会立刻尝试触发下一次攻击（避免某些角色多 1 tick）

抽象成模拟器规则：

* 在 `CastEnd` 处，直接调用一次 `TryStartAttack()`（同一 tick 内的“额外尝试”）
* 这次尝试发生在“正常触发判定 tick”之外，是一个补偿逻辑

### 2.6 resetCDStrategy：攻击结束时的“冷却重置阈值”

文档描述要点：

* 在 `CastEnd`（且在“立刻尝试触发下一次攻击之前”）
* 若剩余冷却 `< 固定阈值`（多数为半帧，即 `0.5 * dt`，白金为 0）
* 则将冷却直接置 0（重置）

抽象成模拟器规则：

* `if cooldownSec < resetCDThresholdSec: cooldownSec = 0`
* 然后再执行 `FinishCallbackTryStartAttack()`

> 这机制就是“对齐/四舍五入”在攻击结束点生效的版本。注意它**只在 CastEnd 执行**，不是实时生效。

---

## 3. 每 Tick 的更新顺序（强约束）

为了同时满足：

* “触发判定在冷却更新之前”
* “FinishCallback 能在冷却归零的那个 tick 里立刻接上（对某些角色消除多 1 tick）”

我建议你们用下面这个顺序（这是一个能覆盖文档现象的最小模型）：

对每个实体，每个 tick：

1. `TriggerCheckTick()`：常规触发判定（若 idle 且 cooldown<=0 则开始攻击）
2. `CooldownUpdateTick()`：冷却倒计时更新（但若本 tick 是“能力第 1 tick”，则跳过一次）
3. `CastUpdateTick()`：更新前摇/后摇；若在此 tick 内进入 `CastEnd`：

   * 先执行 `resetCDStrategy`
   * 再执行 `FinishCallback`（立刻尝试下一次攻击）

> 关键点：**FinishCallback 在本 tick 的冷却更新之后发生**，因此有机会“刚好接上”冷却在此 tick 更新到 0 的情况。

---

## 4. 与帧率无关的离散化函数

你们只需要把所有“化为整帧/四舍五入”换成“化为整 tick”。

```pseudo
function RoundToNearestTick(seconds, dt):
    // 四舍五入到最近 tick 数
    // 等价：round(seconds / dt)
    x = seconds / dt
    return floor(x + 0.5)

function CeilToTick(seconds, dt):
    x = seconds / dt
    return ceil(x)

function ClampNonNegativeInt(n):
    return max(0, n)

function ClampNonNegativeSec(s):
    return max(0.0, s)
```

文档明确提到后摇是“四舍五入”，因此后摇建议用 `RoundToNearestTick`。
前摇只说“化为整帧”，你们可以按需要选择 `RoundToNearestTick` 或 `CeilToTick`，但若目标是贴近文档叙述，优先用 Round。

---

## 5. 伪代码：AttackAbility（核心实现）

### 5.1 配置与状态

```pseudo
enum AttackPhase { Idle, PreDelay, PostDelay }

struct AttackConfig:
    animLengthSec                 // 动画原始长度
    animMaxStretchRatio           // 动画最大拉伸（限制最大加速）
    baseAttackIntervalSec         // 理论攻击间隔(未考虑攻速时的基准)
    usesAnimEvent                 // 是否依赖动画事件触发 SpellOn
    resetCDThresholdPolicy        // e.g. HalfTick, Zero, Custom
    minPostDelayTicks             // 可选：后摇保底（一般为0；某些技能可能=1）
    finishCallbackEnabled         // 是否启用结束后立刻尝试下一次攻击

struct AttackState:
    phase: AttackPhase
    castStartTick: int
    castDurationSec: double

    preDelayTicksRemaining: int
    postDelayTicksRemaining: int

    cooldownSec: double
    skipCooldownUpdateOnce: bool

    pendingAnimAttackEvent: bool  // 动画事件是否到达（由动画系统写入）
```

### 5.2 外部依赖接口（模拟器可插拔）

```pseudo
interface ITargeting:
    function HasValidTarget(entity) -> bool

interface ICombatResolver:
    function ApplyAttackEffect(entity, target)

interface IAnimationDriver:
    function PlayAttackAnimation(entity, speed)
    // 若 usesAnimEvent=true：动画系统在事件点将 state.pendingAnimAttackEvent = true
```

### 5.3 计算 castDurationSec（可插拔策略）

```pseudo
function ComputeCastDurationSec(entity, cfg, attackSpeedMultiplier) -> double:
    // 文档说“根据攻速、动画长度、最大拉伸比例计算”
    // 具体公式未知，做成策略即可
    //
    // 示例：动画最多加速到 maxStretchRatio（即最短持续 = animLength / maxStretchRatio）
    // 攻速越高，希望越短，但不能短于最短持续
    desired = cfg.baseAttackIntervalSec / attackSpeedMultiplier
    minDur  = cfg.animLengthSec / cfg.animMaxStretchRatio
    return max(desired, minDur)
```

> 上面只是示意：真实游戏里“攻击间隔”和“动画持续”不一定严格同一套公式，但你们的模拟器用策略隔离即可。

---

## 6. 伪代码：每 Tick 更新（含顺序）

### 6.1 总入口

```pseudo
function TickEntityAttack(entity, attackCfg, attackState, dt, tickIndex):
    TriggerCheckTick(entity, attackCfg, attackState, dt, tickIndex)
    CooldownUpdateTick(attackState, dt)
    CastUpdateTick(entity, attackCfg, attackState, dt, tickIndex)
```

### 6.2 触发判定（状态机 tick，优先于冷却更新）

```pseudo
function TriggerCheckTick(entity, cfg, st, dt, tickIndex):
    if st.phase != Idle:
        return

    if st.cooldownSec > 0:
        return

    if not Targeting.HasValidTarget(entity):
        return

    StartAttackCast(entity, cfg, st, dt, tickIndex)
```

### 6.3 开始攻击（CastStart）

```pseudo
function StartAttackCast(entity, cfg, st, dt, tickIndex):
    st.phase = PreDelay
    st.castStartTick = tickIndex
    st.pendingAnimAttackEvent = false

    attackSpeedMultiplier = entity.GetAttackSpeedMultiplier()

    // 1) 计算本次攻击总持续
    st.castDurationSec = ComputeCastDurationSec(entity, cfg, attackSpeedMultiplier)

    // 2) 播放动画（按持续时间反推播放速度）
    animSpeed = cfg.animLengthSec / st.castDurationSec
    Animation.PlayAttackAnimation(entity, animSpeed)

    // 3) 初始化前摇（事件点/前摇时长由你们定义：可来源动画标记或配置）
    preDelaySec = entity.GetPreDelaySecScaledBy(animSpeed, st.castDurationSec)
    preTicks = RoundToNearestTick(preDelaySec, dt)
    st.preDelayTicksRemaining = ClampNonNegativeInt(preTicks)

    // 4) 进入冷却（倒计时）
    // 文档：攻击开始瞬间进入冷却，冷却=理论攻击间隔
    theoreticalIntervalSec = entity.GetTheoreticalAttackIntervalSec()
    st.cooldownSec = ClampNonNegativeSec(theoreticalIntervalSec)

    // 5) 文档：能力执行第1 tick不更新冷却
    st.skipCooldownUpdateOnce = true
```

> `GetPreDelaySecScaledBy(...)` 你们可以按项目需要：
>
> * 若你们有动画事件时间点：可直接 `preDelaySec = eventTimeWithinAttack`
> * 或者 `preDelaySec = cfg.preDelayRatio * castDurationSec`

### 6.4 冷却更新（倒计时；且第 1 tick 跳过一次）

```pseudo
function CooldownUpdateTick(st, dt):
    if st.cooldownSec <= 0:
        return

    if st.skipCooldownUpdateOnce:
        st.skipCooldownUpdateOnce = false
        return

    st.cooldownSec = max(0.0, st.cooldownSec - dt)
```

### 6.5 施放更新（PreDelay / PostDelay / CastEnd）

```pseudo
function CastUpdateTick(entity, cfg, st, dt, tickIndex):
    if st.phase == Idle:
        return

    if st.phase == PreDelay:
        UpdatePreDelay(entity, cfg, st, dt, tickIndex)
        return

    if st.phase == PostDelay:
        UpdatePostDelay(entity, cfg, st, dt, tickIndex)
        return
```

#### PreDelay：等待事件/倒计时，触发 SpellOn，并计算 PostDelay

```pseudo
function UpdatePreDelay(entity, cfg, st, dt, tickIndex):
    eventHappened = false

    if cfg.usesAnimEvent:
        // 动画系统在事件点将 st.pendingAnimAttackEvent 置 true
        if st.pendingAnimAttackEvent:
            eventHappened = true
            st.pendingAnimAttackEvent = false
    else:
        st.preDelayTicksRemaining -= 1
        if st.preDelayTicksRemaining <= 0:
            eventHappened = true

    if not eventHappened:
        return

    // SpellOn：造成效果
    target = entity.GetCurrentTarget()
    Combat.ApplyAttackEffect(entity, target)

    // 进入后摇：用“总时长 - 已耗时”计算（文档公式）
    elapsedSec = (tickIndex - st.castStartTick) * dt
    remainingSec = st.castDurationSec - elapsedSec

    postTicks = RoundToNearestTick(remainingSec, dt)
    postTicks = max(postTicks, cfg.minPostDelayTicks)
    st.postDelayTicksRemaining = ClampNonNegativeInt(postTicks)

    st.phase = PostDelay
```

> 这里的 `elapsedSec` 用 tick 差计算，避免 `float now - float start` 的精度抖动。
> 如果你们**想复现文档里的精度 bug**，可以改成：
>
> * `elapsedSec = (floatTimeNow - floatTimeStart)` 并用 float 存储时间（不推荐默认）。

#### PostDelay：倒计时到 0 进入 CastEnd

```pseudo
function UpdatePostDelay(entity, cfg, st, dt, tickIndex):
    st.postDelayTicksRemaining -= 1
    if st.postDelayTicksRemaining > 0:
        return

    FinishAttackCast(entity, cfg, st, dt, tickIndex)
```

#### CastEnd：resetCDStrategy + FinishCallbackTry

```pseudo
function FinishAttackCast(entity, cfg, st, dt, tickIndex):
    // CastEnd
    st.phase = Idle

    // resetCDStrategy：攻击结束时检查剩余冷却，小于阈值则重置
    thresholdSec = ResolveResetThreshold(cfg, dt, entity)
    if st.cooldownSec < thresholdSec:
        st.cooldownSec = 0.0

    // FinishCallback：立刻尝试触发下一次攻击（文档强调在 reset 之后）
    if cfg.finishCallbackEnabled:
        TriggerCheckTick(entity, cfg, st, dt, tickIndex)
```

```pseudo
function ResolveResetThreshold(cfg, dt, entity) -> double:
    // 文档：除白金为0外，其它多为半帧（半个tick）
    if entity.IsPlatinum():
        return 0.0
    return 0.5 * dt
```

---

## 7. 关键“现象”在该模型里的对应解释（便于你们验收）

### 7.1 “多 1 tick”来自哪里？

* 常规触发判定在冷却更新之前
* 若某 tick 内冷却从 `>0` 更新到 `0`

  * 本 tick 的触发判定已经错过
  * 且此时角色可能是 Idle（攻击已经结束）
  * 所以只能等下一个 tick 再触发
    → 攻击间隔就表现为“理论值 + 1 tick”

### 7.2 为什么有些角色不会多 1 tick？

* 因为 `CastEnd` 处的 `FinishCallbackTryStart` 是“额外触发尝试”
* 并且它发生在 resetCDStrategy 之后（冷却可能被重置为 0）
* 当“攻击结束时刻”与“冷却归零”足够接近时，就能在同 tick 内无缝衔接下一次攻击

### 7.3 文档里的浮点精度问题应该怎么落地？

如果你们要做两种模式：

* **推荐模式（稳定，不复现 bug）**：

  * 时间用 `tickIndex` 或 `double`
  * `elapsedSec = (tickIndex - castStartTick) * dt`
  * 四舍五入前可加一个极小 epsilon 防边界抖动（可选）

* **兼容模式（可能复现 bug）**：

  * 用 `float fixedPlayTime += dt` 累加
  * 后摇用 `postDelaySec = castDurationSec - (nowFloat - startFloat)`
  * 再做 `RoundToNearestTick`
    → 运行足够久后可能在 0.5 tick 边界出现 ±1 tick 的跳变

---

## 8. 可选：补充“动画事件未到则强制后摇保底”（对应文档“其它问题”）

文档提到某类情况：等不到攻击事件，强制进入后摇且后摇至少 1 tick。

可用下面的保护逻辑（只在 usesAnimEvent=true 时启用）：

```pseudo
// 在 PreDelay 中加一个最大等待 tick 数（例如来自配置）
if cfg.usesAnimEvent:
    st.preDelayTicksRemaining -= 1
    if st.pendingAnimAttackEvent:
        eventHappened = true
    else if st.preDelayTicksRemaining <= 0:
        // 超时：强制 SpellOn 或强制进入 PostDelay（视技能而定）
        eventHappened = true
        cfg.minPostDelayTicks = max(cfg.minPostDelayTicks, 1)
```