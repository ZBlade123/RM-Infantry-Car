/**
  ******************************************************************************
  * @file    vision_test.c
  * @brief   视觉模块解析 + 自动瞄准步长的主机测试（不接板子也能跑）
  *          编译：见 README「不接板子先验证」一节
  ******************************************************************************
  */

#include <stdio.h>
#include <string.h>
#include "rm_vision.h"
#include "rm_config.h"

static int s_fail;
static int s_pass;

static void ok(int cond, const char *what)
{
    if (cond)
    {
        s_pass++;
        printf("ok   %s\n", what);
    }
    else
    {
        s_fail++;
        printf("FAIL %s\n", what);
    }
}

static void feed(const char *s)
{
    while (*s != '\0')
    {
        Vision_FeedChar(*s++);
    }
}

int main(void)
{
    int32_t ny;
    int32_t np;

    Vision_Reset();
    Vision_SetNow(1000u);

    /* 1. 相对偏差帧 */
    feed("a 40 -10\r\n");
    ok(Vision_GetMode() == RM_VISION_MODE_REL, "a 帧 -> 相对模式");
    ok(Vision_GetDx() == 40, "dx 解析 = 40");
    ok(Vision_GetDy() == -10, "dy 解析 = -10");
    ok(Vision_IsValid() == 1u, "刚收到帧 = 有效");
    ok(Vision_GetFrameCount() == 1u, "帧计数 = 1");

    /* 1b. 只有数字的行也当相对偏差（很多 K210 例程这么发） */
    feed("-25,60\r\n");
    ok((Vision_GetMode() == RM_VISION_MODE_REL) &&
       (Vision_GetDx() == -25) && (Vision_GetDy() == 60), "裸数字行 -> 相对偏差");

    /* 1c. 原文回显（协议不明时靠它排查） */
    ok(strstr(Vision_GetLastLine(), "-25") != NULL, "最近一帧原文可读");

    /* 2. 死区内不动 */
    feed("a 5 7\r\n");
    Vision_SetAim(1u);
    Vision_AimStep(900, 900, &ny, &np);
    ok((ny == 900) && (np == 900), "死区内不动作");

    /* 3. 相对步长与限幅 */
    feed("a 48 0\r\n");
    Vision_AimStep(900, 900, &ny, &np);
    ok(ny == 940, "dx=48 -> 扣死区后 40px，单步限幅到 +4 度");
    ok(np == 900, "dy=0 -> pitch 不动");

    feed("a 5000 0\r\n");
    Vision_AimStep(900, 900, &ny, &np);
    ok(ny == 900 + RM_VISION_MAX_STEP_DEG10, "大偏差被限幅到单步上限");

    /* 4. pitch 方向：目标在下方 -> 低头 */
    feed("a 0 48\r\n");
    Vision_AimStep(900, 900, &ny, &np);
    ok(np == 900 - RM_VISION_MAX_STEP_DEG10, "dy>0 -> pitch 减小（低头）");

    /* 5. 超时后无效 */
    Vision_SetNow(1000u + RM_VISION_TIMEOUT_MS);
    ok(Vision_IsValid() == 0u, "超时后判为丢目标");

    /* 6. 绝对角帧 */
    Vision_SetNow(2000u);
    feed("t 1500 1000\r\n");
    ok(Vision_GetMode() == RM_VISION_MODE_ABS, "t 帧 -> 绝对模式");
    Vision_AimStep(900, 900, &ny, &np);
    ok((ny == 1500) && (np == 1000), "绝对模式直接跟随目标角");

    /* 7. 丢目标 / 关瞄准 */
    feed("x\r\n");
    ok(Vision_GetMode() == RM_VISION_MODE_NONE, "x -> 丢目标");
    Vision_AimStep(900, 900, &ny, &np);
    ok((ny == 900) && (np == 900), "丢目标时保持不动");

    feed("t 1200 800\r\n");
    Vision_SetAim(0u);
    Vision_AimStep(900, 900, &ny, &np);
    ok((ny == 900) && (np == 900), "关瞄准后不跟");

    /* 8. 脏数据不影响状态 */
    Vision_SetAim(1u);
    feed("hello world\r\n");
    ok(Vision_GetMode() == RM_VISION_MODE_ABS, "不认识的行被丢弃");

    /* 9. 粘包/超长行不串帧 */
    feed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n");
    feed("t 1000 900\r\n");
    ok(Vision_GetMode() == RM_VISION_MODE_ABS, "超长行之后仍能正常解析");

    printf("\n%s (%d passed, %d failed)\n",
           (s_fail == 0) ? "ALL PASS" : "SOME FAILED", s_pass, s_fail);
    return (s_fail == 0) ? 0 : 1;
}