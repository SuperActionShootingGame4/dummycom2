/*
 * PWR / LINK / SCO status LEDs + BOND-CLR button. See software_design.md 5.4.
 */
#pragma once

typedef enum {
    LINK_OFF = 0,   /* not connected */
    LINK_BLINK,     /* paging / reconnecting (1 Hz) */
    LINK_ON,        /* ACL / SLC connected */
} link_led_t;

typedef enum {
    SCO_OFF = 0,    /* no audio link */
    SCO_ON,         /* SCO established (receiving) */
    SCO_ERR,        /* buffer trouble (fast blink) */
} sco_led_t;

/* Configure GPIOs, light PWR, start the LED/button task. */
void leds_init(void);

/* Set LINK / SCO indicator state. Safe to call from any task. */
void leds_set_link(link_led_t s);
void leds_set_sco(sco_led_t s);
