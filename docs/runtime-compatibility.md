# Redsand v2：源码来源、SE/AE 差异与新版兼容性

核对日期：2026-09-13。对象为 `output/BUILD.json` 中的 v2 DLL：
`D011D5D7A1C193788CF4B7CC181BF6B722470BE612A341E28A7C0F1003DBCF6C`。

## 为什么拉取 Precision 2.0.6 仍可能重新引入布局错误

`build.json` 分别锁定两个仓库：

| 组件 | 构建提交 |
| --- | --- |
| Precision 2.0.6 | `9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4` |
| CommonLibSSE-NG 7.0.0 | `8b032fa992750d654d6d38a33731714d8b86be1f` |

Precision 的 `src/CMakeLists.txt` 从环境变量 `CommonLibSSEPath` 引入 CommonLib。
Precision 自己的 `.gitmodules` 仅锁定 glm，没有锁定 CommonLib。它在 Hooks.cpp 访问
`character->setup->data->footIkDriverInfo`，编译器根据所用 CommonLib 头文件决定成员偏移。
相同的 Precision 提交，配合不同头文件，完全可能分别生成读取 `0x80` 或 `0x88` 的 DLL。

项目锁定的 CommonLib 把该字段写成 `0x80`。上游在提交
[`231543d61bc511865f1954f3eb2768732452c1d6`](https://github.com/alandtse/CommonLibSSE-NG/commit/231543d61bc511865f1954f3eb2768732452c1d6)
中修正为 `0x88`；提交时间是 2026-09-02 02:09:35 UTC，晚于项目锁定的 7.0.0。
重新构建不会自动越过 `build.json` 指定的旧提交。

本地原版 2.0.6 DLL 已被反汇编确认读取 `0x88`。可以确认其生成代码正确；不能仅凭这个结果确定
Ersh 当时使用了哪个 CommonLib 提交、分支或本地补丁。Precision 的 2.0.6 发布说明提到修复
CommonLib 布局引起的受击冲量问题，但没有给出可复现的完整依赖提交，不能据此断言该条说明专指 foot IK。

当前 v2 保留固定的依赖，用局部读取修正这一个成员，因此不要求把整个 CommonLib 升级。
“修复已进入 GitHub”与“本项目构建取到了修复”是两件事。

## 功能与资源差异

这里 SE/AE 指可执行文件运行时：SE 为 1.5.x，AE 为 1.6.x 及后续版本，包含 1.7.x。
是否购买 Anniversary Upgrade 不决定代码选择哪条分支。

| 项目 | 当前 v2 在 SE / AE 中的关系 |
| --- | --- |
| 分发 DLL | 同一个 x64 DLL，同时开启 SE 和 AE；关闭 VR |
| 粒子寿命、攻击结束后独立消散 | 同一份实现与参数，没有按 SE/AE 改变逻辑 |
| 发射停止、存活粒子计数、超时回收 | 同一实现 |
| 附魔颜色、亮度、默认轨迹 | 同一实现与 NIF/TOML 资源 |
| 玩家/NPC 范围与 75/50/25/5% 密度选项 | 同一 ESP 条件与模型选择逻辑 |
| 旧 Precision 飘带回退 | 同一逻辑，由角色范围控制，非游戏版本控制 |
| 命中判定、物理受击、hitstop、后坐、MCM/API | 保留上游相同功能目标；底层地址/钩子按运行时定位 |
| Foot IK 修复 | SE/AE 均读 `0x88`，旧 v1 的 AE 漏修已删除 |
| 材质、贴图、插件文件 | 两类游戏共用同一份，不提供 AE 独占效果 |
| 显示效果与帧率 | 没有故意制造版本差异；实际结果仍受 ENB、光照、其他模组、硬件和设置影响，不保证逐像素或帧率相同 |

已检查 7 个 ESP：HEDR 均为 1.70，Form Version 为 44，带 ESL 标记。
主插件只依赖 `Skyrim.esm`；可选插件额外依赖 `PrecisionParticleMatch.esp`，没有 Anniversary/Creation Club 主文件依赖。

## 底层版本适配差异

### 引擎地址和钩子

Precision 使用 `RELOCATION_ID(SE_ID, AE_ID)` 定位函数、全局变量和对象标识，通过安装在游戏端的
Address Library 取得对应版本地址。函数内部的插入点另由 `RELOCATION_OFFSET` 决定；
地址库并不能自动修正 C++ 成员偏移，也不能保证任意未来版本的函数内部指令仍一样。

在锁定的 Precision 源码中，除地址定位外，显式版本条件包括：

- `Hooks.cpp:768`：物理步钩子的汇编分为 SE 与 AE。SE 从 r12 取 `bhkWorld`，AE 从 r14 取；插入/返回位置和需要恢复的指令也不同。
- `PCH.h:143`：三参数 `RELOCATION_OFFSET` 的第三项是 **AE 1.7.99 及以后**，不是 VR。
- `Hooks.h` 中有 3 个这种三路钩子：主更新、命中粒子效果放置、碰撞收集。
- 其余直接钩子使用 SE/AE 两路偏移；本文件末尾列出全部 31 个有效调用位置，其中包含物理步 SE/AE 分支的两个调用点。
- 插件启动沿用上游 SKSE 查询和版本声明。SE 查询入口拒绝低于 1.5.39 的运行时，这只是加载条件，不表示本项目已经验证所有中间版本。

### CommonLib 接口

以下为与本模组直接相关的接口示例，说明底层存在差异，并非整个游戏 ABI 的穷举：

| 数据/接口 | SE 1.5.97 | AE 1.6.1170 | AE 1.7.99+ |
| --- | --- | --- | --- |
| Actor 的运行时数据起点 | `0xE0` | `0xE8` | `0xE8` |
| TESObjectREFR 的运行时数据起点 | `0x88` | `0x90` | `0x90` |
| AsActorState 起点 | `0xB8` | `0xC0` | `0xC0` |
| PlayerCharacter 运行时数据起点 | `0x3D8` | `0x3E0` | `0x3E8` |
| NiParticleSystem 修改器列表起点 | `0x168` | `0x168` | 当前依赖仍使用 `0x168` |
| NiParticles::particleData | `0x158` | `0x158` | 当前依赖仍使用 `0x158` |
| NiParticlesData::numVertices | `0x7C` | `0x7C` | 当前依赖仍使用 `0x7C` |

Actor/TESObjectREFR 的变化由 `1.6.629` 门槛处理；PlayerCharacter 另有 `1.7.99` 门槛。
粒子接口中常见的第二个不同偏移属于 **VR**，不能误认为 AE。例如 `GetParticleSystemRuntimeData(0x168, 0x1A8)`
对应的是 SE/AE 与 VR。本包不编译 VR。

### 地址库格式与 DLL 声明

锁定的 CommonLib `IDDB.cpp` 已实现地址库格式 1、2、5 的读取，格式 5 用于 1.7.99+。
直接读取 v2 DLL 导出的 `SKSEPlugin_Version` 得到：

- `versionIndependence = 1`：Address Library 标记。
- `versionIndependenceEx = 3`：NoStructUse 与 AddressLibraryV5 标记。
- `compatibleVersions = [1.7.99.0]`：由旧依赖中的 `RUNTIME_SSE_LATEST` 生成。

这不表示 DLL 只允许 1.7.99。SKSE 的版本独立机制生效时，并不按这个显式列表逐一限制版本。
同样，NoStructUse 是兼容性声明，不是“代码实际上不访问结构体”的证明，更不能取代实机测试。

## 1.6.1170 及后续版本如何判断

| 运行时 | 当前 v2 的代码支持依据 | 实机状态 |
| --- | --- | --- |
| 1.5.97 Steam | 编译启用 SE、对应地址/钩子与 Foot IK 修正 | 本任务没有完成 v2 游戏内复测 |
| 1.6.1170 Steam | 编译启用 AE，使用旧 AE 钩子组及 `0x88` 修正 | 需要反馈玩家复测 |
| 1.6.1179 GOG | 按 AE 运行时处理，需 GOG 对应 SKSE 和地址数据 | 未实测，不能当作已经验证的 Steam 小升级 |
| 1.7.99 Steam | Precision 2.0.5 引入的专门适配被 2.0.6/v2 保留；CommonLib 有格式 5 支持 | 未实测 |
| 1.7.104 Steam | 落入 1.7.99+ 分支；SKSE 与地址库已有对应版本，可作为预期兼容目标 | 未实测，不能保证整套模组正常 |
| 未知未来版本 | 可能仍进入 AE 分支，但地址、结构或指令可能继续改变 | 不能承诺全部兼容 |
| VR / Game Pass / Epic | 本包禁用 VR；SKSE 官方不支持后两类发行版 | 不支持本包的这条运行路径 |

截至核对日期，SKSE 官网列出：Steam 1.7.104 对应 2.3.1；GOG 1.6.1179 对应 2.2.6；
SE 1.5.97 对应 2.0.20。Steam 1.6.1170 对应 SKSE 2.2.6 的历史发布。
Address Library 当前 v13 的文件说明覆盖到 1.7.104。游戏、SKSE、地址库必须匹配，
Precision 的 MCM Helper、SkyUI 与动画生成等前置也仍需在目标游戏版本正常工作。

建议发布描述使用：“同一 DLL 提供 SE/AE 支持，包含 Precision 2.0.6 的 1.7.99+ 适配；
实际测试通过的版本另列。其他版本待验证。”只有收到复测结果后，才添加对应版本的“已验证”标记。

来源：

- [Precision 构建依赖入口](https://github.com/ersh1/Precision/blob/9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4/src/CMakeLists.txt)
- [Precision 版本偏移分派](https://github.com/ersh1/Precision/blob/9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4/src/PCH.h)
- [Precision 发布说明](https://www.nexusmods.com/skyrimspecialedition/mods/72347)
- [SKSE 当前支持版本](https://skse.silverlock.org/)
- [SKSE 2.2.6 支持 1.6.1170](https://github.com/ianpatt/skse64/releases/tag/v2.2.6)
- [SKSE 插件兼容性检查](https://github.com/ianpatt/skse64/blob/master/skse64/PluginManager.cpp)
- [Address Library 文件与版本范围](https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files)

## Precision 直接钩子偏移清单

下面列出对象源码中的有效 `RELOCATION_OFFSET` 调用。数值是**各自目标函数内部的相对偏移**，不能跨函数直接比较，也不是可独立用于二进制修改的地址。

| 源码位置 | 钩子 | SE | AE < 1.7.99 | AE >= 1.7.99 |
| --- | --- | --- | --- | --- |
| `Hooks.cpp:773` | `HookPrePhysicsStep` | `0x2A9` | `0x407` | `0x407` |
| `Hooks.cpp:779` | `HookPrePhysicsStep` | `0x2A9` | `0x407` | `0x407` |
| `Hooks.h:18` | `_Nullsub` | `0x748` | `0xC26` | `0xC38` |
| `Hooks.h:19` | `_ApplyMovement` | `0xF0` | `0xFB` | `0xFB` |
| `Hooks.h:58` | `_Func1` | `0x2C1` | `0x2CA` | `0x2CA` |
| `Hooks.h:59` | `_Func2` | `0x2DD` | `0x2E6` | `0x2E6` |
| `Hooks.h:60` | `_ApplyPerkEntryPoint` | `0x343` | `0x34F` | `0x34F` |
| `Hooks.h:61` | `_HitData_Populate1` | `0x1B7` | `0x1C6` | `0x1C6` |
| `Hooks.h:62` | `_HitData_Populate2` | `0xEB` | `0x110` | `0x110` |
| `Hooks.h:63` | `_TESObjectCELL_PlaceParticleEffect` | `0xABD` | `0xB39` | `0xB49` |
| `Hooks.h:64` | `_ApplyDeathForce` | `0x10A` | `0x10A` | `0x10A` |
| `Hooks.h:65` | `_HitActor_GetAttackData` | `0xB3` | `0xC2` | `0xC2` |
| `Hooks.h:67` | `_CdPointCollectorCast` | `0x26A` | `0x294` | `0x2AC` |
| `Hooks.h:69` | `_HitData_GetAttackData` | `0xD3` | `0xDD` | `0xDD` |
| `Hooks.h:70` | `_HitData_GetWeaponDamage` | `0x1A5` | `0x1A4` | `0x1A4` |
| `Hooks.h:71` | `_HitData_GetBashDamage` | `0x22F` | `0x226` | `0x226` |
| `Hooks.h:72` | `_HitData_GetUnarmedDamage` | `0x26A` | `0x24D` | `0x24D` |
| `Hooks.h:125` | `_GetMaxRange` | `0x147` | `0x128` | `0x128` |
| `Hooks.h:169` | `_TESCamera_Update` | `0x1A6` | `0x1A6` | `0x1A6` |
| `Hooks.h:239` | `_ProcessHavokHitJobs` | `0x104` | `0xFC` | `0xFC` |
| `Hooks.h:242` | `_hkbRagdollDriver_DriveToPose` | `0x25B` | `0x256` | `0x256` |
| `Hooks.h:243` | `_hkbRagdollDriver_PostPhysics` | `0x18C` | `0x18B` | `0x18B` |
| `Hooks.h:247` | `_QueueTask_ToggleCharacterBumper` | `0x1F` | `0x1F` | `0x1F` |
| `Hooks.h:248` | `_ToggleCharacterBumper` | `0x56` | `0x56` | `0x56` |
| `Hooks.h:250` | `_bhkCollisionFilter_CompareFilterInfo1` | `0x16F` | `0x16F` | `0x16F` |
| `Hooks.h:251` | `_bhkCollisionFilter_CompareFilterInfo2` | `0x17` | `0x17` | `0x17` |
| `Hooks.h:252` | `_bhkCollisionFilter_CompareFilterInfo3` | `0xBC` | `0xBC` | `0xBC` |
| `Hooks.h:253` | `_bhkCollisionFilter_CompareFilterInfo4` | `0x31` | `0x31` | `0x31` |
| `Hooks.h:254` | `_bhkCollisionFilter_CompareFilterInfo5` | `0x17` | `0x17` | `0x17` |
| `Hooks.h:255` | `_bhkCollisionFilter_CompareFilterInfo6` | `0x118` | `0x118` | `0x118` |
| `Hooks.h:256` | `_bhkCollisionFilter_CompareFilterInfo7` | `0x128` | `0x128` | `0x128` |
