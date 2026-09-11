#include "fxpak_art_test_io.h"
extern "C" {
#include "fxpak_art.h"
}
#include <string>
#include <cassert>
#include <cstring>
#include <iostream>
extern uint8_t *fxart_host_sram,fxart_host_command;
extern std::string fxart_host_root;
extern unsigned fxart_host_opens,fxart_host_reads,fxart_host_max_read;
extern void (*fxart_host_read_hook)();
static uint8_t ram[65536];
static void request(uint32_t gen,const char* path) {
  sram_writebyte(0,FXART_MAILBOX+16);
  sram_writebyte(1,FXART_MAILBOX+17);
  sram_writeshort(strlen(path),FXART_MAILBOX+18);
  sram_writelong(gen,FXART_MAILBOX+20);
  sram_writelong(~gen,FXART_MAILBOX+24);
  sram_writeblock((void*)path,FXART_MAILBOX+32,strlen(path)+1);
  sram_writebyte(FXART_READY,FXART_MAILBOX+16);
}
static void run(unsigned n=20) {while(n--)fxart_poll();}
int main(int argc,char**argv) {
  assert(argc==2);fxart_host_root=argv[1];fxart_host_sram=ram;
  memset(ram,0x5a,sizeof(ram));fxart_init();
  assert(!memcmp(ram+0x5000,"FXA1",4));
  request(1,"/FXPAK Demo.sfc");run();
  assert(sram_readbyte(FXART_MAILBOX+8)==FXART_OK);
  assert(sram_readlong(FXART_STAGING+8)==1);
  assert(sram_readlong(FXART_STAGING+24)==~uint32_t(1));
  assert(fxart_crc16(0xffff,ram+0x6020,FXART_PAYLOAD_SIZE)==sram_readshort(FXART_STAGING+20));
  request(2,"/missing.sfc");run();assert(sram_readbyte(FXART_MAILBOX+8)==FXART_MISSING);
  request(3,"/version.sfc");run();assert(sram_readbyte(FXART_MAILBOX+8)==FXART_INVALID);
  request(4,"/checksum.sfc");run();assert(sram_readbyte(FXART_MAILBOX+8)==FXART_INVALID);
  request(5,"/short.sfc");run();assert(sram_readbyte(FXART_MAILBOX+8)==FXART_INVALID);
  request(6,"/FXPAK Demo.sfc");run(3);request(7,"/missing.sfc");run();
  assert(sram_readlong(FXART_MAILBOX+12)==7 && sram_readbyte(FXART_MAILBOX+8)==FXART_MISSING);
  request(8,"/FXPAK Demo.sfc");run(2);
  fxart_host_read_hook=[](){fxart_host_command=1;};run(1);fxart_host_read_hook=nullptr;
  assert(fxart_host_opens==0 && sram_readbyte(FXART_MAILBOX+8)!=FXART_OK);
  unsigned reads=fxart_host_reads;run();assert(reads==fxart_host_reads);
  fxart_host_command=0;request(9,"/FXPAK Demo.sfc");run();assert(sram_readbyte(FXART_MAILBOX+8)==FXART_OK);
  assert(fxart_host_max_read<=512 && fxart_host_opens==0);
  for(unsigned i=0;i<65536;i++) if(!((i>=0x5000&&i<0x5200)||(i>=0x6000&&i<0x71c0)))assert(ram[i]==0x5a);
  std::cout<<"PASS MCU worker: success, missing, version/CRC/size rejection, cancellation, command priority, SRAM bounds, 512-byte reads\n";
}
