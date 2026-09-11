#ifndef FXPAK_ART_H
#define FXPAK_ART_H
#include <stdint.h>
/* Optional mailbox: no use of MCU_CMD, SNES_CMD, or their parameters. */
#define FXART_MAILBOX 0xff5000UL
#define FXART_STAGING 0xff6000UL
#define FXART_RECORD_SIZE 4544
#define FXART_HEADER_SIZE 32
#define FXART_TILE_SIZE 4480
#define FXART_PAYLOAD_SIZE 4512
#define FXART_VERSION 1
#define FXART_READY 0xa5
#define FXART_IDLE 0
#define FXART_BUSY 1
#define FXART_OK 2
#define FXART_MISSING 3
#define FXART_INVALID 4
#define FXART_REQ 16
#define FXART_PATH 32
#define FXART_PATH_MAX 256
void fxart_init(void);
void fxart_poll(void);
void fxart_cancel(void);
uint16_t fxart_crc16(uint16_t crc, const uint8_t *data, unsigned size);
#endif
