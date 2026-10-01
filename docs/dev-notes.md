# TuyaOpenClaw 开发备忘（Dev Notes）

> 本文件是本仓库自有文档（非上游内容），随 feature/clock-ai 分支维护。

## 1. Windows 构建链（本机已踩坑并固化）

**入口**：仓库根目录 `.\build_win.ps1`（支持 `build` / `flash` / `monitor` / `clean`）。

为什么需要它（两个本机事实）：

1. **ninja 长命令限制**：SDK 锁定的 ninja `1.11.1.4`（`.venv` 内）在 Windows 上无法执行超过
   32767 字符的命令；T5AI AP 侧由长绝对路径拼出的编译命令约 40K 字符，直接 `CreateProcess`
   失败。而下载更新的 ninja（≥1.12，可自动转 response file）会被本机 Application Control
   策略按文件内容拦截，无法使用。
2. **解决方案**：项目物理目录在 `C:\T5`（短路径），原路径
   `C:\Users\heish\Downloads\5_Scripts\TuyaOpen` 保留为 junction 指向 `C:\T5`，旧路径引用
   全部照常可用。短路径下最长编译命令约 13K，SDK 自带的 ninja 即可胜任。
3. **tos.py 跨盘 `cd` 补丁**：tos.py 的子进程用 `cd <path> && ...`（无 `/d`），跨盘符失效；
   `build_win.ps1` 每次构建前对 `TuyaOpen/tools/cli_command/util.py` 幂等打补丁
   （`cd` → `cd /d`，标记 `TuyaOpenClaw win32 fix`）。子模块重新 checkout 后会被脚本自动重打。

**注意**：`TuyaOpen` 是 git submodule（上游 v1.8.0-14-g4e6d0e7e，不随本仓库提交修改）。

## 2. 时钟 AI 模块

- 开关：Kconfig 符号 `ENABLE_APP_CLOCK_AI`（默认 n；config 文件与 using.config 中写作
  `CONFIG_ENABLE_APP_CLOCK_AI=y`，生成的 `tuya_kconfig.h` 剥离 `CONFIG_` 前缀，C 代码用
  `#if defined(ENABLE_APP_CLOCK_AI)`）；预设配置 `config/CLOCK_AI.config`，复制为
  `app_default.config` 后生效。
- 代码：`src/app_clock.c` / `src/app_clock.h`。1s 定时器 → `tal_time_get_local_time_custom()`
  取本地时间（含时区/夏令时）→ `[clock]` 日志 + （AI 显示开启时）经
  `ai_ui_disp_msg(AI_UI_DISP_NOTIFICATION, ...)` 在状态栏显示 HH:MM:SS。
- 显示约束：屏幕由 SDK `ai_ui`（wechat 变体）全权持有，应用层不直接抢屏；全屏时钟页后续可
  基于 `ai_ui_page` 扩展，当前为合规最小骨架。
- 验证：构建成功（`dist\TuyaOpenClaw_1.0.0\TuyaOpenClaw_QIO_1.0.0.bin`），`libtuyaapp.a`
  含 `app_clock_init` 符号。

## 3. Git 架构

- `origin` = `https://github.com/heishanmao/DuckyClaw.git`（fork，账号 heishanmao）
- `upstream` = `https://github.com/tuya/TuyaOpenClaw.git`（官方）
- 日常开发在 `feature/clock-ai`；`master` 与 fork 同步到上游最新（dfecbe3）。
- 同步上游：`git fetch upstream && git rebase upstream/master`（feature 分支）。
- `secrets.h`（UUID/AUTHKEY）、`app_default.config` 均被 .gitignore，不入库。

## 4. 授权（涂鸦平台）

- `include/tuya_app_config_secrets.h` 已填 `tuya-uuid.xlsx` 第 1 对
  （PID 仍为 `baphqfvzfmpumn5w`，需到涂鸦 IoT 平台核对哪对 UUID/AUTHKEY 绑定该 PID）。
- 板子若使用出厂烧录授权（`tuya_authorize_read()` 成功），secrets.h 仅作回退，不影响。
- CLI（`cfg_show`/`cfg_set_auth`）从 COM9/COM10 各波特率均未进入，待排查；串口日志口
  COM10 @ 460800。
