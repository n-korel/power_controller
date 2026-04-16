#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

#define PROTO_MAX_DATA  64

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[PROTO_MAX_DATA];
} proto_packet_t;

void uart_protocol_init(void);
void uart_protocol_process(void);

/* Called from HAL_UART_RxCpltCallback */
void uart_protocol_rx_byte_cb(void);

/* TX helpers */
void uart_send_ack(uint8_t cmd, uint8_t status);
void uart_send_response(uint8_t cmd, const uint8_t *data, uint8_t len);

/* Returns 1 while TX is in progress (for bootloader wait) */
uint8_t uart_tx_busy(void);

#endif /* UART_PROTOCOL_H */
