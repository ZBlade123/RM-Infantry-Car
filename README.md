# RM 步兵小车 v3.8

按 2026-11-05 采购价格表与 `降压PCB1_2026-11-03` Gerber 复核：**2S XT60 LiPo、60mm 麦轮、MC310 霍尔编码器电机 ×4、总线舵机二维云台**。固件仍是工程原有 F103C8T6 + 两颗 TB6612 的板级映射；新 Gerber 未与该映射验证兼容，不能直接照接。

这是软件与装配方案，不是实车验收记录。电机具体型号、6P 针序、舵机电压/协议/角度、MOS 电流能力仍需核实；未接实车验证，不能据此直接满载上电。原厂 PWM/总线例程目录保留作参考，不是整车烧录工程。

## 文档入口

- [接线总表](docs/接线总表.md)：当前唯一接线标准，含电源树、逐路引脚、改板、6P 核线与上电验收。
- [配件与新 PCB 核对](docs/配件与PCB核对.md)：实购数量/金额、新 Gerber 可确认的网络及未确认项。
- [采购清单](docs/采购清单.md)：实购物料、缺件与规格待核，不将采购记录等同于到货验收。
- [电机与编码器](docs/电机与编码器.md)：开环边界与参数条件。
- [云台套件说明](docs/云台套件说明.md)：总线兼容性、供电与小步标定。
- [发射机构](docs/发射机构.md)：低端 MOS 接法和保护器件核对。
- [烧写固件](docs/烧写固件.md)、[调试指令](docs/调试指令.md)、[电脑操控与图传](docs/电脑操控与图传.md)。
- 手机遥控 App：[tools/android-remote](tools/android-remote/README.md)（按住即发、松手停车，APK 16KB）。
- [新手指南：PowerShell 与工具](docs/新手指南_PowerShell与工具.md)：第一次用命令行的人先看这个（怎么跑 tests、怎么连车）。
- [修订与验收记录](docs/修订与验收记录.md)：已运行的检查与尚未完成的实车验收。

## 当前配置与限制

| 项目 | 配置 |
|---|---|
| 主控 | STM32F103C8T6，72MHz，64KB Flash / 20KB RAM |
| 底盘 | 60mm 麦轮；轮序 0=FL、1=RL、2=RR、3=FR；TIM2 PA0~PA3 PWM |
| 电机 | 310 带编码器，但 PCB 未接入 A/B，**仍是开环占空比控制** |
| 云台 | `RM_GIMBAL_MODE=2`，USART3 PB10 开漏，只发；RR IN1 改 PA15，IN2 仍 PB11 |
| 总线协议 | 所附松甲例程的 115200 ASCII `#idPpwmTtime!`，不是通用磁编码舵机协议 |
| 遥控 | HC-05，USART1 PA9/PA10，9600 8N1 |
| 发射 | PB14，3.3V 高有效外置 MOS，默认不支持反转 |
| 视觉/图传键鼠 | 关闭；PB11 为电机方向，PA2/PA3 为 PWM，不能按旧方案接视觉/USART2 |
| 电源 | 单块 2S，满电 8.4V；两块轮换，不串并；H2 丝印 12V 不代表有 12V 输出 |
| 电压检测 | `RM_ENABLE_VBAT=0`；BB 响只报警。可选 ADC 改为 33k/10k，不再用危险的等比分压 |

MP1584 #2 调 **5V 给 H1**，不是 3.3V；#1 能否供两轴云台必须按舵机手册和带载试验判断，不足就换 BEC。15A 保险与 SS54 也需按负载校核，不能默认保护足够。**65% PWM 限幅不是限流保护**。

## 编译与烧录

打开 `firmware/Mecanum-Car/MDK-ARM/Mecanum-Car.uvprojx`，F7 编译后检查错误与警告，再用 ST-Link SWD 烧录。烧录时断开电机、舵机和波箱，外部供电时不并接下载器电源输出。完整方法见烧写文档。

PowerShell 本机无界面构建示例（Keil 路径以实际安装为准）：

```powershell
$project = (Resolve-Path 'firmware/Mecanum-Car/MDK-ARM/Mecanum-Car.uvprojx').Path
$buildLog = Join-Path (Split-Path $project) 'build_log.txt'
Start-Process 'D:\keil5\Core\UV4\UV4.exe' -WindowStyle Hidden -ArgumentList @('-b', ('"' + $project + '"'), '-j0', '-o', ('"' + $buildLog + '"')) -Wait
Get-Content $buildLog
```

