# 尸鬼命中后动作卡住：SE / AE Foot IK 修复 v2

## 结论

旧的 Redsand 粒子 DLL 使用了有布局错误的 CommonLib 头文件；v1 修复只覆盖 SE。
在 AE（包括用户反馈的 1.6.1170）上，v1 仍读取 `hkbCharacterData + 0x80`。
本地 Nexus 安装来源的原版 Precision 2.0.6 DLL 在相同命中物理代码中读取 `+0x88`，没有 SE/AE 分支。
因此，“同为 2.0.6，原版正常，两个重编译版本异常”并不矛盾。

v2 对 SE 和 AE 均读取 `+0x88`，保留原本的脚部 IK 驱动、动画图与姿态轨道检查。
修复仅绕过一个错误的成员声明，不整体升级 CommonLib，也不关闭命中物理功能。

**证据边界：** 已核对源码、原版与旧修改版二进制，以及本地 1.5.97 的反射记录。
尚未在本任务中完成 v2 的游戏内测试，也没有本地 1.6.1170 游戏实例；不能据此宣布所有尸鬼问题均已修复。

## 证据与原因

构建锁定：

- Precision：`9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4`（2.0.6）。
- CommonLibSSE-NG：`8b032fa992750d654d6d38a33731714d8b86be1f`。

旧 CommonLib 声明把 `footIkDriverInfo` 放在 `0x80`，而原生布局在这里存放
`characterPropertyValues`，真正的 `footIkDriverInfo` 在 `0x88`。
本地 1.5.97 反射记录分别位于 VA `0x141707860` 与 `0x141707888`，成员偏移分别为 `0x80` 与 `0x88`。
本地保留的 AE 1.6.318 二进制也给出相同成员偏移，反射记录分别位于 VA `0x1417F7DC0` 与 `0x1417F7DE8`；这不等同于已经检查 1.6.1170 的可执行文件。

本地二进制对照（RVA 为相对 DLL 基址的地址，不是对象成员偏移）：

| DLL | SHA-256 | 命中物理中的读取 |
| --- | --- | --- |
| 原版 Precision 2.0.6，MO2 的 `精准判定-Precision` | `1808784CA738A9FD04926A1F05E3EB4DD51560B751C5E4E9B599D628C2ED9CF5` | RVA `0x54CB2`：`cmp qword ptr [rcx + 0x88], rsi` |
| 旧 Redsand Particles | `0A582465BED6BFB784FC19CC4108D72104E4E033A1BF1E1988A65FFACB384974` | RVA `0x56B41`：`cmp qword ptr [rcx + 0x80], rsi` |
| SE 修复 v1 / Draugr Hit Reaction Fix | `5BCE9E36B6597938679C5EF544EA0FA0DD8A5E01BFC91861AB884F1E9486CA35` | SE：RVA `0x56BD2` 读取 `0x88`；AE：RVA `0x56BDB` 仍读取 `0x80` |

上游命中物理代码在认为脚部 IK 有效时，把 `hkbCharacter.poseLocal` 复制回生成器的姿态轨道。
如果 `0x80` 的角色属性指针非空，但真正的脚部 IK 信息为空，就可能错误进入这个复制分支。
对没有生成相应脚部 IK 姿态的角色，这会有把旧姿态写回当前动画的风险，与受击后卡住、慢动作的表现相符。
具体玩家的尸鬼是否恰好满足这些运行时条件，仍需复测验证。

旧 v1 中有以下条件，这也是此前单独测试 `HasSEFootIKDriverInfo` 没有发现 AE 漏修的原因：

```cpp
REL::Module::IsSE()
    ? RedsandHavokLayout::HasSEFootIKDriverInfo(characterData)
    : characterData->footIkDriverInfo.get() != nullptr; // AE: 仍为 0x80
```

v2 去掉这处分支，让两类游戏共同使用 `HasFootIKDriverInfo`。
回归测试直接包含构建用的 `HavokFootIK.inc`，覆盖“属性非空、脚部 IK 为空”、
“脚部 IK 非空、属性为空”、缺失 setup/data/driver，以及模拟 SE/AE 两条调用环境。
这些是内存布局与控制流测试，不是游戏仿真。

参考源码：

- [构建所用的旧 CommonLib 声明](https://github.com/alandtse/CommonLibSSE-NG/blob/8b032fa992750d654d6d38a33731714d8b86be1f/include/RE/H/hkbCharacterData.h)
- [Precision 2.0.6 命中物理复制逻辑](https://github.com/ersh1/Precision/blob/9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4/src/Hooks.cpp#L1482-L1497)
- [目前上游已修正为 0x88 的 CommonLib 声明](https://github.com/alandtse/CommonLibSSE-NG/blob/8d558ccdb59a0bb93d7450c85ada67f3fa4b52d4/include/RE/H/hkbCharacterData.h)

## MO2 与玩家复测

1. 备份原有 DLL。将 v2 安装为单独的测试模组，令其 `SKSE/Plugins/Precision.dll` 覆盖原版 Precision、Redsand Particles 和旧 `Draugr Hit Reaction Fix`。通过 MO2 的 Data 页或文件冲突页核对最终来源。上面的 SHA-256 可辨认旧 DLL。
2. 完全退出游戏后重新启动。DLL 不会因为重读存档而重新加载。检查 `Documents/My Games/Skyrim Special Edition/SKSE/Precision.log`，应有 `Redsand Havok foot IK layout fix v2: SE/AE offset=0x88, runtime=...`，并确认玩家对应的运行时版本。
3. 使用尚未把目标打到异常状态的同一存档，保持动画、骨架、Precision MCM 设置不变。先仅替换 DLL，对照原版 2.0.6 与 v2，分别连续攻击尸鬼和普通人形 NPC，观察受击、行走、攻击、恢复。不要在已经卡住的目标上判断修复失败。
4. 分别复测 1.5.97 与 1.6.1170，并确认粒子尾迹在攻击结束后仍能独立消散。
5. 若 v2 日志已确认但仍异常，先在 Precision MCM 关闭 **Hit Impulse / Apply impulse on hit** 作对照；它用于定位受击物理路径，不应当作为必须永久关闭的修复。恢复该项后再单独关闭 **Hitstop / Apply hitstop to target**，区分姿态复制问题与受击停顿。每次从干净状态重试。
6. 反馈游戏版本、`Precision.log`、实际生效 DLL 的 SHA-256，以及两项开关的对照结果。`bCopyFootIkToPoseTrack` 在本项目锁定的上游中是源码内的开关，没有被 `Settings::ReadSettings` 读取；把同名字段手动加到 INI 不会生效。

本次检查时，提供的 MO2 配置同时启用了上述多个 DLL 提供者，旧 `Draugr Hit Reaction Fix` 在列表中覆盖 Redsand Particles。
另外 `1.Precision 精准` 内的 DLL 实际版本为 2.0.4，不能只根据模组名称判断正在测试的版本。
本任务生成的测试包位于项目 `output` 目录；不会自动改变玩家的游戏安装或 MO2 启用列表。
