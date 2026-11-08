#include "status_led.h"
#include <stddef.h>

// durations in ms, alternating ON, OFF, ON, OFF ...; the last entry is an OFF gap; 0 ends the pattern
static const uint16_t P_DATA[]    = {40, 60, 0};
static const uint16_t P_WRITE[]   = {40, 80, 40, 140, 0};
static const uint16_t P_ERR_SD[]  = {150, 150, 150, 150, 150, 400, 0};
static const uint16_t P_ERR_BUS[] = {30, 50, 30, 50, 30, 50, 30, 50, 30, 150, 0};
static const uint16_t P_IDLE[]    = {400, 150, 0};

static const uint16_t *const PATTERN[LED_EV_COUNT] = {
    [LED_EV_DATA] = P_DATA, [LED_EV_WRITE] = P_WRITE, [LED_EV_ERR_SD] = P_ERR_SD,
    [LED_EV_ERR_BUS] = P_ERR_BUS, [LED_EV_IDLE] = P_IDLE,
};
// highest priority first
static const led_event_t PRIORITY[LED_EV_COUNT] = {LED_EV_ERR_BUS, LED_EV_ERR_SD, LED_EV_WRITE, LED_EV_DATA, LED_EV_IDLE};

static void (*g_set)(bool);
static uint32_t pending;
static const uint16_t *cur;
static int idx;
static uint32_t until;
static bool on;

static void set(bool v) { on = v; if (g_set) g_set(v); }

void status_led_init(void (*setter)(bool)) {
    g_set = setter;
    pending = 0;
    cur = NULL;
    set(false);
}

void status_led_event(led_event_t ev) { pending |= 1u << ev; }

void status_led_task(uint32_t now) {
    if (cur) {
        if ((int32_t)(now - until) < 0) return;                  // current step still running
        idx++;
        if (cur[idx] != 0) { set(!on); until = now + cur[idx]; return; }
        cur = NULL;                                              // pattern finished
        set(false);
    }
    for (int i = 0; i < LED_EV_COUNT; i++) {                     // start the next pending pattern
        led_event_t ev = PRIORITY[i];
        if (pending & (1u << ev)) {
            pending &= ~(1u << ev);
            cur = PATTERN[ev];
            idx = 0;
            set(true);
            until = now + cur[0];
            return;
        }
    }
}
