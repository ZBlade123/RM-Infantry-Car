#include "servo_host_stubs.h"
#include "rm_config.h"
#include "rm_servo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint32_t tick;
static unsigned sent, failures;
static unsigned pulse[3], duration[3];
static uint32_t gpio_mode;

#define CHECK(name, condition) do { \
    if (!(condition)) { printf("FAIL %s\n", name); failures++; } \
    else { printf("ok %s\n", name); } \
} while (0)

uint32_t HAL_GetTick(void) { return tick; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    (void)port;
    gpio_mode = gpio->Mode;
}
int HAL_UART_Init(UART_HandleTypeDef *uart)
{
    CHECK("bus TX only", uart->Init.Mode == UART_MODE_TX);
    CHECK("bus baud", uart->Init.BaudRate == RM_GIMBAL_BUS_BAUD);
    return 0;
}
int HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data, uint16_t len, uint32_t timeout)
{
    char frame[32];
    unsigned id, p, ms;
    (void)uart;
    (void)timeout;
    if (len >= sizeof(frame)) { abort(); }
    memcpy(frame, data, len);
    frame[len] = '\0';
    if (sscanf(frame, "#%3uP%4uT%4u!", &id, &p, &ms) == 3 && id < 3) {
        pulse[id] = p;
        duration[id] = ms;
    }
    sent++;
    return 0;
}
static void step(void) { tick += 10; Servo_Task(); }

int main(void)
{
    unsigned before, i;
    Servo_Init();
    CHECK("open drain", gpio_mode == GPIO_MODE_AF_OD);
    CHECK("no artificial yaw end-stop", Yaw_GetDeg10() == 900);
    CHECK("no artificial pitch end-stop", Servo_GetDeg10() == 900);
    Gimbal_SetDeg10(1800, 1600);
    CHECK("homing ignores commands", Yaw_GetTargetDeg10() == 900 && Servo_GetTargetDeg10() == 900);
    step();
    CHECK("rate limit first 10ms", sent == 0);
    step();
    CHECK("center frames", sent == 2 && pulse[1] == 1500 && pulse[2] == 1500);
    CHECK("slow initial center", duration[1] == RM_GIMBAL_HOME_MS && duration[2] == RM_GIMBAL_HOME_MS);
    for (i = 0; i < 148; i++) { step(); }
    CHECK("homing complete", Servo_IsHoming() == 0);
    CHECK("stationary frames suppressed", sent == 2);
    Gimbal_SetDeg10(1800, 1600);
    step();
    CHECK("yaw slew not target jump", Yaw_GetDeg10() == 930);
    CHECK("pitch slew not target jump", Servo_GetDeg10() == 930);
    CHECK("no early transmit", sent == 2);
    step();
    CHECK("sent intermediate yaw", pulse[1] == 1434);
    CHECK("sent intermediate pitch", pulse[2] == 1566);
    CHECK("pulse telemetry updated", Yaw_GetPulseUs() == pulse[1] && Servo_GetPulseUs() == pulse[2]);
    CHECK("normal interpolation", duration[1] == RM_GIMBAL_BUS_TIME_MS);
    for (i = 0; i < 40; i++) { step(); }
    CHECK("targets reached", Yaw_GetDeg10() == 1800 && Servo_GetDeg10() == 1600);
    before = sent;
    for (i = 0; i < 10; i++) { step(); }
    CHECK("unchanged output suppressed", sent == before);
    Gimbal_SetDeg10(-100, 2000);
    CHECK("limits clamp", Yaw_GetTargetDeg10() == 0 && Servo_GetTargetDeg10() == 1600);
    step();
    CHECK("reverse slew", Yaw_GetDeg10() == 1770);
    CHECK("ADC divider safe at full 2S", RM_VBAT_FULL_MV * RM_VBAT_R_BOTTOM <
          RM_VBAT_REF_MV * (RM_VBAT_R_TOP + RM_VBAT_R_BOTTOM));
    printf("%s (%u failed)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
