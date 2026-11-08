#pragma once
#include <stdint.h>
#include <stdbool.h>

// Status LED patterns (non-blocking). Events only request a pattern; patterns never interrupt each other and the
// highest-priority pending one is played next. Repeated requests of the same kind while pending are merged.
//   LED_EV_DATA     one short flash            a sensor reading was decoded
//   LED_EV_WRITE    two short flashes          a record was written to the SD card
//   LED_EV_ERR_SD   three slow flashes         SD card problem (repeated while the problem lasts)
//   LED_EV_ERR_BUS  five rapid flashes         sniffer / bus problem (CRC error, DMA overrun, PIO stall, dropped record)
//   LED_EV_IDLE     one long flash            alive, but no sensor traffic seen for a while
typedef enum { LED_EV_DATA, LED_EV_WRITE, LED_EV_ERR_SD, LED_EV_ERR_BUS, LED_EV_IDLE, LED_EV_COUNT } led_event_t;

void status_led_init(void (*set)(bool on));
void status_led_event(led_event_t ev);
void status_led_task(uint32_t now_ms);       // call often with a millisecond clock
