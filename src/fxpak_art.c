/* Optional, cooperative cover sidecar reader. SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include "fxpak_art.h"
#ifdef FXART_HOST_TEST
#include "fxpak_art_test_io.h"
#else
#include "config.h"
#include "memory.h"
#include "snes.h"
#include "ff.h"
#endif

static FIL art_file; /* Never borrow file_handle/file_lfn/file_buf. */
static uint8_t opened, state;
static uint32_t generation, handled;
static unsigned offset;
static uint16_t crc, expected_crc;
static uint8_t chunk[512];
static char sidecar[FXART_PATH_MAX+4];

static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | ((uint32_t)le16(p+2)<<16); }
uint16_t fxart_crc16(uint16_t c, const uint8_t *p, unsigned n) {
  while(n--) {
    c ^= (uint16_t)*p++ << 8;
    for(unsigned i=0;i<8;i++) c = (c & 0x8000) ? (uint16_t)((c<<1)^0x1021) : (uint16_t)(c<<1);
  }
  return c;
}
static int priority_command(void) { return snes_get_mcu_cmd()!=0 || get_snes_reset(); }
static int current(void) {
  return !priority_command()
    && sram_readbyte(FXART_MAILBOX+FXART_REQ)==FXART_READY
    && sram_readlong(FXART_MAILBOX+20)==generation
    && sram_readlong(FXART_MAILBOX+24)==~generation;
}
void fxart_cancel(void) {
  if(opened) f_close(&art_file);
  opened=0; state=0; handled=0; /* Retry a still-current request after a modal command. */
  sram_writebyte(FXART_IDLE,FXART_MAILBOX+8);
}
void fxart_init(void) {
  fxart_cancel(); handled=0;
  sram_memset(FXART_MAILBOX,512,0);
  sram_writeshort(FXART_VERSION,FXART_MAILBOX+4);
  sram_writeshort(FXART_RECORD_SIZE,FXART_MAILBOX+6);
  /* Publish capability last, before the menu CPU is released from reset. */
  sram_writeblock((void*)"FXA1",FXART_MAILBOX,4);
}
static void complete(uint8_t status) {
  if(opened) f_close(&art_file);
  opened=0; state=0;
  if(!current()) return;
  sram_writelong(generation,FXART_MAILBOX+12);
  sram_writebyte(status,FXART_MAILBOX+8); /* completion is the final write */
}
static int valid_header(const uint8_t *h) {
  return !memcmp(h,"FXC1",4) && le16(h+4)==FXART_VERSION
    && le16(h+6)==FXART_RECORD_SIZE && le32(h+8)==0
    && h[12]==80 && h[13]==112 && h[14]==1 && h[15]==0
    && le16(h+16)==FXART_PAYLOAD_SIZE && le16(h+18)==0
    && (uint16_t)(le16(h+20)^le16(h+22))==0xffff
    && le32(h+24)==0xffffffffUL && le32(h+28)==0;
}
void fxart_poll(void) {
  /* Existing commands always win. Each call performs at most one open/read.
     SD reads are bounded to one 512-byte chunk; never wait for an art ACK. */
  if(priority_command()) { fxart_cancel(); return; }
  if(state && !current()) fxart_cancel();
  if(!state) {
    uint8_t req[16];
    sram_readblock(req,FXART_MAILBOX+16,sizeof(req));
    uint32_t next=le32(req+4);
    if(req[0]!=FXART_READY || !next || next==handled || le32(req+8)!=~next) return;
    generation=next;
    unsigned len=le16(req+2);
    if(req[1]!=1 || len<2 || len>=FXART_PATH_MAX) {
      if(current()) {handled=next; complete(FXART_MISSING);} return;
    }
    sram_readblock(sidecar,FXART_MAILBOX+FXART_PATH,len+1);
    if(!current()) return;
    handled=next;
    /* Full, absolute ROM path. No prefix hashes or basename collisions. */
    if(sidecar[0]!='/' || sidecar[len]!=0 || strlen(sidecar)!=len) {complete(FXART_INVALID);return;}
    memcpy(sidecar+len,".fxc",5);
    sram_writebyte(FXART_BUSY,FXART_MAILBOX+8);
    state=1; return;
  }
  if(state==1) {
    if(f_open(&art_file,sidecar,FA_READ)!=FR_OK) {complete(FXART_MISSING);return;}
    opened=1;
    if(!current()) {fxart_cancel();return;}
    if(f_size(&art_file)!=FXART_RECORD_SIZE) {complete(FXART_INVALID);return;}
    offset=0;crc=0xffff;state=2;return;
  }
  UINT got=0;
  unsigned amount=FXART_RECORD_SIZE-offset;
  if(amount>sizeof(chunk)) amount=sizeof(chunk);
  FRESULT result=f_read(&art_file,chunk,amount,&got);
  if(!current()) {fxart_cancel();return;}
  if(result!=FR_OK || got!=amount) {complete(FXART_INVALID);return;}
  unsigned skip=0;
  if(!offset) {
    if(!valid_header(chunk)) {complete(FXART_INVALID);return;}
    expected_crc=le16(chunk+20); skip=FXART_HEADER_SIZE;
  }
  crc=fxart_crc16(crc,chunk+skip,amount-skip);
  sram_writeblock(chunk,FXART_STAGING+offset,amount);
  offset+=amount;
  if(offset<FXART_RECORD_SIZE) return;
  /* Validate BGR555 and the reserved transparent palette entry as well. */
  if(crc!=expected_crc || sram_readshort(FXART_STAGING+4512)!=0) {complete(FXART_INVALID);return;}
  for(unsigned i=0;i<16;i++)
    if(sram_readshort(FXART_STAGING+4512+2*i)&0x8000) {complete(FXART_INVALID);return;}
  if(!current()) {fxart_cancel();return;}
  sram_writelong(generation,FXART_STAGING+8);
  sram_writelong(~generation,FXART_STAGING+24);
  complete(FXART_OK);
}