主机回归测试（仓库根目录运行，需 GCC）：

```powershell
powershell -ExecutionPolicy Bypass -File tools/test_firmware.ps1
```

构建或主机测试通过不证明供电、线序、驱动温升或实车失联测试通过。

## 首次实操

先看 [配件与 PCB 核对](docs/配件与PCB核对.md) 确认手上的板属于哪一版。按对应文档逐阶段进行：断电核线 → 降压空载设定 → 仅逻辑供电/烧录/PONG → 架空逐轮点动 → 云台单颗空载与两轴带载 → MOS 假负载 → 失联、总断电、低压报警验收。任何一阶段不合格就停止。新 Gerber 板未完成原理图/额定/引脚核对前，不接电池和负载。

常用指令（参数是逻辑值，不是实测速度/角度）：

| 指令 | 用途 |
|---|---|
| `p` / `g` / `cal` | 通信、云台软件状态、底盘标定值 |
| `m 0 200` 后 `x` | 左前轮短时点动后急停；其余轮号 1~3 |
| `flip 0` | 临时反向；永久保存须抄入 `RM_MOTOR_FWD_SIGN` 重编译 |
| `v 200 0 0` / `v 0 200 0` / `v 0 0 200` | 前进/左移/左转，架空和低速开始 |
| `y 950` / `s 950` | 小步调整 Yaw/Pitch，单位 0.1°；实际行程须标定 |
| `id 1` / `id 2` | 仅兼容协议、线上一颗空载舵机时改 ID |
| `f 0/1/2/3` | 停/定时单发/连发/定时三连发，先完成 MOS 验收 |
| `x` | 底盘与发射急停，不代替物理断电 |

上电运动锁定 1.2s，云台回中阶段 1.5s 忽略角度命令。停止控制/心跳 500ms 会触发失联停车；测试时保留整车供电，不能用拔电池代替。云台读数没有位置反馈，PWM 模式上电速度不能由软件保证；总线首次中心指令用 1500ms 插值，随后按软件斜坡发变化指令，每 20ms 最多发送一对帧。

## 本次代码修订

- 总线输出改为与套件源码一致的开漏，需匹配上拉/电平接口。
- 总线发送斜坡后的中间值，修复直接跳到目标角、遥测脉宽不更新的问题。
- 上电不再人为先扫下限；总线初次回中采用较长插值。实际位置未知，不宣称绝对防撞。
- 可选电池 ADC 上臂改 33kΩ、下臂 10kΩ；8.4V 时约 1.95V，旧 10k/10k 会输入 4.2V，禁止使用。
- 发射软件 PWM 按 `RM_FIRE_PWM_HZ` 计算周期，修复标注 100Hz 而实际为 10Hz 的问题；1ms 时隙下 100Hz 占空分辨率约 10%。

参数集中在 `firmware/Mecanum-Car/Core/Inc/rm_config.h`。修改 CubeMX `.ioc` 后重新生成可能覆盖手工配置，需要逐项检查，不能将原始 `.ioc` 当作当前完整板级配置。

## 上传到 GitHub

仓库：<https://github.com/ZBlade123/RM-Infantry-Car>（public，main 分支）

本机 `C:\Windows\System32\drivers\etc\hosts` 把 github.com 等域名指到 127.0.0.1，且当前不是管理员改不了，所以推送要先起本地隧道：`tools\gh_tunnel.py` 在 127.0.0.1:443 上转发到 GitHub 真实 IP，git 正常 `push` 即可。

```powershell
cd <项目根目录>
# 一条命令：自动起隧道 -> add -> commit -> push
powershell -ExecutionPolicy Bypass -File .\tools\gh_push.ps1 -Message "改了云台斜坡"
# 首次或远端地址丢了时加 -Init；不想交互输入可加 -Token <你的 PAT>
```

- 隧道只在推送时需要在跑，进程随用随起；`_tunnel.log/_tunnel.err` 已加入 `.gitignore`。
- 不在仓库里的内容：`_github_upload/`（旧的一次性暂存副本）、`tools/手机App/`（39MB APK，放 Release 更合适）、`_gimbal_src/**/*.exe`、Keil 的 `Objects/Listings` 与 `*.o/*.d/*.crf/*.axf` 等中间产物。
- 仓库里的 `gimbal_kit/` 是厂商套件源码副本，只作参考，不是本工程编译对象。
