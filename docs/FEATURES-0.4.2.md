# TOTKDroid 0.4.2-beta 功能包说明

这份文档说明在官方 `v0.4.0-beta`（提交 `4882dbc`，仓库内为 `af9195c`）之上加入的功能：每项做什么、代码在哪、怎么用，以及下次如何把它们装回一个干净的基底上。

打包文件：`packs/totkdroid-0.4.2-features.zip`，内容如下：

| 文件 | 内容 |
|---|---|
| `README.md` | 本文档 |
| `features-0.4.2.patch` | 相对 `af9195c` 的完整补丁（`git apply` 用） |
| `FILES.txt` | 改动的文件清单，以及每个文件属于哪一项功能 |
| `files/` | 改动后的完整文件（补丁打不上时直接覆盖用） |

## 一、加入的功能（用户选定的第 1–9 项和第 15 项）

| 编号 | 功能 | 主要文件 | 提交 |
|---|---|---|---|
| 1 | 金手指（Atmosphère 数据金手指 + 代码金手指） | `runtime/cheats.c`、`cheats.h`、`cheat_vm.c`、`cheat_vm.h`、`docs/CHEATS.md`、`ui/mods_tab.mm` | `382360f` `7afcb06` `dbd736a` `c6003a2` `ea5861d` |
| 2 | 金手指名称的中文显示 | `ui/gb2312_hanzi.inc`、`ui/i18n.mm`、`ui/i18n_zh_hans.inc`、`ui/i18n_zh_hant.inc`、`ui/i18n.h` | `d6c812e` |
| 3 | 导入模组时复制它的 cheats 文件夹 | `android/.../TOTKRecompAndroidActivity.java` | `d992b03` |
| 4 | ExeFS 补丁（IPS / IPS32 / .pchtxt） | `runtime/exefs_patch.c`、`exefs_patch.h`、`tests/exefs/` | `dc6c181` `be20d28` |
| 5 | 只有 ExeFS 补丁的文件夹也算模组 | `runtime/mods.c`、`mods.h` | `50072dd` |
| 6 | 资源尺寸表（RESTBL）自动合并 | `runtime/services/restbl.c`、`restbl.h`、`services/fs.c`、`tests/restbl/` | `7223055` |
| 7 | 游戏读取 romfs 文件的日志 | `runtime/services/fs.c` | `013f1a2` |
| 8 | 性能覆盖层改中文，内存行显示“已用 / 总量 / 剩余” | `runtime/ui/perf_overlay.mm`、`ui/i18n*`、`nvn/vulkan/device.c`、`nvn/vulkan/exec.c` | `979d9e8` `bffd358` |
| 9 | 覆盖层始终显示帧率 | `runtime/settings.c`、`ui/perf_overlay.mm` | `27eee06` |
| 15 | 只保留经典触控布局（去掉动作图标布局） | `runtime/settings.c`、`ui/controls_ui.mm` | `d6c812e` `ffb9e29` |

共用的挂接点（第 1、4、6 项要用到）：`runtime/main.c`（`cheats_boot`）、`runtime/native.c`（`cheats_native_ready`）、`runtime/nvn/objects.c`（`cheats_frame`）、`runtime/kernel.c` / `kernel.h`（内存区域表 `kernel_region_of`）、`runtime/rt.h`、`runtime/settings.h`（`cheats_on`、覆盖层项）、`NOTICE.md`（MIT 来源声明）。

另外把版本改为 `versionCode 13`、`versionName "0.4.2-beta"`（`android/totkrecompandroid/app/build.gradle.kts`）。

### 没有加入的内容（用户未选）

- Win-FG 帧生成、LSFG 设置迁移（第 10–14 项）。
- 内存节省开关（已移除）。
- 1x–4x 阴影贴图（已移除，阴影选项保持上游的 Game / Lower / Lowest）。
- 8x 内部分辨率（已降为 2x，不在包内）。
- CI 与构建相关提交、PC 版实验提交、`24bfca7`（模组文件夹删除并忽略，已在基底中）。
- 覆盖层的 GPU 负载 / 频率行（`gpu_load_clock`，来自上游之后的主线，未选）。
- 历史清理（`5b67629`、`044dc43`），由用户决定（第 17 项）。

## 二、用法

### 1. 金手指

1. 找到游戏版本对应的文件。文件名是主程序 build id 前 8 字节的大写十六进制。TOTK 1.4.3 的 build id 是 `277178B7DBA1B6D4`，所以文件名是 **`277178B7DBA1B6D4.txt`**。其他名字的文件会被忽略。
2. 把它放进某个模组的 `cheats` 文件夹：
   ```
   mods/
     我的金手指/
       cheats/
         277178B7DBA1B6D4.txt
   ```
   也可以是 Switch SD 卡或模拟器的目录结构：`atmosphere/contents/0100F2C0115B6000/cheats`、`contents/0100F2C0115B6000/cheats`、`0100F2C0115B6000/cheats`。
3. 在 模组页 > 金手指 中打开需要的金手指。数据金手指在游戏运行时立即生效。

格式是标准 Atmosphère：`[名字]` 开始一个金手指，`{名字}` 是常开的主金手指，下面跟 8 位十六进制的指令字。没有指令字的名字显示为标题。`TID=` 那一行不显示。

- 数据金手指：每帧（游戏第 60 帧起）在呈现时运行。
- 代码金手指：在游戏原生代码中运行（默认 `LARECOMP_NATIVE` 开启）。关闭或松开按键时会恢复原指令。**尚未在真机上验证。**
- 未运行的情况（见 `docs/CHEATS.md`）：指令写入之外还有其他操作码、写入未整 4/8 字节的指令、写入移植器自己改过的指令、跳转到游戏代码中未被金手指写入的零字。
- 重编译版（`LARECOMP_NATIVE=0`）中不运行代码金手指，数据金手指未验证。

