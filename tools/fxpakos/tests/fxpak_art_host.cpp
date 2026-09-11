#include "fxpak_art_test_io.h"
#include <string>
#include <cstring>
#include <cstdlib>
#include <cassert>
uint8_t *fxart_host_sram=nullptr;
std::string fxart_host_root;
uint8_t fxart_host_command=0, fxart_host_reset=0;
unsigned fxart_host_opens=0,fxart_host_reads=0,fxart_host_max_read=0;
void (*fxart_host_read_hook)()=nullptr;
static unsigned index(uint32_t a) {assert(a>=0xff0000 && a<=0xffffff);return a-0xff0000;}
extern "C" {
uint8_t snes_get_mcu_cmd(void) {return fxart_host_command;}
uint8_t get_snes_reset(void) {return fxart_host_reset;}
uint8_t sram_readbyte(uint32_t a) {return fxart_host_sram[index(a)];}
uint16_t sram_readshort(uint32_t a) {return sram_readbyte(a)|(sram_readbyte(a+1)<<8);}
uint32_t sram_readlong(uint32_t a) {return sram_readshort(a)|(uint32_t(sram_readshort(a+2))<<16);}
void sram_writebyte(uint8_t d,uint32_t a) {fxart_host_sram[index(a)]=d;}
void sram_writeshort(uint16_t d,uint32_t a) {sram_writebyte(d,a);sram_writebyte(d>>8,a+1);}
void sram_writelong(uint32_t d,uint32_t a) {sram_writeshort(d,a);sram_writeshort(d>>16,a+2);}
uint16_t sram_readblock(void *p,uint32_t a,uint16_t n) {assert(index(a)+n<=65536);memcpy(p,fxart_host_sram+index(a),n);return n;}
uint16_t sram_writeblock(void *p,uint32_t a,uint16_t n) {assert(index(a)+n<=65536);memcpy(fxart_host_sram+index(a),p,n);return n;}
void sram_memset(uint32_t a,uint32_t n,uint8_t d) {assert(index(a)+n<=65536);memset(fxart_host_sram+index(a),d,n);}
FRESULT f_open(FIL *f,const char *path,unsigned) {
  f->file=fopen((fxart_host_root+path).c_str(),"rb");
  if(!f->file)return 1;
  fxart_host_opens++;
  fseek(f->file,0,SEEK_END);f->size=ftell(f->file);rewind(f->file);return FR_OK;
}
FRESULT f_read(FIL *f,void *p,unsigned n,UINT *got) {
  fxart_host_reads++; if(n>fxart_host_max_read)fxart_host_max_read=n;
  *got=fread(p,1,n,f->file);
  if(fxart_host_read_hook)fxart_host_read_hook();
  return ferror(f->file)?1:FR_OK;
}
FRESULT f_close(FIL *f) {if(f->file){fclose(f->file);f->file=nullptr;fxart_host_opens--;}return FR_OK;}
}
