/* Optional metadata sidecar reader. SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>
#include "fxpak_meta.h"
#include "fxpak_art.h"
#ifdef FXART_HOST_TEST
#include "fxpak_art_test_io.h"
extern uint32_t fxmeta_host_ticks;
#define getticks() fxmeta_host_ticks
#else
#include "config.h"
#include "memory.h"
#include "snes.h"
#include "ff.h"
#include "timer.h"
#endif

static FIL meta_file;
static uint8_t opened, state;
static uint32_t generation, handled, started;
static char sidecar[FXART_PATH_MAX+4];
static uint8_t record[FXMETA_RECORD_SIZE];
static uint16_t le16(const uint8_t *p) {return p[0] | ((uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p) {return le16(p) | ((uint32_t)le16(p+2)<<16);}
static int priority(void) {return snes_get_mcu_cmd()!=0 || get_snes_reset();}
static int current(void) {
  return !priority() && sram_readbyte(FXART_MAILBOX+FXART_REQ)==FXART_READY
    && sram_readlong(FXART_MAILBOX+20)==generation
    && sram_readlong(FXART_MAILBOX+24)==~generation;
}
static int art_done(void) {
  uint8_t status=sram_readbyte(FXART_MAILBOX+8);
  return status>=FXART_OK && status<=FXART_INVALID
    && sram_readlong(FXART_MAILBOX+12)==generation;
}
void fxmeta_cancel(void) {
  if(opened) f_close(&meta_file);
  opened=0;state=0;handled=0;
  sram_writebyte(FXART_IDLE,FXMETA_MAILBOX+8);
}
void fxmeta_init(void) {
  fxmeta_cancel();
  sram_memset(FXMETA_MAILBOX,64,0);
  sram_writeshort(FXMETA_VERSION,FXMETA_MAILBOX+4);
  sram_writeshort(FXMETA_RECORD_SIZE,FXMETA_MAILBOX+6);
  sram_writeblock((void*)"FXM1",FXMETA_MAILBOX,4);
}
static void complete(uint8_t status) {
  if(opened) f_close(&meta_file);
  opened=0;state=0;
  if(!current()) return;
  sram_writelong(generation,FXMETA_MAILBOX+12);
  sram_writebyte(status,FXMETA_MAILBOX+8);
}
static int zeros(const uint8_t *p,unsigned n) {
  while(n--) if(*p++) return 0;
  return 1;
}
static int text(const uint8_t *p,unsigned n) {
  for(unsigned i=0;i<n;i++) {
    if(!p[i]) return zeros(p+i,n-i);
    if(p[i]<32 || p[i]>126) return 0;
  }
  return 0;
}
static int valid(void) {
  unsigned year=le16(record+184),lo=record[186],hi=record[187];
  return !memcmp(record,"FXM1",4) && le16(record+4)==FXMETA_VERSION
    && le16(record+6)==FXMETA_RECORD_SIZE && !le32(record+8)
    && le16(record+12)==224 && !le16(record+14)
    && (uint16_t)(le16(record+16)^le16(record+18))==0xffff
    && le32(record+20)==0xffffffffUL && zeros(record+24,8)
    && fxart_crc16(0xffff,record+32,224)==le16(record+16)
    && record[32] && text(record+32,64) && text(record+96,32)
    && text(record+128,32) && text(record+160,24)
    && (!year || (year>=1900 && year<=2199))
    && ((!lo && !hi) || (lo>=1 && lo<=hi && hi<=8))
    && !(le16(record+188)&~0x3ff) && !(le16(record+190)&~1)
    && zeros(record+192,64);
}
void fxmeta_poll(void) {
  /* Called before artwork: a final art read cannot also start metadata I/O. */
  if(priority()) {fxmeta_cancel();return;}
  if(state && !current()) fxmeta_cancel();
  if(!state) {
    uint8_t req[16];
    sram_readblock(req,FXART_MAILBOX+FXART_REQ,sizeof(req));
    uint32_t next=le32(req+4);
    if(req[0]!=FXART_READY || !next || next==handled || le32(req+8)!=~next) return;
    generation=next;
    if(!art_done()) return;
    unsigned len=le16(req+2);
    if(req[1]!=1 || len<2 || len>=FXART_PATH_MAX) {
      if(current()) {handled=next;complete(FXART_MISSING);}return;
    }
    sram_readblock(sidecar,FXART_MAILBOX+FXART_PATH,len+1);
    if(!current()) return;
    handled=next;
    if(sidecar[0]!='/' || sidecar[len] || strlen(sidecar)!=len) {complete(FXART_INVALID);return;}
    memcpy(sidecar+len,".fxm",5);
    started=getticks();state=1;
    sram_writebyte(FXART_BUSY,FXMETA_MAILBOX+8);
    return;
  }
  if((uint32_t)(getticks()-started)>=120) {complete(FXART_INVALID);return;}
  /* An interrupted artwork service must finish again before metadata I/O. */
  if(!art_done()) return;
  if(state==1) {
    FRESULT result=f_open(&meta_file,sidecar,FA_READ);
    if(result==FR_OK) opened=1;
    if(!current()) {fxmeta_cancel();return;}
    if(result!=FR_OK) {complete(FXART_MISSING);return;}
    if(f_size(&meta_file)!=FXMETA_RECORD_SIZE) {complete(FXART_INVALID);return;}
    state=2;return;
  }
  UINT got=0;
  FRESULT result=f_read(&meta_file,record,sizeof(record),&got);
  if(!current()) {fxmeta_cancel();return;}
  if((uint32_t)(getticks()-started)>=120 || result!=FR_OK || got!=sizeof(record) || !valid()) {
    complete(FXART_INVALID);return;
  }
  if(!current()) {fxmeta_cancel();return;}
  sram_writeblock(record,FXMETA_STAGING,sizeof(record));
  sram_writelong(generation,FXMETA_STAGING+8);
  sram_writelong(~generation,FXMETA_STAGING+20);
  complete(FXART_OK);
}
