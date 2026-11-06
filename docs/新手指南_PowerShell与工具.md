# 新手指南：PowerShell 是什么，tests / tools 怎么用

> 面向第一次用命令行的人。照着抄命令即可，不需要理解原理。

---

## 1. PowerShell 是什么

**PowerShell 就是 Windows 自带的"黑窗口"，用打字代替点鼠标。**

- 平时你双击 Keil 打开工程 → 那是点鼠标；
- 在 PowerShell 里输入 `.\test_firmware.ps1` 回车 → 那是打字。

两者做的事一样，只是命令行能一次说清"用哪个文件、加什么参数"。我们仓库里的 `.ps1`（PowerShell 脚本）和 `.py`（Python 脚本）都必须在这种窗口里运行，**双击是没用的**（双击只会用记事本打开它）。

### 怎么打开

任选一种：

| 方式 | 操作 |
|---|---|
| 开始菜单 | 按 `Win` 键 → 输入 `powershell` → 回车（会出现蓝色窗口） |
| 在文件夹里开 | 打开项目文件夹 → 点一下顶部**地址栏** → 输入 `powershell` → 回车（**这个最省事，直接就在对的目录里**） |
| 右键 | 在文件夹空白处按住 `Shift` 右键 → "在此处打开 PowerShell 窗口" |

---

## 2. 只需要记住 3 个命令

```powershell
cd <项目根目录>    # 进入某个目录（change directory）
dir                                          # 列出这个目录里有什么（也可以写 ls）
cls                                          # 清屏
```

窗口顶部会显示你当前在哪个目录，**所有命令都是"相对于当前目录"执行的**，所以先 `cd` 到对的目录再干活。

> 小技巧：输入路径时按 `Tab` 键会自动补全；按 `↑` 上方向键能翻出上次的命令；按 `Ctrl+C` 可以中断正在跑的东西；粘贴用**鼠标右键**（`Ctrl+V` 在这个窗口里不一定好用）。

---

## 3. 为什么命令里要带 `-ExecutionPolicy Bypass`

Windows 默认**禁止运行脚本**（防止乱下东西被执行）。所以运行 `.ps1` 时要临时放开这一次：

```powershell
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Port COM7
```

读作："启动 powershell → 这一次允许运行脚本（`Bypass`）→ 运行文件 `tools\pc_remote.ps1` → 传参数 `-Port COM7`"。

如果直接输入 `.\pc_remote.ps1` 会报：

```
无法加载文件 ...pc_remote.ps1，因为在此系统上禁止运行脚本。
```

**这不是脚本坏了**，就是上面那个开关没打开，改用完整写法即可。

---

## 4. tests/ 怎么用：在电脑上验算固件（不用板子）

`tests` 里是 4 个 `.c` 测试文件，配合 `tools\test_firmware.ps1` 一键跑。它们把固件里的算法（麦轮解算、舵机换算、发射时序、视觉解析）编译成电脑上的 exe 跑一遍，**检查逻辑对不对**——上车前就能发现"四轮同向"这类 bug。

### 步骤

1. 确认 gcc 能用（只需做一次）：

```powershell
gcc --version
```

看到 `gcc (TDM64 ...)` 之类就对了。如果提示"不是内部或外部命令"，先临时加路径：

```powershell
$env:Path = "D:\Dev-Cpp\TDM-GCC-64\bin;" + $env:Path
```

2. 跑全部测试：

```powershell
cd <项目根目录>
powershell -ExecutionPolicy Bypass -File tools\test_firmware.ps1
```

3. 看输出：

```
ok chassis forward ...
ok servo clamp ...
All firmware host tests passed.
```

看到最后一句 **All firmware host tests passed.** 就说明 4 组全过。
出现 `FAIL` 或 `Compile failed` 就把整屏红字发给我。

### 单跑一组（改完某个模块时用）

```powershell
cd <项目根目录>\firmware\Mecanum-Car
gcc -std=c99 -Wall -Wextra -Werror -DRM_HOST_TEST -I Core/Inc Core/Src/rm_chassis.c tests\chassis_follow_test.c -lm -o $env:TEMP\chassis.exe
& $env:TEMP\chassis.exe
```

