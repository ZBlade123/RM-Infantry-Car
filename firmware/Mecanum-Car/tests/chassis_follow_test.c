/**
  * @file    chassis_follow_test.c
  * @brief   主机自检：底盘云台系驱动（follow）+ 摇杆死区 + 麦轮逆解方向
  *
  * 不需要板子，直接 gcc 编译 rm_chassis.c 跑：
  *   gcc -std=c99 -Wall -Wextra -I Core/Inc -I tests/host_stubs ^
  *       Core/Src/rm_chassis.c tests/chassis_follow_test.c -lm -o tests/chassis.exe
  *
  * 轮序：0=FL 1=RL 2=RR 3=FR；逆解 k=(160+160)/2=160，
  * 这里只用纯平移指令（wz=0），所以 k 不参与。
  */

#include <stdio.h>

#include "rm_chassis.h"
#include "rm_config.h"

/* ---- 电机层打桩：只记录下发的逻辑速度 ---- */
static int16_t g_w[4];

void Motor_SetTarget(uint8_t idx, int16_t logical)
{
    if (idx < 4u) { g_w[idx] = logical; }
}
uint8_t Motor_IsValidIdx(uint8_t idx) { return (idx < 4u) ? 1u : 0u; }
void    Motor_Init(void) { }
void    Motor_Task(void) { }
void    Motor_StopAll(void) { }
void    Motor_EmergencyStop(void) { }
int16_t Motor_GetTarget(uint8_t idx) { return g_w[idx & 3u]; }
int16_t Motor_GetOutput(uint8_t idx) { return g_w[idx & 3u]; }
int8_t  Motor_GetSign(uint8_t idx) { (void)idx; return 1; }
int16_t Motor_GetTrim(uint8_t idx) { (void)idx; return 0; }
void    Motor_FlipSign(uint8_t idx) { (void)idx; }
void    Motor_SetTrim(uint8_t idx, int16_t trim) { (void)idx; (void)trim; }

static int s_fail;

static void check(const char *name, int cond)
{
    if (cond == 0)
    {
        printf("FAIL %s\n", name);
        s_fail++;
    }
    else
    {
        printf("ok   %s\n", name);
    }
}

/* 斜坡每 10ms 只走 RM_CHASSIS_ACCEL_STEP，多打几拍让它到位 */
static void settle(void)
{
    int i;

    for (i = 0; i < 20; i++)
    {
        Chassis_Task();
    }
}

int main(void)
{
    /* 1) 车体系（默认）：前进四轮同向 */
    Chassis_Init();
    Chassis_SetVelocity(400, 0, 0);
    settle();
    check("车体系统前进 FL=400", g_w[0] == 400);
    check("车体系统前进 FR=400", g_w[3] == 400);
    check("车体系统前进 RL=400", g_w[1] == 400);
    check("车体系统前进 RR=400", g_w[2] == 400);

    /* 2) 死区：小于 RM_CHASSIS_DEADZONE 的指令当 0 */
    Chassis_Init();
    Chassis_SetVelocity(RM_CHASSIS_DEADZONE, 0, 0);
    settle();
    check("死区内指令不出力", (g_w[0] == 0) && (g_w[1] == 0) && (g_w[2] == 0) && (g_w[3] == 0));

    /* 3) 云台系，云台在中位：等价车体系 */
    Chassis_Init();
    Chassis_SetFollow(1u);
    Chassis_SetGimbalYawDeg10((int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10));
    Chassis_SetVelocity(400, 0, 0);
    settle();
    check("中位 follow 前进 FL=400", g_w[0] == 400);
    check("中位 follow 前进 FR=400", g_w[3] == 400);

    /* 4) 云台转到 +90°：前进变成"往左"，所以 FL/FR=-400、RL/RR=+400 */
    Chassis_Init();
    Chassis_SetFollow(1u);
    Chassis_SetGimbalYawDeg10((int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10 + 900));
    Chassis_SetVelocity(400, 0, 0);
    settle();
    check("云台+90 前进 -> 左移 FL=-400", g_w[0] == -400);
    check("云台+90 前进 -> 左移 FR=-400", g_w[3] == -400);
    check("云台+90 前进 -> 左移 RL=+400", g_w[1] == 400);
    check("云台+90 前进 -> 左移 RR=+400", g_w[2] == 400);

    /* 5) 云台转到 -90°：前进变成"往右"，方向反过来 */
    Chassis_Init();
    Chassis_SetFollow(1u);
    Chassis_SetGimbalYawDeg10((int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10 - 900));
    Chassis_SetVelocity(400, 0, 0);
    settle();
    check("云台-90 前进 -> 右移 FL=+400", g_w[0] == 400);
    check("云台-90 前进 -> 右移 RR=-400", g_w[2] == -400);

    /* 6) 关掉 follow 立刻回车体系 */
    Chassis_SetFollow(0u);
    Chassis_SetGimbalYawDeg10((int16_t)(RM_GIMBAL_YAW_CENTER_DEG * 10 + 900));
    settle();
    check("follow 关掉后回车体系 FL=400", g_w[0] == 400);

    /* 7) 指令斜坡：一次 Task 只走一个步长，不会瞬间冲到目标 */
    Chassis_Init();
    Chassis_SetVelocity(400, 0, 0);
    Chassis_Task();
    check("第一拍只走一个步长", g_w[0] == (int16_t)RM_CHASSIS_ACCEL_STEP);

    /* 8) 归一化：指令超限时按比例压到 RM_MOTOR_LIMIT 以内 */
    Chassis_Init();
    Chassis_SetVelocity(1000, 0, 1000);
    settle();
    check("超限被压到电机限幅内", (g_w[0] <= (int16_t)RM_MOTOR_LIMIT) && (g_w[0] >= -(int16_t)RM_MOTOR_LIMIT));
    check("超限后四轮仍有旋转分量", (g_w[0] != g_w[2]));

    if (s_fail == 0)
    {
        printf("ALL PASS\n");
        return 0;
    }
    printf("%d FAILED\n", s_fail);
    return 1;
}