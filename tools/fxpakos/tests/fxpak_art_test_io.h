#ifndef FXPAK_ART_TEST_IO_H
#define FXPAK_ART_TEST_IO_H
#include <stdint.h>
#include <stdio.h>
typedef unsigned UINT;
typedef int FRESULT;
typedef struct { FILE *file; unsigned size; } FIL;
#define FR_OK 0
#define FA_READ 1
#define f_size(f) ((f)->size)
#ifdef __cplusplus
extern "C" {
#endif
FRESULT f_open(FIL*,const char*,unsigned);
FRESULT f_read(FIL*,void*,unsigned,UINT*);
FRESULT f_close(FIL*);
uint8_t snes_get_mcu_cmd(void);
uint8_t get_snes_reset(void);
uint8_t sram_readbyte(uint32_t);
uint16_t sram_readshort(uint32_t);
uint32_t sram_readlong(uint32_t);
void sram_writebyte(uint8_t,uint32_t);
void sram_writeshort(uint16_t,uint32_t);
void sram_writelong(uint32_t,uint32_t);
uint16_t sram_readblock(void*,uint32_t,uint16_t);
uint16_t sram_writeblock(void*,uint32_t,uint16_t);
void sram_memset(uint32_t,uint32_t,uint8_t);
#ifdef __cplusplus
}
#endif
#endif
