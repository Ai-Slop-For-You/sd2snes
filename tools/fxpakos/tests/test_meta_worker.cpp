/* Link with --wrap=f_open,--wrap=f_read to count even failed/closed I/O. */
#include "fxpak_art_test_io.h"
extern "C" {
#include "fxpak_art.h"
#include "fxpak_meta.h"
uint32_t fxmeta_host_ticks=0;
FRESULT __real_f_open(FIL*,const char*,unsigned);
FRESULT __real_f_read(FIL*,void*,unsigned,UINT*);
}
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
extern uint8_t *fxart_host_sram,fxart_host_command,fxart_host_reset;
extern std::string fxart_host_root;
extern unsigned fxart_host_opens;
extern void (*fxart_host_read_hook)();
static unsigned operations;
static unsigned short_read;
extern "C" FRESULT __wrap_f_open(FIL *f,const char *p,unsigned mode) {
  operations++;return __real_f_open(f,p,mode);
}
extern "C" FRESULT __wrap_f_read(FIL *f,void *p,unsigned n,UINT *got) {
  operations++;assert(n<=512);
  FRESULT result=__real_f_read(f,p,n,got);
  if(short_read) {*got=0;short_read=0;}
  return result;
}
static uint8_t ram[65536];
static void put16(std::vector<uint8_t>& v,unsigned offset,unsigned n) {v[offset]=n;v[offset+1]=n>>8;}
static void seal(std::vector<uint8_t>& v) {
  unsigned crc=fxart_crc16(0xffff,v.data()+32,224);
  put16(v,16,crc);put16(v,18,crc^65535);
}
static std::vector<uint8_t> metadata() {
  std::vector<uint8_t> v(256);
  memcpy(v.data(),"FXM1",4);put16(v,4,1);put16(v,6,256);put16(v,12,224);
  memset(v.data()+20,255,4);memcpy(v.data()+32,"STARFALL",8);
  memcpy(v.data()+96,"Publisher",9);memcpy(v.data()+128,"Developer",9);
  memcpy(v.data()+160,"Action",6);put16(v,184,1995);v[186]=1;v[187]=2;
  put16(v,188,1023);put16(v,190,1);seal(v);return v;
}
static void write(const std::string& name,const std::vector<uint8_t>& v) {
  std::filesystem::path p=fxart_host_root+name;
  std::filesystem::create_directories(p.parent_path());
  std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size());
}
static void request(uint32_t gen,const char* path) {
  sram_writebyte(0,FXART_MAILBOX+16);sram_writebyte(1,FXART_MAILBOX+17);
  sram_writeshort(strlen(path),FXART_MAILBOX+18);
  sram_writelong(gen,FXART_MAILBOX+20);sram_writelong(~gen,FXART_MAILBOX+24);
  sram_writeblock((void*)path,FXART_MAILBOX+32,strlen(path)+1);
  sram_writebyte(FXART_READY,FXART_MAILBOX+16);
}
static void meta_poll() {
  std::array<uint8_t,65536> before;
  memcpy(before.data(),ram,sizeof(ram));
  bool external_request_mutation=fxart_host_read_hook!=nullptr;
  fxmeta_poll();
  for(unsigned i=0;i<sizeof(ram);i++)
    if(!(external_request_mutation && i>=0x5010 && i<0x5120) && !((i>=0x5200 && i<0x5240)||(i>=0x7200 && i<0x7300))) assert(before[i]==ram[i]);
}
static void tick() {
  operations=0;meta_poll();fxart_poll();assert(operations<=1);fxmeta_host_ticks++;
}
static void run(unsigned n=25) {while(n--)tick();}
static void status(uint32_t gen,uint8_t result) {
  assert(sram_readlong(FXMETA_MAILBOX+12)==gen);
  if(sram_readbyte(FXMETA_MAILBOX+8)!=result)
    std::cerr<<"generation "<<gen<<": expected "<<unsigned(result)<<", got "<<unsigned(sram_readbyte(FXMETA_MAILBOX+8))<<"\n";
  assert(sram_readbyte(FXMETA_MAILBOX+8)==result);
  assert(fxart_host_opens==0);
}
static void terminal(uint32_t gen) {
  sram_writelong(gen,FXART_MAILBOX+12);sram_writebyte(FXART_MISSING,FXART_MAILBOX+8);
}
int main(int argc,char**argv) {
  assert(argc==2);fxart_host_root=argv[1];fxart_host_sram=ram;
  std::filesystem::create_directories(fxart_host_root);
  std::filesystem::remove(fxart_host_root+"/valid.sfc.fxc");
  auto valid=metadata();write("/valid.sfc.fxm",valid);write("/nested/game.sfc.fxm",valid);
  memset(ram,0x5a,sizeof(ram));fxart_init();fxmeta_init();
  assert(!memcmp(ram+0x5200,"FXM1",4));
  assert(sram_readshort(FXMETA_MAILBOX+4)==1 && sram_readshort(FXMETA_MAILBOX+6)==256);
  uint32_t gen=1;
  request(gen,"/valid.sfc");run();status(gen++,FXART_OK);
  assert(sram_readlong(FXMETA_STAGING+8)==1 && sram_readlong(FXMETA_STAGING+20)==~uint32_t(1));
  assert(!memcmp(ram+0x7220,"STARFALL",8));
  assert(sram_readbyte(FXART_MAILBOX+8)==FXART_MISSING); // Metadata needs no cover.
  request(gen,"/missing.sfc");run();status(gen++,FXART_MISSING);
  request(gen,"/nested/game.sfc");run();status(gen++,FXART_OK);
  // Cover can be corrupt while current metadata remains valid.
  write("/valid.sfc.fxc",std::vector<uint8_t>(4));
  request(gen,"/valid.sfc");run();status(gen++,FXART_OK);
  assert(sram_readbyte(FXART_MAILBOX+8)==FXART_INVALID);
  // Valid cover exercises the entire shared I/O schedule, including its last read.
  std::vector<uint8_t> cover(FXART_RECORD_SIZE);
  memcpy(cover.data(),"FXC1",4);put16(cover,4,1);put16(cover,6,FXART_RECORD_SIZE);
  cover[12]=80;cover[13]=112;cover[14]=1;put16(cover,16,FXART_PAYLOAD_SIZE);
  unsigned crc=fxart_crc16(0xffff,cover.data()+32,FXART_PAYLOAD_SIZE);
  put16(cover,20,crc);put16(cover,22,crc^65535);memset(cover.data()+24,255,4);
  write("/valid.sfc.fxc",cover);
  request(gen,"/valid.sfc");run();status(gen++,FXART_OK);
  assert(sram_readbyte(FXART_MAILBOX+8)==FXART_OK);
  // Header, CRC, canonical text, semantic ranges, masks, and reserved bytes.
  const std::vector<std::pair<unsigned,uint8_t>> mutations={
    {0,'Z'},{4,2},{6,1},{8,1},{12,0},{14,1},{18,0},{20,0},{24,1},
    {32,0},{33,1},{41,'x'},{96,127},{128,128},{160,31},
    {184,1},{185,0},{186,0},{187,9},{189,4},{191,1},{192,1}};
  for(auto mutation:mutations) {
    auto bad=valid;bad[mutation.first]=mutation.second;
    if(mutation.first>=32)seal(bad);
    write("/bad.sfc.fxm",bad);request(gen,"/bad.sfc");run();status(gen++,FXART_INVALID);
  }
  auto bad=valid;bad[100]^=1;write("/bad.sfc.fxm",bad);
  request(gen,"/bad.sfc");run();status(gen++,FXART_INVALID);
  bad=valid;memset(bad.data()+32,'A',64);seal(bad);write("/bad.sfc.fxm",bad);
  request(gen,"/bad.sfc");run();status(gen++,FXART_INVALID);
  bad.pop_back();write("/bad.sfc.fxm",bad);
  request(gen,"/bad.sfc");run();status(gen++,FXART_INVALID);
  bad.push_back(0);bad.push_back(0);write("/bad.sfc.fxm",bad);
  request(gen,"/bad.sfc");run();status(gen++,FXART_INVALID);
  // Service cancellation during read: normal command, reset, marker revocation,
  // changed generation and inverse corruption must not publish READY or staging.
  for(unsigned scenario=0;scenario<5;scenario++) {
    fxmeta_cancel();request(gen,"/valid.sfc");terminal(gen);
    meta_poll();meta_poll();assert(fxart_host_opens==1);
    std::array<uint8_t,256> previous;memcpy(previous.data(),ram+0x7200,256);
    if(scenario==0)fxart_host_read_hook=[](){fxart_host_command=1;};
    if(scenario==1)fxart_host_read_hook=[](){fxart_host_reset=1;};
    if(scenario==2)fxart_host_read_hook=[](){sram_writebyte(0,FXART_MAILBOX+16);};
    if(scenario==3)fxart_host_read_hook=[](){request(9000,"/missing.sfc");};
    if(scenario==4)fxart_host_read_hook=[](){sram_writelong(0,FXART_MAILBOX+24);};
    meta_poll();fxart_host_read_hook=nullptr;
    assert(fxart_host_opens==0 && sram_readbyte(FXMETA_MAILBOX+8)!=FXART_OK);
    assert(!memcmp(previous.data(),ram+0x7200,256));
    fxart_host_command=0;fxart_host_reset=0;
    request(gen,"/valid.sfc");terminal(gen); // Still-current normal command retries.
    meta_poll();meta_poll();meta_poll();status(gen++,FXART_OK);
  }
  fxmeta_cancel();request(gen,"/valid.sfc");terminal(gen);meta_poll();meta_poll();
  short_read=1;meta_poll();status(gen++,FXART_INVALID);
  // Deadline after open, including unsigned tick wrap.
  for(uint32_t start:{100u,0xfffffff0u}) {
    fxmeta_cancel();fxmeta_host_ticks=start;request(gen,"/valid.sfc");terminal(gen);
    meta_poll();meta_poll();fxmeta_host_ticks+=120;meta_poll();status(gen++,FXART_INVALID);
  }
  // New requests replace an opened old sidecar without publishing old content.
  fxmeta_cancel();request(gen++,"/valid.sfc");terminal(gen-1);meta_poll();meta_poll();
  request(gen,"/missing.sfc");run();status(gen++,FXART_MISSING);
  // Reject malformed request paths and never open a sidecar from an uncommitted request.
  fxmeta_cancel();request(gen,"relative.sfc");terminal(gen);
  operations=0;meta_poll();status(gen++,FXART_INVALID);assert(operations==0);
  fxmeta_cancel();request(gen,"/valid.sfc");terminal(gen);
  sram_writebyte(0,FXART_MAILBOX+17);operations=0;meta_poll();
  status(gen++,FXART_MISSING);assert(operations==0);
  fxmeta_cancel();request(gen,"/valid.sfc");terminal(gen);
  sram_writebyte('x',FXART_MAILBOX+32+strlen("/valid.sfc"));
  meta_poll();status(gen++,FXART_INVALID);
  fxmeta_cancel();request(gen,"/valid.sfc");terminal(gen);
  sram_writebyte(0,FXART_MAILBOX+16);operations=0;meta_poll();assert(operations==0);
  sram_writebyte(FXART_READY,FXART_MAILBOX+16);
  // 255-byte full path is accepted without borrowing the artwork path storage.
  std::string longest="/"+std::string(120,'a')+"/"+std::string(133,'b');
  assert(longest.size()==255);write(longest+".fxm",valid);
  request(++gen,longest.c_str());run();status(gen++,FXART_OK);
  // Same basename in separate folders and full paths including extension.
  auto alternate=valid;memcpy(alternate.data()+32,"MOONFALL",8);seal(alternate);
  write("/other/game.sfc.fxm",alternate);
  request(gen,"/other/game.sfc");run();status(gen++,FXART_OK);
  assert(!memcmp(ram+0x7220,"MOONFALL",8));
  request(gen,"/nested/game.sfc");run();status(gen++,FXART_OK);
  assert(!memcmp(ram+0x7220,"STARFALL",8));
  fxmeta_cancel();fxart_cancel();
  std::cout<<"PASS metadata: schema/CRC/size, current generation, missing/corrupt cover, combined I/O budget, private SRAM/path, read cancellation, retry, timeout/wrap\n";
}
