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

Windows 下将仓库克隆到 D:，准备 Python 3.14，并调整工具脚本中的本机 Python 路径。

1. 创建 `work/downloads` 和 `outputs/决构编辑器-v0.1.15`。
2. 运行 `tools/bootstrap.py` 下载工具链，再运行 `tools/prepare_dependencies.py` 准备依赖。这两步需要联网。
3. 在 `upstream/script` 准备 YGOPro 卡片脚本，再执行 `tools/build.ps1`。
4. 自行准备运行所需的数据库与资源。运行目录可用 `DUEL_EDITOR_RUNTIME_ROOT` 指定，构建输出目录可用 `tools/build.ps1 -RuntimeDirectory` 指定。
5. `tools/test_engine.py` 验证实际核心，`tools/test_gui.py --sync` 与 `--perspective` 验证界面与双方交互。

工具保留开发环境的 D: 路径与版本，迁移环境时需相应调整。v0.1.15 既有开发验证通过 580 项真实引擎断言；此次公开上传清理了注释、整理了源码，没有重新执行整套图形测试。

## 来源与许可证

客户端来自 [Fluorohydride/ygopro](https://github.com/Fluorohydride/ygopro)，保留 GPL-2.0 许可证；核心来自 [Fluorohydride/ygopro-core](https://github.com/Fluorohydride/ygopro-core)，保留 `upstream/ocgcore/LICENSE` 中的 MIT 许可证。其他依赖遵循各自许可。

源码基线：客户端 `3734faa3adf8191c93c5b027c54f2f8b310d626a`，核心 `c4ccbab40702e4af6da4f4ad1d7a04c3ea80ad02`，开发时的脚本基线 `cfbb14cdbf7b2095db543f2620eb2e88b0fb9220`。另见 `docs/源码基线.json`。
