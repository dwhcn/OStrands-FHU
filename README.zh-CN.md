## FHU Companion

[English](README.md) | **简体中文**

当前 FHU Companion 的源码在 [`companion/`](companion/)。可从这套源码构建
简体中文和英文的 `FHUCompanion.dll`。两个语言版本都带有默认关闭的
「无存量也渗出」开关。Companion 可以和普通 OStrands 插件一起使用。

- [安装、依赖和升级说明](companion/INSTALL.txt)
- [构建说明](companion/BUILD.txt)
- [中英双语更新日志](companion/CHANGELOG.txt)
- [Nexus Mods 下载页](https://www.nexusmods.com/skyrimspecialedition/mods/199648?tab=files)

下方是早期集成版的说明。构建当前 Companion 时请进入 `companion/` 目录。

---

## 旧版 OStrands 与 Fill Her Up 集成

[English](README.md) | **简体中文**

OStrands 是一个面向 Skyrim SE/AE 的 SKSE 插件，在 OStim 和 SexLab 场景中绘制程序化生成的丝线、滴落和精液。本构建把它接入 **Fill Her Up（FHU）**：画面由 FHU 自己的释放特效和它存下的精液驱动，而不只是 OStrands 的高潮事件。

> **成人内容。** 本模组面向 OStim、SexLab 这类成人框架，仅限成年玩家。仓库里只有源代码和生成的网格、贴图，没有任何照片、视频或描绘性行为的图像。

### 本构建新增的内容

**FHU 释放特效。** FHU 用特效物品来播放释放。OStrands 监听 `sr_FillHerUp.esp` 里的 12 个释放特效物品：装备其中一个，就在对应的洞口开始释放；卸下就停止。阴道、肛门、口腔分开处理，各有开关。这些 FormID 只在数据载入时解析一次；没装这个 ESP 时插件照常作为原版 OStrands 运行，并在日志里注明没找到 FHU。

**由 FHU 存量驱动的渗漏。** 场景之外，每秒一次，OStrands 向 FHU 的任务脚本 `sr_inflateQuest` 查询 `GetVaginalCum`、`GetAnalCum`、`GetOralCum`、`isPlugged` 和 `isGagged`。每轮都查玩家，另外轮询附近最多 6 个角色。存量决定一个缓慢渗漏的储液池，所以角色在场景结束之后还会持续滴落。塞子和口球会按洞口阻断渗漏；只有在口腔渗漏开启时才会去查 `GetOralCum`。

**衣物阻挡流动。** `ClothingBlock` 在身体槽或两个骨盆槽被遮住时，同时阻断释放和渗漏。`Equipment Exceptions` 里的条目可以逐件覆盖这个自动判定，并指定作用于哪些洞口，所以暴露的服装可以标记成"不阻挡"。

**独立的设置页。** 以上内容都在 SKSE Menu Framework 里单独的 **Fill Her Up Release** 页面，和原有的 Strands / Semen / Arousal / Alignment 并列：每个洞口的释放与渗漏开关、渗漏速率、释放量、圆形水滴、随机流量，以及衣物阻挡规则。

**独立的流体参数。** FHU 流体有自己的一节 `[FillHerUp]`（在 `OStrands.ini` 里），量、强度、黏度、内聚力、拉伸、皮肤附着、不透明度、光照补偿、寿命和淡出时间都是独立的。它还跑在独立的流体域里，所以 FHU 流体和场景里的精液不会共用求解器或参数。你自己的改动照旧放在 `OStrands_User.ini`，更新不会覆盖。

### 生物支持（继承自基础分支）

生物支持来自本构建所基于的 [OStrands by LeonVanWinkle](https://gitlab.com/LilRicefield/ostrands) 分支，本构建未作改动。生物可以留在 SexLab 和 OStim 场景中，丝线、自动配对、身体碰撞和精液都跟随每个生物自己的阴茎骨骼。内置了 BakaFactory ABC 骨骼的装备参数，骨骼匹配不到的生物会从它身上穿着的勃起网格读取阴茎骨骼，而且生物只有在穿着勃起网格时才算有阴茎。细节见上游分支。

生物只能作为给予方。暂不支持生物作为接受方，也不支持生物的口腔，因为生物骨骼里没有 OStrands 要找的洞口骨骼和嘴部骨骼。

### 前置需求

- SKSE 和 Address Library for SKSE Plugins
- OStim Standalone 或 SexLab（支持 SexLab P+）
- Fill Her Up，本页的功能都需要它。没装则 OStrands 按原版运行
- 要使用生物功能：带生物动画的 SexLab 或 OStim、带阴茎骨骼的生物骨骼、勃起的生物网格（例如 More Nasty Critters 加 Creature Framework）。内置参数对应 BakaFactory ABC

可选：SKSE Menu Framework（游戏内设置）、OCum、SexLab Aroused / SLO Aroused NG / OSL Aroused（性奋滴落）、PPA - Penetration Physics。

### 设置

这些写在 `Data/SKSE/Plugins/OStrands.ini`。你自己的改动放在 `OStrands_User.ini`，更新不会覆盖它。

```ini
[FillHerUp]
Amount = 1.5              ; 释放时的丝线频率和液滴量
Strength = 1.0            ; 液体离开身体的速度
Viscosity = 50.0
Cohesion = 15.0
Stretch = 5.0
Stickiness = 0.4          ; 皮肤附着，越低越容易从皮肤上流走
Opacity = 0.9
LightingCompensation = 40.0
Lifetime = 30.0
FadeTime = 6.0

SeepRate = 1.0            ; 存量 FHU 液体的被动渗漏速率
ReleaseVaginal = true
ReleaseAnal = true
ReleaseOral = false
SeepVaginal = true
SeepAnal = true
SeepOral = false
RoundDrops = false        ; 用圆形水滴和积液代替丝线
ClothingBlock = true
```

这里的每一项也都能在游戏内的 Fill Her Up Release 页面调整。把 `Amount` 或 `SeepRate` 设成 `0` 就能关掉对应效果，不必去动开关。

### 生物设置

```ini
[Creatures]
Enabled = true

[CreatureRigs]
; 名称 = 从根部到顶端的阴茎骨骼 | 朝向轴（x、y 或 z）| 顶端延展 | 轴半径
troll = TD 2, TD 3, TD 4, TD 5, TD 6, TD 7 | x | 0.1 | 1.1
```

- 骨骼会使用第一个它全部拥有的参数组。如果一个都不匹配，就从生物穿着的勃起网格读取阴茎骨骼，所以没有参数组的包也能用。
- 生物只有在穿着蒙皮到这些骨骼的勃起网格时才算有阴茎。穿着软网格时没有。
- 重复某个内置参数组的名字并给出新值，即可修改它。
- 用 `名称 = none` 删除一个内置参数组。
- 用新名字新增参数组。
- 轴半径写 `0` 表示从穿着的网格上测量。

内置参数组覆盖：

| | | | |
|---|---|---|---|
| 熊 | 野猪 | 犬科 | 查鲁斯 |
| 鹿 | 尸鬼 | 雪精灵 | 石像鬼 |
| 巨人 | 山羊 | 海象 | 马 |
| 猛犸 | 雪地小精灵 | 剑齿虎 | 骷髅鼠 |
| 巨魔 | 吸血鬼领主 | 狼人 | |

查鲁斯那组也覆盖蜘蛛和收割者，巨人那组也覆盖潜伏者。

Creatures 开关也在 SKSE Menu Framework 设置的 Strands 页面里。

### 编译

1. 把 CommonLibSSE-NG 克隆进 `extern/`：
   ```
   git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG
   ```
2. 运行 `build.cmd`。需要 Visual Studio 的 C++ 生成工具和 vcpkg。插件输出到 `build/release/0_OStrands.dll`。
3. 运行 `package.cmd`，在 `dist/` 里生成发布用的 zip。

### 下载

构建好的模组发布在 Nexus Mods：**[Skyrim Special Edition mod 199648](https://www.nexusmods.com/skyrimspecialedition/mods/199648?tab=files)**，在那里下载后用模组管理器安装。

发布说明和源码归档同时也在 [releases 页面](https://github.com/dwhcn/OStrands-FHU/releases)。每个 tag 都对应本仓库的源码；要自己生成可安装的 zip，请按[编译](#编译)一节操作后再运行 `package.cmd`。

### 版本号

版本号跟随基础分支，基础分支又跟随上游 OStrands 并多一位：`上游版本.分支修订号`。`0.8.0.2` 是在上游 0.8.0 之上的第二个生物版本，也是本 Fill Her Up 构建的基础。

### 致谢

- OStrands 作者 LeonVanWinkle（Saint），crajjjj 亦有贡献。
- 生物支持由 Keevan2374 实现，勃起网格判定、网格回退和 OStim 生物部分由 crajjjj 完成。
- 内置生物参数组基于 PPA - Penetration Physics 附带的 BakaFactory ABC 覆盖。
- Fill Her Up 集成由 dwhcn 完成。
- 代码由 [codex](https://github.com/openai/codex) 实现，由维护者指导并审阅。

### 许可证

GNU General Public License v3.0，见 `LICENSE`。发布用的 zip 内含许可证副本，以及注明具体源码提交的声明文件。

源码：https://github.com/dwhcn/OStrands-FHU （上游 OStrands：https://gitlab.com/Keevan2374/ostrands）

---

> 本文与本次仓库发布由 DeepSeek Harness（AI 编码助手）在维护者的指示下撰写并提交。
