// Headless bsnes-plus accuracy core, with an explicit simulated cartridge MCU.
#include <snes.hpp>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "fxpak_art.h"
#include "fxpak_meta.h"
uint32_t fxmeta_host_ticks=0;
}
#include "fxpak_art_test_io.h"
extern uint8_t *fxart_host_sram;
extern std::string fxart_host_root;
extern uint8_t fxart_host_command;
extern unsigned fxart_host_opens,fxart_host_max_read;
static bool art_mode=false,art_service=true,art_command_pause=false;
static bool meta_mode=false,meta_service=true;
using namespace SNES;
static std::map<std::string,unsigned> symbols;
static unsigned frame=0, nmis=0, max_nmi_clocks=0, nmi_start=0;
static bool in_nmi=false, pal_region=false;
static unsigned art_dma_bytes=0,max_art_dma_bytes=0;
static void check(bool ok,const char* what) {
  if(!ok) {std::cerr<<"FAIL: "<<what<<" frame="<<frame<<"\n";std::exit(1);}
}
struct PPUAudit : MMIO {
  MMIO* target=nullptr;
  bool blank=true;
  unsigned unsafe_writes=0, cgaddr=0, cghalf=0, unsafe_cgram=0;
  uint8 mmio_read(unsigned a) override {return target->mmio_read(a);}
  void mmio_write(unsigned a,uint8 d) override {
    unsigned reg=a&0xffff;
    if(art_mode && in_nmi && (reg==0x2118 || reg==0x2119) && (cpu.getRegister(CPUDebugger::RegisterPC)&0xffff)>=(symbols.at("fxart_nmi")&0xffff) && (cpu.getRegister(CPUDebugger::RegisterPC)&0xffff)<(symbols.at("fxart_crc_table")&0xffff))art_dma_bytes++;
    if(reg==0x2100) blank=d&0x80;
    if((reg==0x2118 || reg==0x2119 || reg==0x2104) && !blank && cpu.vcounter()>0 && cpu.vcounter()<225) unsafe_writes++;
    if(reg==0x2121) {cgaddr=d;cghalf=0;}
    if(reg==0x2122) {
      // Stock HDMA intentionally updates only backdrop color 0 each scanline.
      if(cgaddr!=0 && !blank && cpu.vcounter()>0 && cpu.vcounter()<225) unsafe_cgram++;
      if(++cghalf==2) {cghalf=0;cgaddr=(cgaddr+1)&255;}
    }
    target->mmio_write(a,d);
  }
} audit;
static std::string outdir;
static unsigned ramword(const char* name) {
  unsigned a=symbols.at(name)&0x1ffff;
  return memory::wram[a]|(memory::wram[a+1]<<8);
}
static void dump(const std::string& name,const uint8_t* p,unsigned n) {
  std::ofstream f(outdir+"/"+name,std::ios::binary); f.write((const char*)p,n);
}
struct Frontend : Interface {
  unsigned buttons=0;
  std::vector<uint8_t> rgb;
  unsigned w=0,h=0;
  int16_t input_poll(bool port,Input::Device,unsigned,unsigned id) override {return !port && ((buttons>>id)&1);}
  void video_refresh(const uint16_t* data,unsigned width,unsigned height) override {
    w=width; h=height; rgb.resize(w*h*3);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
      auto c=data[y*1024+x];
      for(unsigned k=0;k<3;k++) rgb[(y*w+x)*3+k]=((c>>(5*(2-k)))&31)*255/31;
    }
  }
  void save(const char* name) {
    std::ofstream f(outdir+"/"+name,std::ios::binary);
    f<<"P6\n"<<w<<" "<<h<<"\n255\n";f.write((char*)rgb.data(),rgb.size());
  }
} frontend;
struct FakeMCU : MMIO {
  uint8_t bram[512]={};
  std::vector<unsigned> commands;
  unsigned pending=0,ready_at=0;
  std::string launched_path;
  bool reset_requested=false;
  void service() {
    if(!pending || frame<ready_at) return;
    if(pending==0x0a) directory();
    bram[2]=pending==0x0b ? 0x77 : 0x55;
    pending=0;
  }
  std::string cwd() {return (char*)memory::cartram.data();}
  void directory() {
    uint8_t* rom=memory::cartrom.data();
    memset(rom+0x20000,0,0x10000);
    std::vector<std::pair<std::string,unsigned>> names;
    if(cwd()=="/") names={{"Homebrew/",0x40},{"FXPAK Demo.sfc",1},{"A very long game filename for horizontal scrolling regression test.sfc",1}};
    else names={{"../",0x80},{"Inside Folder.sfc",1}};
    for(unsigned i=0;i<24;i++) names.push_back({"Test Game "+std::to_string(i)+".sfc",1});
    for(unsigned i=0;i<names.size();i++) {
      unsigned a=0x21000+i*128;
      rom[0x20000+i*4]=a&255;rom[0x20001+i*4]=(a>>8)&255;
      rom[0x20002+i*4]=a>>16;rom[0x20003+i*4]=names[i].second;
      memcpy(rom+a,"  32K ",6);strcpy((char*)rom+a+6,names[i].first.c_str());
    }
  }
  uint8 mmio_read(unsigned addr) override {return bram[addr&511];}
  void mmio_write(unsigned addr,uint8 data) override {
    bram[addr&511]=data;
    if((addr&0xffff)!=0x2a00 || data==0) return;
    commands.push_back(data);
    std::cout<<"MCU frame="<<frame<<" cmd="<<std::hex<<unsigned(data)<<std::dec<<" cwd="<<cwd()<<"\n";
    if(data==0x0a || data==0x01 || data==0x0b) {pending=data;ready_at=frame+3;}
    if(data==0x01) {
      check(bram[4]==0 && bram[5]==0 && bram[6]==0xff,"LOADROM CWD pointer");
      unsigned a=bram[8]|(bram[9]<<8)|(bram[10]<<16);
      check(a>=0x21000 && a<0x22000,"LOADROM descriptor address");
      launched_path=cwd()+(const char*)(memory::cartrom.data()+a+6);
    }
    if(data==0x80) reset_requested=true;
  }
} mcu;
static void frames(unsigned count) {
  for(unsigned i=0;i<count;i++) {
    mcu.service();
    if(art_mode) {
      fxart_host_command=(art_command_pause || mcu.pending || mcu.reset_requested) ? 1 : 0;
      fxmeta_host_ticks=uint64_t(frame)*100/(pal_region?50:60);
      if(meta_mode && meta_service)fxmeta_poll();
      if(art_service)fxart_poll();
    }
    SNES::system.run();frame++;
  }
}
static void snap(const char* name) {
  std::cout<<name<<" frame="<<frame<<" pc="<<std::hex<<cpu.getRegister(CPUDebugger::RegisterPC)<<std::dec
    <<" nmi="<<nmis<<" sel="<<ramword("filesel_sel")<<" sp="<<cpu.getRegister(CPUDebugger::RegisterS)<<" cwd="<<mcu.cwd()<<"\n";
  frontend.save(name);
  dump(std::string(name)+".wram",memory::wram.data(),0x20000);
  dump(std::string(name)+".vram",memory::vram.data(),0x10000);
  dump(std::string(name)+".cgram",memory::cgram.data(),512);
  dump(std::string(name)+".oam",memory::oam.data(),544);
}
static void press(unsigned id) {frontend.buttons=1<<id;frames(2);frontend.buttons=0;frames(12);}
static unsigned rambyte(const char* n) {return ramword(n)&255;}
static uint32_t generation() {return sram_readlong(FXART_MAILBOX+20);}
static bool dynamic() {return rambyte("fxart_source")==1;}
static void fallback() {check(!dynamic(),"fallback must remain selected");}
static std::vector<uint8_t> record(const std::string& path) {
  std::ifstream f(fxart_host_root+path,std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
static void publish(std::vector<uint8_t> data,uint32_t gen) {
  check(data.size()==FXART_RECORD_SIZE,"fixture size");
  sram_writeblock(data.data(),FXART_STAGING,data.size());
  sram_writelong(gen,FXART_STAGING+8);sram_writelong(~gen,FXART_STAGING+24);
  sram_writelong(gen,FXART_MAILBOX+12);sram_writebyte(FXART_OK,FXART_MAILBOX+8);
}
static void matches(const std::string& path) {
  auto data=record(path);
  if(!dynamic())std::cerr<<"art path="<<path<<" gen="<<generation()<<" phase="<<rambyte("fxart_phase")<<" status="<<unsigned(sram_readbyte(FXART_MAILBOX+8))<<" row="<<rambyte("fxart_row")<<"\n";
  check(dynamic(),"complete cover must become active");
  if(rambyte("fxos_cover_visible")!=2)snap("modal-fail.ppm");
  check(rambyte("fxos_cover_visible")==2,"dynamic OAM is visible");
  for(unsigned row=0;row<14;row++)
    check(!memcmp(memory::vram.data()+0xe400+row*512,data.data()+32+row*320,320),"native tile rows match SD sidecar");
  check(!memcmp(memory::cgram.data()+0x120,data.data()+4512,32),"native palette matches sidecar");
  check((memory::oam[32*4+2]|((memory::oam[32*4+3]&1)<<8))==288,"dynamic OBJ tile base");
  check((memory::oam[32*4+3]&14)==2,"dynamic OBJ palette bank");
}
static void run_art_tests() {
  const std::string first="/FXPAK Demo.sfc.fxc";
  const std::string second="/A very long game filename for horizontal scrolling regression test.sfc.fxc";
  frames(180);snap("boot.ppm");fallback();
  check(generation()==1,"initial non-ROM selection generates cancellation");
  std::vector<uint8_t> fallback_tiles(memory::vram.data()+0xc800,memory::vram.data()+0xe400);
  std::vector<uint8_t> fallback_palette(memory::cgram.data()+0x100,memory::cgram.data()+0x120);
  unsigned stack=cpu.getRegister(CPUDebugger::RegisterS);
  press(5);
  check(generation()==2,"ROM highlight increments generation");
  check(std::string((char*)fxart_host_sram+0x5020)=="/FXPAK Demo.sfc","full selected path request");
  fallback();frames(65);snap("first.ppm");matches(first);
  press(5);check(generation()==3,"second ROM increments generation");fallback();
  frames(65);snap("second.ppm");matches(second);
  press(5);frames(65);snap("missing.ppm");fallback();
  check(sram_readbyte(FXART_MAILBOX+8)==FXART_MISSING,"missing sidecar reported");
  // Inject malformed publications after bypassing MCU checks: exercise SNES validator.
  art_service=false;
  for(unsigned kind=0;kind<4;kind++) {
    press(4);fallback();
    auto data=record(second);uint32_t gen=generation();
    if(kind==0)data[4]=2;
    if(kind==1)data[100]^=1;
    if(kind==2)gen--;
    if(kind==3) {frames(130);check(sram_readbyte(FXART_MAILBOX+16)==0,"deadline revokes request");}
    publish(data,gen);frames(60);fallback();
    press(5);frames(2);fallback();
  }
  // Corrupt a record after partial WRAM snapshot, and replace its publication.
  for(unsigned kind=0;kind<2;kind++) {
    press(4);publish(record(second),generation());frames(4);
    check(rambyte("fxart_phase")==2,"SNES snapshot in progress");
    if(kind==0)sram_writebyte(sram_readbyte(FXART_STAGING+3000)^1,FXART_STAGING+3000);
    else sram_writelong(generation()-1,FXART_MAILBOX+12);
    frames(60);fallback();press(5);
  }
  // Rapid real controller changes during I/O and during the WRAM validation stage.
  art_service=true;
  for(unsigned i=0;i<8;i++) {press(4);fallback();press(5);fallback();}
  // Four-frame alternating selections also outrun the reader's first file.
  uint32_t burst_generation=generation();
  for(unsigned i=0;i<12;i++)for(unsigned button:{4u,5u}) {
    frontend.buttons=1<<button;frames(2);frontend.buttons=0;frames(2);fallback();
  }
  check(generation()>=burst_generation+24,"rapid four-frame selections generate fresh requests");
  press(4);frames(65);matches(second);
  // Existing MCU work can interrupt an SD read; unchanged selection must resume.
  press(4);art_command_pause=true;frames(3);fallback();
  check(fxart_host_opens==0,"command closes in-progress sidecar");
  art_command_pause=false;frames(65);matches(first);press(5);frames(65);matches(second);
  // Cancel specifically in the middle of the inactive VRAM upload.
  press(4);
  unsigned deadline=frame+100;
  while(rambyte("fxart_row")==0 && frame<deadline)frames(1);
  check(rambyte("fxart_row")!=0,"multi-frame upload reached");
  press(5);fallback();frames(65);matches(second);
  // Existing dialogs/favorites/recent must hide and restore the dynamic cover.
  for(unsigned button: {9u,1u,2u,3u}) {
    press(button);check(rambyte("fxos_cover_visible")==0,"modal hides dynamic cover");
    press(0);frames(2);
    matches(second);
  }
  // Directory selection cancels artwork. Real directory/parent command flow remains.
  press(10);fallback();press(8);check(mcu.cwd()=="/Homebrew","directory enter with art service");
  press(5);frames(2);
  check(std::string((char*)fxart_host_sram+0x5020)=="/Homebrew/Inside Folder.sfc","nested full ROM identity");
  press(0);check(mcu.cwd()=="/","directory parent with art service");
  // Hidden extensions still request the original filename (byte 1 means '.').
  char* leaf=(char*)memory::cartrom.data()+0x21000+128+6;
  char* dot=strrchr(leaf,'.');check(dot!=nullptr,"fixture extension");*dot=1;
  press(5);check(std::string((char*)fxart_host_sram+0x5020)=="/FXPAK Demo.sfc","hidden extension restored in request");
  *dot='.';frames(65);matches(first);
  // Carry across the low generation word must preserve full 32-bit identity.
  unsigned ga=symbols.at("fxart_generation")&0x1ffff;
  memory::wram[symbols.at("fxart_source")&0x1ffff]=0;
  memory::wram[ga]=255;memory::wram[ga+1]=255;memory::wram[ga+2]=0;memory::wram[ga+3]=0;
  press(5);check(generation()==0x10000,"generation carry across 16-bit word");frames(65);matches(second);
  press(4);frames(65);matches(first);
  unsigned before=nmis;frames(600);check(nmis-before==600,"one NMI per idle frame");
  check(cpu.getRegister(CPUDebugger::RegisterS)==stack,"art foreground stack balanced");
  check(!memcmp(memory::vram.data()+0xc800,fallback_tiles.data(),fallback_tiles.size()),"fallback tile region immutable");
  check(!memcmp(memory::cgram.data()+0x100,fallback_palette.data(),32),"fallback palette immutable");
  check(audit.unsafe_writes==0,"no active VRAM/OAM writes with artwork");
  check(audit.unsafe_cgram==0,"no active non-backdrop CGRAM writes with artwork");
  // Launch another selected ROM while a new art read is pending.
  press(5);fallback();press(8);frames(10);
  check(mcu.launched_path==second.substr(0,second.size()-4),"correct ROM launched while art pending");
  check(mcu.reset_requested,"ROM handshake reaches reset with artwork pending");
  check(fxart_host_opens==0,"art file closed on command");
  check(fxart_host_max_read<=512,"bounded MCU read size");
  std::cout<<"PASS ART region="<<(pal_region?"PAL":"NTSC")<<" frames="<<frame<<" nmis="<<nmis
    <<" max_nmi_master_clocks="<<max_nmi_clocks<<" unsafe_writes="<<audit.unsafe_writes
    <<" max_art_tile_bytes="<<max_art_dma_bytes<<" unsafe_cgram="<<audit.unsafe_cgram<<"\n";
}
static void run_art_edge_tests() {
  frames(180);check(generation()==41,"warm startup advances surviving generation");fallback();
  press(5);frames(50);check(generation()==42,"new selection advances old mailbox");fallback();
  publish(record("/FXPAK Demo.sfc.fxc"),generation());frames(40);matches("/FXPAK Demo.sfc.fxc");
  // The maximum accepted path is 255 bytes including the leading slash.
  uint8_t* rom=memory::cartrom.data();
  rom[0x20004]=0;rom[0x20005]=0x80;rom[0x20006]=2;
  std::string leaf=std::string(250,'a')+".sfc";
  strcpy((char*)rom+0x28006,leaf.c_str());frames(12);fallback();
  check(sram_readshort(FXART_MAILBOX+18)==255,"255-byte path supported");
  check(std::string((char*)fxart_host_sram+0x5020)=="/"+leaf,"long path exact identity");
  publish(record("/FXPAK Demo.sfc.fxc"),generation());frames(40);matches("/FXPAK Demo.sfc.fxc");
  leaf="a"+leaf;strcpy((char*)rom+0x28006,leaf.c_str());frames(12);fallback();
  check(sram_readshort(FXART_MAILBOX+18)==0 && sram_readbyte(FXART_MAILBOX+17)==0,"overlong path cancels safely");
  rom[0x20004]=0x80;rom[0x20005]=0x10;art_service=true;frames(65);matches("/FXPAK Demo.sfc.fxc");
  unsigned ga=symbols.at("fxart_generation")&0x1ffff;
  memory::wram[symbols.at("fxart_source")&0x1ffff]=0;
  memory::wram[ga]=254;memory::wram[ga+1]=255;memory::wram[ga+2]=255;memory::wram[ga+3]=255;
  press(5);check(generation()==0xffffffff,"last unique generation supported");frames(65);
  matches("/A very long game filename for horizontal scrolling regression test.sfc.fxc");
  press(5);fallback();check(rambyte("fxart_exhausted")==1,"generation wrap disables optional service");
  check(sram_readbyte(FXART_MAILBOX+16)==0,"wrapped request is never published");
  press(4);frames(40);fallback();check(sram_readbyte(FXART_MAILBOX+16)==0,"navigation after wrap stays safe");
  check(audit.unsafe_writes==0 && audit.unsafe_cgram==0,"edge cases preserve safe display writes");
  press(8);frames(10);check(mcu.reset_requested,"ROM launch still works after generation exhaustion");
  std::cout<<"PASS ART EDGES region="<<(pal_region?"PAL":"NTSC")<<" frames="<<frame
    <<" max_nmi_master_clocks="<<max_nmi_clocks<<"\n";
}
static void meta_matches(const std::string& path) {
  auto data=record(path);check(data.size()==256,"metadata fixture size");
  uint32_t gen=generation();
  for(unsigned i=0;i<4;i++){data[8+i]=gen>>(8*i);data[20+i]=(~gen)>>(8*i);}
  if(!ramword("fxmeta_valid")) {
    snap("meta-fail.ppm");
    std::cerr<<"meta status="<<unsigned(sram_readbyte(FXMETA_MAILBOX+8))<<" gen="<<gen<<" handled="<<ramword("fxmeta_handled")<<"\n";
  }
  check(ramword("fxmeta_valid")==1,"valid metadata becomes visible");
  check(!memcmp(memory::wram.data()+0x11300,data.data(),256),"entire accepted metadata matches compiler record");
  check(!memcmp(memory::wram.data()+(symbols.at("fxmeta_display_text")&0x1ffff),data.data()+32,64),"display title matches accepted record");
}
static void meta_stamp(std::vector<uint8_t>& data,uint32_t gen) {
  for(unsigned i=0;i<4;i++){data[8+i]=gen>>(8*i);data[20+i]=(~gen)>>(8*i);}
}
static void meta_publish(const std::vector<uint8_t>& data,uint32_t response) {
  sram_writebyte(FXART_BUSY,FXMETA_MAILBOX+8);
  sram_writeblock((void*)data.data(),FXMETA_STAGING,256);
  sram_writelong(response,FXMETA_MAILBOX+12);
  sram_writebyte(FXART_OK,FXMETA_MAILBOX+8);
}
static void meta_seal(std::vector<uint8_t>& data) {
  unsigned crc=fxart_crc16(0xffff,data.data()+32,224);
  data[16]=crc;data[17]=crc>>8;data[18]=~crc;data[19]=(~crc)>>8;
}
static void meta_fallback(const std::string& title) {
  check(ramword("fxmeta_valid")==0,"missing/invalid metadata uses fallback");
  unsigned a=symbols.at("fxmeta_display_text")&0x1ffff;
  check(std::string((char*)memory::wram.data()+a)==title,"fallback uses current ROM filename");
}
static void run_meta_tests() {
  const std::string first="/FXPAK Demo.sfc", second="/A very long game filename for horizontal scrolling regression test.sfc";
  frames(200);snap("boot.ppm");meta_fallback("Homebrew/");
  unsigned stack=cpu.getRegister(CPUDebugger::RegisterS);
  std::vector<uint8_t> fallback_tiles(memory::vram.data()+0xc800,memory::vram.data()+0xe400);
  std::vector<uint8_t> fallback_palette(memory::cgram.data()+0x100,memory::cgram.data()+0x120);
  press(5);check(!ramword("fxmeta_valid"),"new selection clears old metadata immediately");
  frames(65);meta_matches(first+".fxm");matches(first+".fxc");snap("first.ppm");
  press(5);check(!ramword("fxmeta_valid"),"second selection clears first title");
  frames(65);meta_matches(second+".fxm");matches(second+".fxc");snap("second.ppm");
  press(5);frames(65);meta_fallback("Test Game 0.sfc");fallback();snap("missing.ppm");
  press(5);frames(65);meta_fallback("Test Game 1.sfc");snap("bad-crc.ppm");
  check(sram_readbyte(FXMETA_MAILBOX+8)==FXART_INVALID,"MCU rejects bad metadata CRC");
  press(5);frames(65);meta_fallback("Test Game 2.sfc");snap("bad-version.ppm");
  check(sram_readbyte(FXMETA_MAILBOX+8)==FXART_INVALID,"MCU rejects bad metadata version");
  press(4);press(4); // Missing entry: each injection selects the valid second cover anew.
  meta_service=false;
  for(unsigned kind=0;kind<19;kind++) {
    press(4);frames(25);matches(second+".fxc");
    auto data=record(second+".fxm");uint32_t response=generation();meta_stamp(data,response);
    if(kind==0)data[4]=2;
    if(kind==1)data[100]^=1;
    if(kind==2){response--;meta_stamp(data,response);} // Previous selection while real new cover is visible.
    if(kind==3)data[20]^=1;
    if(kind==4)data[18]^=1;
    if(kind==5)data[24]=1;
    if(kind==6)data[192]=1;
    if(kind==7)data[40]='X'; // Nonzero text after NUL.
    if(kind==8){data[186]=0;data[187]=2;}
    if(kind==9){data[186]=3;data[187]=2;}
    if(kind==10)data[189]=4;
    if(kind==11)data[190]=2;
    if(kind==12)data[6]=1;
    if(kind==13)data[12]=1;
    if(kind==14)data[14]=1;
    if(kind==15)data[32]=0;
    if(kind==16)memset(data.data()+32,'A',64);
    if(kind==17)data[32]=128;
    if(kind==18){data[184]=0;data[185]=1;}
    if(kind>=6)meta_seal(data); // Semantic failures must survive valid CRC verification.
    meta_publish(data,response);frames(65);
    meta_fallback(second.substr(1,63));matches(second+".fxc");
    if(kind==2)snap("stale-with-cover.ppm");
    press(5);frames(2);
  }
  meta_service=true;
  uint32_t burst=generation();
  for(unsigned i=0;i<12;i++)for(unsigned button:{4u,5u}) {
    frontend.buttons=1<<button;frames(2);frontend.buttons=0;frames(2);
    check(!ramword("fxmeta_valid"),"rapid selection never retains accepted old metadata");
  }
  check(generation()>=burst+24,"rapid metadata selections advance generations");
  press(4);frames(65);meta_matches(second+".fxm");matches(second+".fxc");snap("rapid.ppm");
  // Pause the real service specifically with its metadata FIL open.
  press(4);
  unsigned deadline=frame+100;
  while(!(sram_readbyte(FXMETA_MAILBOX+8)==FXART_BUSY && fxart_host_opens==1 &&
          sram_readbyte(FXART_MAILBOX+8)==FXART_OK) && frame<deadline)frames(1);
  check(frame<deadline,"metadata open reached for command interruption");
  art_command_pause=true;frames(3);check(fxart_host_opens==0,"normal command closes metadata FIL");
  art_command_pause=false;frames(65);meta_matches(first+".fxm");matches(first+".fxc");
  unsigned modal=0;
  for(unsigned button:{9u,1u,2u,3u}) {
    press(button);check(rambyte("fxos_cover_visible")==0,"metadata modal hides cover");
    snap(("modal-"+std::to_string(modal++)+".ppm").c_str());
    press(0);frames(2);meta_matches(first+".fxm");matches(first+".fxc");
  }
  snap("restored.ppm");
  press(10);press(8);check(mcu.cwd()=="/Homebrew","metadata directory entry works");
  press(5);frames(65);meta_matches("/Homebrew/Inside Folder.sfc.fxm");fallback();snap("nested.ppm");
  check(std::string((char*)fxart_host_sram+0x5020)=="/Homebrew/Inside Folder.sfc","metadata uses full nested identity");
  press(0);check(mcu.cwd()=="/","metadata parent navigation works");
  char* leaf=(char*)memory::cartrom.data()+0x21000+128+6;
  char* dot=strrchr(leaf,'.');check(dot!=nullptr,"hidden extension fixture");*dot=1;
  press(5);frames(65);meta_matches(first+".fxm");matches(first+".fxc");snap("hidden-valid.ppm");
  check(std::string((char*)fxart_host_sram+0x5020)==first,"hidden extension preserves metadata request identity");*dot='.';
  char* missing=(char*)memory::cartrom.data()+0x21000+3*128+6;
  char* missing_dot=strrchr(missing,'.');check(missing_dot!=nullptr,"hidden missing fixture");*missing_dot=1;
  press(5);press(5);frames(65);meta_fallback("Test Game 0");snap("hidden-missing.ppm");
  check(std::string((char*)fxart_host_sram+0x5020)=="/Test Game 0.sfc","hidden fallback retains full request path");*missing_dot='.';
  press(4);frames(65);meta_matches(second+".fxm");matches(second+".fxc");
  unsigned before=nmis;frames(600);check(nmis-before==600,"metadata idle has one NMI per frame");
  check(cpu.getRegister(CPUDebugger::RegisterS)==stack,"metadata foreground stack balanced");
  check(!memcmp(memory::vram.data()+0xc800,fallback_tiles.data(),fallback_tiles.size()),"metadata leaves fallback tiles immutable");
  check(!memcmp(memory::cgram.data()+0x100,fallback_palette.data(),32),"metadata leaves fallback palette immutable");
  check(audit.unsafe_writes==0 && audit.unsafe_cgram==0,"metadata never writes active PPU registers");
  press(4);press(8);frames(10);
  check(mcu.launched_path==first,"correct ROM launched while metadata pending");
  check(mcu.reset_requested && fxart_host_opens==0,"launch reaches reset with no sidecar FIL open");
  std::cout<<"PASS META region="<<(pal_region?"PAL":"NTSC")<<" frames="<<frame<<" nmis="<<nmis
    <<" max_nmi_master_clocks="<<max_nmi_clocks<<" max_art_tile_bytes="<<max_art_dma_bytes
    <<" unsafe_writes="<<audit.unsafe_writes<<" unsafe_cgram="<<audit.unsafe_cgram<<"\n";
}
int main(int argc,char**argv) {
  if(argc<4 || argc>6) {std::cerr<<"usage: menu_harness ROM SYMBOLS OUTPUT_DIR [NTSC|PAL] [ART_SD]\n";return 2;}
  outdir=argv[3]; pal_region=argc>=5 && std::string(argv[4])=="PAL";
  meta_mode=std::getenv("FXMETA_TESTS")!=nullptr;
  art_mode=argc==6;check(!meta_mode || art_mode,"metadata mode requires fixture directory"); if(art_mode)fxart_host_root=argv[5];
  std::ifstream syms(argv[2]); unsigned addr;std::string name;
  while(syms>>std::hex>>addr>>name) symbols[name]=addr;
  std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> rom(0x400000,0);
  f.read((char*)rom.data(),65536);if(f.gcount()!=65536)return 2;
  SNES::system.init(&frontend);
  memory::cartrom.copy(rom.data(),rom.size());
  cartridge.load(Cartridge::Mode::Normal,lstring{R"(<cartridge region="NTSC"><rom><map mode="shadow" address="00-3f:8000-ffff"/><map mode="linear" address="c0-ff:0000-ffff"/></rom><ram size="0x10000"><map mode="linear" address="ff:0000-ffff"/></ram><srtc><map address="00-3f:2800-2801"/></srtc></cartridge>)"});
  memset(memory::cartram.data(),0,0x10000);
  memory::cartram.data()[0x19d]=15;
  memory::cartram.data()[0x1101]=2;
  memory::cartram.data()[0x1103]=2;
  strcpy((char*)memory::cartram.data()+0x1420,"Recent Demo.sfc");
  strcpy((char*)memory::cartram.data()+0x1520,"Second Recent.sfc");
  strcpy((char*)memory::cartram.data()+0x4000,"Favorite Demo.sfc");
  strcpy((char*)memory::cartram.data()+0x4100,"Second Favorite.sfc");
  if(art_mode) {
    fxart_host_sram=memory::cartram.data();fxart_init();
    if(meta_mode)fxmeta_init();
    if(std::getenv("FXART_EDGE_TESTS")) {
      publish(record("/FXPAK Demo.sfc.fxc"),40);
      sram_writelong(40,FXART_MAILBOX+20);sram_writelong(~uint32_t(40),FXART_MAILBOX+24);
      art_service=false;
    }
  }
  // Seed a valid RTC so screenshots do not depend on uninitialized RTC RAM.
  uint8_t rtc_seed[13]={0,0,0,0,2,1,1,1,9,6,2,10,5};
  memcpy(memory::cartrtc.data(),rtc_seed,13);
  uint32_t now=uint32_t(time(nullptr));
  for(unsigned i=0;i<4;i++) memory::cartrtc.data()[16+i]=now>>(8*i);
  config().region=pal_region ? System::Region::PAL : System::Region::NTSC;
  SNES::system.power();
  audit.target=memory::mmio.handle(0x2100);
  memory::mmio.map(0x2100,0x213f,audit);
  memory::mmio.map(0x2a00,0x2bff,mcu);
  mcu.bram[2]=0x55;
  cpu.step_event=[](){
    unsigned pc=cpu.getRegister(CPUDebugger::RegisterPC);
    unsigned clock=cpu.vcounter()*1364+cpu.hcounter();
    if((pc&0xffff)==(symbols.at("NMI_16bit")&0xffff)) {
      check(!in_nmi,"NMI must not reenter");in_nmi=true;nmis++;nmi_start=clock;art_dma_bytes=0;
    }
    if(in_nmi && cpu.disassembler_read(pc)==0x40) {
      unsigned elapsed=(clock+(pal_region?312:262)*1364-nmi_start)%((pal_region?312:262)*1364);
      if(elapsed>max_nmi_clocks) max_nmi_clocks=elapsed;
      check(cpu.vcounter()>=225,"NMI must finish inside VBlank");
      check(elapsed<(pal_region?87:37)*1364,"NMI duration bounded by one VBlank");
      if(art_mode) {
        if(art_dma_bytes>max_art_dma_bytes)max_art_dma_bytes=art_dma_bytes;
        check(art_dma_bytes<=640,"at most 640 artwork tile bytes per VBlank");
        if(dynamic()) {
          unsigned ga=symbols.at("fxart_generation")&0x1ffff,ta=symbols.at("fxart_transfer_generation")&0x1ffff;
          check(!memcmp(memory::wram.data()+ga,memory::wram.data()+ta,4),"visible cover generation always current");
        }
      }
      if(meta_mode && ramword("screen_dma_disable")==0 && ramword("window_stack_head")==0xffff && ramword("fxmeta_valid")) {
        unsigned ma=symbols.at("fxmeta_generation")&0x1ffff,ga=symbols.at("fxart_generation")&0x1ffff;
        check(!memcmp(memory::wram.data()+ma,memory::wram.data()+ga,4),"visible metadata generation matches artwork selection");
        check(!memcmp(memory::wram.data()+ma,memory::wram.data()+0x11308,4),"accepted metadata generation matches visible panel");
      }
      in_nmi=false;
    }
  };
  if(meta_mode) {run_meta_tests();return 0;}
  if(art_mode) {if(std::getenv("FXART_EDGE_TESTS"))run_art_edge_tests();else run_art_tests();return 0;}
  frames(200);snap("boot.ppm");
  check(nmis>=170,"boot reaches stable NMI loop");
  check(ramword("filesel_sel")==0,"initial selection");
  check((ramword("fxos_cover_visible")&255)==1,"cover visible on boot");
  unsigned stack=cpu.getRegister(CPUDebugger::RegisterS);
  press(8);snap("folder.ppm");check(mcu.cwd()=="/Homebrew","A enters directory");
  press(0);snap("parent.ppm");check(mcu.cwd()=="/","B returns to parent");
  press(5);snap("down.ppm");check(ramword("filesel_sel")==1,"Down moves selection");
  press(9);snap("menu.ppm");check((ramword("fxos_cover_visible")&255)==0,"main menu hides cover");
  press(0);snap("closed.ppm");check((ramword("fxos_cover_visible")&255)==1,"cover restored after modal");
  press(1);snap("context.ppm");check((ramword("fxos_cover_visible")&255)==0,"context menu hides cover");
  press(0);
  press(2);snap("favorites.ppm");check((ramword("fxos_cover_visible")&255)==0,"Select opens favorites");
  press(0);
  press(3);snap("recent.ppm");check((ramword("fxos_cover_visible")&255)==0,"Start opens recent");
  press(0);
  press(7);snap("page.ppm");check(ramword("dirptr_addr")>0,"Right pages forward");
  press(6);check(ramword("dirptr_addr")==0,"Left pages back");
  press(11);check(ramword("filesel_sel")==17,"R selects end of listing");
  press(10);check(ramword("filesel_sel")==0 && ramword("dirptr_addr")==0,"L returns to first entry");
  press(5);press(5);frames(100);snap("marquee.ppm");
  check(ramword("direntry_xscroll")>0,"long filename scrolls");
  press(4);check(ramword("filesel_sel")==1,"Up moves selection");
  unsigned before=nmis;
  frames(600);snap("stable.ppm");
  check(nmis-before==600,"one NMI per frame over 600 idle frames");
  check(cpu.getRegister(CPUDebugger::RegisterS)==stack,"foreground stack remains balanced");
  check(audit.unsafe_writes==0,"no VRAM/OAM writes during active display");
  check(audit.unsafe_cgram==0,"no non-backdrop CGRAM writes during active display");
  press(8);frames(10);snap("launch.ppm");
  check(mcu.launched_path=="/FXPAK Demo.sfc","correct selected ROM passed to MCU");
  check(mcu.reset_requested,"LOADROM/ACK/FPGA_RECONF/fade reaches CMD_RESET");
  check(mcu.commands.size()>=4,"MCU commands captured");
  std::cout<<"PASS region="<<(pal_region?"PAL":"NTSC")<<" frames="<<frame<<" nmis="<<nmis
    <<" max_nmi_master_clocks="<<max_nmi_clocks<<" unsafe_vram_oam_writes="<<audit.unsafe_writes<<"\n";
}