把 `rm_chassis.c` / `chassis_follow_test.c` 换成别的模块和它的测试文件即可。

> 什么时候必须跑：改完 `rm_chassis.c`、`rm_servo.c`、`rm_shooter.c`、`rm_vision.c` 之后。**跑通了再上车**。

---

## 5. tools/ 怎么用：让电脑连上战车

前提：HC-05 已配对（配对码 `1234`），设备管理器里出现**两个** HC-05 串口（COM3、COM4 这种），用名字带 **传出 / Outgoing** 的那个。

### 第一步：看有哪些串口

```powershell
cd <项目根目录>
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -List
```

会列出 `COM7  蓝牙链接上的标准串行 (COM7)` 之类。**记下带"传出"的那个号。**

### 第二步：不确定哪个能通就自动试

```powershell
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Probe
```

它会挨个串口发一个 `p`，谁回 `RM-INFANTRY-CAR` 就是它。
（战车**没上电**时，列表里的 COM 是配对留下的"幽灵"，会报 `The port 'COM3' does not exist.`，正常。）

### 第三步：先发一条指令确认固件活着

```powershell
powershell -ExecutionPolicy Bypass -File tools\rm_send.ps1 -Port COM7 -Cmd "p"
```

回 `RM-INFANTRY-CAR 3.6` → 通了。再试点动（车**架空**）：

```powershell
powershell -ExecutionPolicy Bypass -File tools\rm_send.ps1 -Port COM7 -Cmd "m 0 200"
powershell -ExecutionPolicy Bypass -File tools\rm_send.ps1 -Port COM7 -Cmd "x"
```

### 第四步：演练键盘映射（不发数据）

```powershell
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Dry
```

窗口会打印"你按了 W → 发 v 300 0 0"。按 `Esc` 退出。

### 第五步：真开车

```powershell
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Port COM7
```

| 键 | 动作 | 键 | 动作 |
|---|---|---|---|
| `W` / `S` | 前进 / 后退 | 方向键 | 云台 Yaw / Pitch |
| `A` / `D` | 左移 / 右移 | 空格 | 三连发 |
| `Q` / `E` | 原地转 | `X` | **急停** |
| `Shift` | 加速 | `R` | 云台回中 |
| `G` | 状态回显 | `T` | 遥测 |
| `Esc` | 退出（自动急停） | | |

> 出任何异常：先按 `X`。

### 可选：Python 帧工具

日常文本指令用不上它，只有要发**十六进制帧**（`A5 5A ...`）时才用。本机没装系统 Python，用这个：

```powershell
python tools\rm_remote.py --selftest
```

---

## 6. 常见报错对照

| 报错 | 原因 | 解决 |
|---|---|---|
| `无法加载文件 ... 禁止运行脚本` | 没加 `-ExecutionPolicy Bypass` | 用完整写法（见 §3） |
| `不是内部或外部命令` | 命令名打错，或 gcc 没在 PATH | 检查拼写；`$env:Path = "D:\Dev-Cpp\TDM-GCC-64\bin;" + $env:Path` |
| `系统找不到指定的路径` | 当前目录不对 | `cd` 到 `RM步兵战车代码` 再跑 |
| `The port 'COM3' does not exist` | 板上电前配对残留的幽灵串口 | 给车上电，或改用 `-Probe` |
| `拒绝访问 COM7` | 串口正被别的软件占着（串口助手/另一个窗口） | 关掉其它串口软件，只留一个窗口 |
| 中文乱码 | 脚本没存成 UTF-8 with BOM | 重新保存该 `.ps1` 为 "UTF-8 带 BOM" |
| 打字没反应 | 焦点不在 PowerShell 窗口 | 点一下窗口再按 `Esc` 退出 |

---

## 7. 最短路径（背这一页就够）

```powershell
# A. 上车前验算固件
cd <项目根目录>
powershell -ExecutionPolicy Bypass -File tools\test_firmware.ps1

# B. 上车调试
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -List     # 找 COM
powershell -ExecutionPolicy Bypass -File tools\rm_send.ps1 -Port COM7 -Cmd "p"
powershell -ExecutionPolicy Bypass -File tools\pc_remote.ps1 -Port COM7
```