金手指开关保存在设置文件（`totkrecompandroid.ini`）的 `cheats_on` 一行里，键是 `<模组文件夹名>/<金手指名>` 的 FNV-1a 32 位哈希。这一行必须单独成行（`runtime/settings_cheats_line.h`）：0.4.2 的 ae200ac 曾把它接在 `touch_layout` 后面，开关会在每次开机时丢失。修复后读取时会自动拆出旧文件里接在一起的开关。**重命名模组文件夹会把它的金手指全部关掉。**

### 2. 金手指名称的中文显示

金手指名称中的汉字使用 GB2312 汉字字形（`gb2312_hanzi.inc`，只在简体中文界面加载）。覆盖层的中文也依赖这些字形。**注意**：简体字体 NotoSansCJK 无法使用，所有运行中都回退为 NotoSerifCJK 的字形，这是已知现象。

### 3. 导入模组时复制 cheats

在 APP 中导入模组时，模组的 `cheats` 文件夹也会被复制（以前只复制 romfs 和说明文件）。

### 4. ExeFS 补丁

放在模组的 `exefs` 文件夹，或 `atmosphere/exefs_patches/<名字>/` 中：

- `<build id>.ips`：IPS（PATCH 格式，3 字节偏移）或 IPS32（EEOF/IPS32 格式，4 字节偏移）。两种都用 `.ips` 扩展名，由文件头区分。偏移按模拟器的方式计算：要跳过 NSO 头（0x100 字节）。
- `.pchtxt`（IPSwitch 格式），用 `@nsobid` 行匹配 build id。偏移加上 `@flag` 的 offset_shift 后再去掉 NSO 头。

补丁在金手指之前写入主程序代码，并早于游戏线程启动。格式的测试：`bash tests/exefs/run.sh`。

### 5. 只有 ExeFS 补丁的模组

模组文件夹里只有 `exefs/` 或 `atmosphere/exefs_patches/` 也会被列为模组。

### 6. 资源尺寸表（RESTBL）

这一项自动运行，不需要设置。游戏按文件表项分配读取缓冲区，所以模组的文件比原版大时，表项必须增大。`fs.c` 在启动时（`restbl_prepare`）：

- 合并模组自带的 RESTBL（如果有）；
- 增大被模组替换文件的表项，并为新增文件估算表项；
- 把结果写到 `cache/restbl/ResourceSizeTable.zs`（`cache/` 不提交）。

测试：`bash tests/restbl/run.sh`（RESTBL）、`bash tests/settings/run.sh`（设置文件里的 `cheats_on` 行）。

### 7. romfs 读取日志

游戏每开始读取一个 romfs 文件，都会写一行 `[fs] read <路径>`。默认开启，便于崩溃日志看出最后读的文件。设置环境变量 `LARECOMP_LOG_FILES=0` 可关闭。

### 8. 性能覆盖层

- 中文界面。内存行是 `已用 / 总量 / 剩余`（应用占用、手机总内存、可用内存）。
- 帧数（FPS）、帧时间（ms）和 RAM 有图表，覆盖层中的文字由 `tr_zh` 包装为中文。

### 9. 帧数始终显示

覆盖层的 FPS 不能关闭（`overlay_items` 总是 `| OVERLAY_FPS`）。其他项仍可在设置中选择。

### 15. 只有经典触控布局

`touch_scheme` 固定为 0。设置中的旧值会被忽略，动作图标布局已不使用。弓和投掷的瞄准滑条跟随 `touch_scheme`（`ffb9e29`）。

## 三、下次把它们装回干净基底的步骤

下面的步骤给下一位 AI 或开发者用，假设基底是官方标签 `af9195c`。

1. 在仓库中建一个干净分支或工作树，基于 `af9195c`（不要基于 `27be190`，那是陈旧的本地 HEAD）。
2. 应用补丁（不要手改冲突块）：
   ```
   git apply --check features-0.4.2.patch && git apply features-0.4.2.patch
   ```
   如果失败，用 `files/` 中的完整文件覆盖对应路径，并核对 `FILES.txt`。
3. 检查 `android/totkrecompandroid/gradlew` 的权限必须是 100755（`git ls-tree` 显示 `100755`）。从某些提交检出文件时权限会变成 100644，CI 的 `gradlew` 会失败（`79dcb4d`、`68365e0` 就因此失败，修复为 `chmod +x` 后提交）。
4. 主机测试：`bash tests/cheat_vm/run.sh`、`bash tests/exefs/run.sh`、`bash tests/restbl/run.sh`、`bash tests/settings/run.sh`。`.mm` 文件和 `perf_overlay.mm` 不能在主机上编译，以 CI 的 `gradlew assembleDebug` 为准。
5. 不要再加入第一节“没有加入的内容”中的项，除非用户明确要求。

### 已知风险

- 覆盖层的 GPU 负载行没有加入。若要加，它的函数 `gpu_load_clock` 在上游之后的主线里，需要单独选择。
- 代码金手指和重编译版的金手指未在真机上验证。
- 0.4.2 构建在用户的真机上尚未测试（截至本文档，用户只测过官方标签构建 `af9195c`）。
- 上游 `51e5275`（文字缺失与颜色变暗，上游 issue #91）不在官方标签中，本包也不修复它。如果换到上游主线作为基底，需要先确认这一问题。
- `cache/` 和 `mod文件夹/` 被 `.gitignore` 忽略，不能提交。
