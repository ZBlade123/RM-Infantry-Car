#include "servo_host_stubs.h"
#include "rm_config.h"
#include "rm_shooter.h"
#include <stdio.h>

static uint32_t tick, output;
static unsigned failures;
#define CHECK(name, condition) do { \
    if (!(condition)) { printf("FAIL %s\n", name); failures++; } \
    else { printf("ok %s\n", name); } \
} while (0)
uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, uint32_t state)
{
    (void)port; (void)pin;
    output = state;
}

int main(void)
{
    unsigned i, high = 0, rises = 0, prev = 0;
    Shooter_Init();
    CHECK("initially off", output == 0);
    Shooter_SetMode(2);
    for (i = 0; i < 4; i++) { tick += 10; Shooter_Task(); }
    CHECK("continuous duty limit", Shooter_GetDuty() == RM_FIRE_DUTY_PERCENT);
    Shooter_PwmTick();
    prev = output;
    for (i = 0; i < 1000; i++) {
        Shooter_PwmTick();
        high += output != 0;
        if (output && !prev) { rises++; }
        prev = output;
    }
    CHECK("100Hz period, not 10Hz", rises == RM_FIRE_PWM_HZ);
    CHECK("90 percent high slots", high == 900);
    Shooter_Stop();
    Shooter_PwmTick();
    CHECK("stop immediately off", output == 0 && Shooter_GetMode() == 0);
    Shooter_SetMode(1);
    tick += RM_FIRE_SHOT_MS;
    Shooter_Task();
    Shooter_PwmTick();
    CHECK("single timeout off", output == 0 && Shooter_GetMode() == 0);
    Shooter_SetMode(1);
    CHECK("single cooldown", Shooter_GetMode() == 0);
    tick += RM_FIRE_COOLDOWN_MS;
    Shooter_SetMode(2);
    tick += RM_FIRE_MAX_MS;
    Shooter_Task();
    Shooter_PwmTick();
    CHECK("continuous timeout off", output == 0 && Shooter_GetMode() == 0);
    printf("%s (%u failed)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
