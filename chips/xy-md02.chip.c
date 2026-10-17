// XY-MD02 SHT20 temperature/humidity sensor, Modbus RTU slave, 9600 8N1.
// Function 0x03/0x04, registers: 0x0001 temp x10 (signed), 0x0002 RH x10,
// 0x0101 slave address, 0x0102 baud code (0 = 9600).
// Talks half-duplex on A: listens with a UART, replies by bit-banging A and
// releases the bus afterwards. Slave address comes from attrs.address.
#include "wokwi-api.h"
#include <stdlib.h>

#define BAUD 9600

typedef struct {
  pin_t a;
  uint32_t temp_attr, hum_attr, addr_attr;
  timer_t frame_timer, bit_timer;
  uint8_t rx[16], tx[16];
  int rx_len, tx_len, tx_bit;
  bool sending;
} chip_state_t;

static uint16_t crc16(const uint8_t *buf, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

static int16_t x10(float v) { return (int16_t)(v * 10 + (v < 0 ? -0.5f : 0.5f)); }

static int32_t read_register(chip_state_t *c, uint16_t reg) {
  switch (reg) {
    case 0x0001: return (uint16_t)x10(attr_read_float(c->temp_attr));
    case 0x0002: return (uint16_t)x10(attr_read_float(c->hum_attr));
    case 0x0101: return attr_read(c->addr_attr);
    case 0x0102: return 0;
    default: return -1;
  }
}

// Level of bit n of the outgoing frame: start(0), 8 data bits LSB first, stop(1).
static uint32_t tx_level(chip_state_t *c, int n) {
  int b = n % 10;
  return b == 0 ? 0 : b == 9 ? 1 : (c->tx[n / 10] >> (b - 1)) & 1;
}

static void on_bit(void *user_data) {
  chip_state_t *c = user_data;
  if (++c->tx_bit == c->tx_len * 10) {
    timer_stop(c->bit_timer);
    pin_mode(c->a, INPUT_PULLUP);
    c->sending = false;
    return;
  }
  pin_write(c->a, tx_level(c, c->tx_bit));
}

static void reply(chip_state_t *c, int len) {
  uint16_t crc = crc16(c->tx, len);
  c->tx[len++] = crc & 0xFF;
  c->tx[len++] = crc >> 8;
  c->tx_len = len;
  c->tx_bit = 0;
  c->sending = true;
  pin_mode(c->a, OUTPUT_LOW);  // start bit of the first byte
  timer_start_ns(c->bit_timer, 1000000000ULL / BAUD, true);
}

// Called after 3.5 character times of silence: one complete request frame.
static void on_frame(void *user_data) {
  chip_state_t *c = user_data;
  uint8_t *r = c->rx, *t = c->tx;
  int n = c->rx_len;
  c->rx_len = 0;
  if (n != 8 || r[0] != attr_read(c->addr_attr) || crc16(r, n) != 0) return;

  uint8_t fc = r[1];
  uint16_t start = r[2] << 8 | r[3], count = r[4] << 8 | r[5];
  t[0] = r[0];
  t[1] = fc;
  if (fc != 0x03 && fc != 0x04) {
    t[1] |= 0x80, t[2] = 0x01;  // illegal function
    return reply(c, 3);
  }
  if (count < 1 || count > 5) {
    t[1] |= 0x80, t[2] = 0x02;  // illegal data address
    return reply(c, 3);
  }
  t[2] = count * 2;
  for (int i = 0; i < count; i++) {
    int32_t v = read_register(c, start + i);
    if (v < 0) {
      t[1] |= 0x80, t[2] = 0x02;
      return reply(c, 3);
    }
    t[3 + i * 2] = v >> 8;
    t[4 + i * 2] = v & 0xFF;
  }
  reply(c, 3 + count * 2);
}

static void on_rx(void *user_data, uint8_t byte) {
  chip_state_t *c = user_data;
  if (c->sending) return;  // our own echo
  if (c->rx_len < (int)sizeof(c->rx)) c->rx[c->rx_len++] = byte;
  timer_start(c->frame_timer, 35 * 1000000 / BAUD, false);
}

void chip_init(void) {
  chip_state_t *c = calloc(1, sizeof(chip_state_t));
  c->temp_attr = attr_init_float("temperature", 25.0);
  c->hum_attr = attr_init_float("humidity", 50.0);
  c->addr_attr = attr_init("address", 1);
  c->a = pin_init("A", INPUT_PULLUP);
  pin_init("B", INPUT_PULLDOWN);
  pin_init("VCC", INPUT);
  pin_init("GND", INPUT);

  const timer_config_t frame = {.callback = on_frame, .user_data = c};
  const timer_config_t bit = {.callback = on_bit, .user_data = c};
  c->frame_timer = timer_init(&frame);
  c->bit_timer = timer_init(&bit);

  const uart_config_t uart = {
    .rx = c->a, .tx = NO_PIN, .baud_rate = BAUD, .rx_data = on_rx, .user_data = c,
  };
  uart_init(&uart);
}
