# 决构编辑器 · YGOPro

基于 Fluorohydride/YGOPro 的局面编辑与 Combo 推演工具。一个玩家可控制双方，自由修改局面，再从当前时点继续对局。

- 开局自定义双方卡组、名称和起手数量，起手通过独立发牌流程加入。
- 生成、编辑和删除卡片，自定义怪兽形态、召唤信息、状态与限制。
- 管理指示物、超量素材、卡组顺序、抽卡和基本分。
- 暂停、逐点前进、回溯、分支和跨会话存档。
- 双方独立视角与时点设置，编辑记录随回溯和存档恢复，支持保存录像。

MDPro3 扩展版：[duel-structure-editor-mdpro3](https://github.com/BAQjun/duel-structure-editor-mdpro3)。

## 源码结构

`upstream/gframe` 为客户端与编辑界面，`upstream/ocgcore` 为修改后的核心，`tools` 为构建和验证脚本，`tests` 为引擎及界面测试，`docs` 为使用说明与状态审计。

本仓库不包含玩家卡组、存档、录像、卡图、卡片数据库、编译器和生成的运行包。客户端及核心源码已包含在仓库中，不需要再初始化 core 子模块；卡片脚本需另外准备。

## 构建与验证

构建环境为 Windows，需要 Python 3.14。工具与依赖位置通过环境变量或 `build-config.local.json` 配置，无指定盘符要求。详见[路径配置与迁移](docs/路径配置与迁移.md)。

1. 创建 `work/downloads` 和 `outputs/决构编辑器-v0.1.17`。
2. 运行 `tools/bootstrap.py` 下载工具链，再运行 `tools/prepare_dependencies.py` 准备依赖。这两步需要联网。
3. 在 `upstream/script` 准备 YGOPro 卡片脚本，再执行 `tools/build.ps1`。
4. 自行准备运行所需的数据库与资源。运行目录可用 `DUEL_EDITOR_RUNTIME_ROOT` 指定，构建输出目录可用 `tools/build.ps1 -RuntimeDirectory` 指定。
5. `tools/test_engine.py` 验证实际核心，`tools/test_gui.py --sync` 与 `--perspective` 验证界面与双方交互。

v0.1.16 通过 625 项真实核心断言，另以 `tools/test_gui.py --summon-history` 验证召唤素材界面、整组撤回与前进。

## 来源与许可证

客户端来自 [Fluorohydride/ygopro](https://github.com/Fluorohydride/ygopro)，保留 GPL-2.0 许可证；核心来自 [Fluorohydride/ygopro-core](https://github.com/Fluorohydride/ygopro-core)，保留 `upstream/ocgcore/LICENSE` 中的 MIT 许可证。其他依赖遵循各自许可。

源码基线：客户端 `3734faa3adf8191c93c5b027c54f2f8b310d626a`，核心 `c4ccbab40702e4af6da4f4ad1d7a04c3ea80ad02`，开发时的脚本基线 `cfbb14cdbf7b2095db543f2620eb2e88b0fb9220`。另见 `docs/源码基线.json`。

最新修复：[自定义连接怪兽的表示限制](docs/0.2.4-连接表示限制.md)，已同步两个版本；本次真实核心回归通过 589 项断言。

最新修复：[自定义连接怪兽的实际连接关系](docs/0.2.6-自定义连接关系.md)，真实脚本验证女男爵可在自定义箭头指向下发动并破坏狮鹫，核心回归 609 项通过。

新增：[对应召唤与素材履历](docs/0.3.0-对应召唤与素材履历.md)，包含真实素材组、来源与去向、龙骑士素材计数及 I:P 素材赋予抗性。

路径兼容更新：v0.1.17 移除开发机盘符与目录依赖，配置和迁移说明见[路径配置与迁移](docs/路径配置与迁移.md)。
