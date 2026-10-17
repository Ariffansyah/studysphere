// MAX485 half-duplex transceiver. A/B are modeled digitally: A carries the
// UART level, B its inverse. The bus is released (failsafe-biased, A high)
// when DE is low, so several transceivers/slaves can share one A/B net.
#include "wokwi-api.h"
#include <stdlib.h>

typedef struct {
  pin_t ro, re, de, di, a, b;
} chip_state_t;

static void update(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *c = user_data;
  if (pin_read(c->de)) {
    uint32_t d = pin_read(c->di);
    pin_mode(c->a, d ? OUTPUT_HIGH : OUTPUT_LOW);
    pin_mode(c->b, d ? OUTPUT_LOW : OUTPUT_HIGH);
  } else {
    pin_mode(c->a, INPUT_PULLUP);
    pin_mode(c->b, INPUT_PULLDOWN);
  }
  // RE is active low; the module's pull-up holds RO high while disabled.
  pin_write(c->ro, pin_read(c->re) ? 1 : pin_read(c->a));
}

void chip_init(void) {
  chip_state_t *c = malloc(sizeof(chip_state_t));
  c->ro = pin_init("RO", OUTPUT_HIGH);
  c->re = pin_init("RE", INPUT);
  c->de = pin_init("DE", INPUT);
  c->di = pin_init("DI", INPUT_PULLUP);
  c->a = pin_init("A", INPUT_PULLUP);
  c->b = pin_init("B", INPUT_PULLDOWN);
  const pin_watch_config_t watch = {.edge = BOTH, .pin_change = update, .user_data = c};
  pin_watch(c->re, &watch);
  pin_watch(c->de, &watch);
  pin_watch(c->di, &watch);
  pin_watch(c->a, &watch);
  update(c, c->a, 0);
}
