// Headless bsnes-plus accuracy core, with an explicit simulated cartridge MCU.
#include <snes.hpp>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <cstring>
#include <cstdlib>
using namespace SNES;
static std::map<std::string,unsigned> symbols;
static unsigned frame=0, nmis=0, max_nmi_clocks=0, nmi_start=0;
static bool in_nmi=false, pal_region=false;
static void check(bool ok,const char* what) {
  if(!ok) {std::cerr<<"FAIL: "<<what<<" frame="<<frame<<"\n";std::exit(1);}
}
struct PPUAudit : MMIO {
  MMIO* target=nullptr;
  bool blank=true;
  unsigned unsafe_writes=0;
  uint8 mmio_read(unsigned a) override {return target->mmio_read(a);}
  void mmio_write(unsigned a,uint8 d) override {
    unsigned reg=a&0xffff;
    if(reg==0x2100) blank=d&0x80;
    if((reg==0x2118 || reg==0x2119 || reg==0x2104) && !blank && cpu.vcounter()>0 && cpu.vcounter()<225) unsafe_writes++;
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
  for(unsigned i=0;i<count;i++) {mcu.service();SNES::system.run();frame++;}
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
int main(int argc,char**argv) {
  if(argc!=4 && argc!=5) {std::cerr<<"usage: menu_harness ROM SYMBOLS OUTPUT_DIR [PAL]\n";return 2;}
  outdir=argv[3]; pal_region=argc==5;
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
      check(!in_nmi,"NMI must not reenter");in_nmi=true;nmis++;nmi_start=clock;
    }
    if(in_nmi && cpu.disassembler_read(pc)==0x40) {
      unsigned elapsed=(clock+(pal_region?312:262)*1364-nmi_start)%((pal_region?312:262)*1364);
      if(elapsed>max_nmi_clocks) max_nmi_clocks=elapsed;
      check(cpu.vcounter()>=225,"NMI must finish inside VBlank");
      in_nmi=false;
    }
  };
  frames(180);snap("boot.ppm");
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
  press(8);frames(10);snap("launch.ppm");
  check(mcu.launched_path=="/FXPAK Demo.sfc","correct selected ROM passed to MCU");
  check(mcu.reset_requested,"LOADROM/ACK/FPGA_RECONF/fade reaches CMD_RESET");
  check(mcu.commands.size()>=4,"MCU commands captured");
  std::cout<<"PASS region="<<(pal_region?"PAL":"NTSC")<<" frames="<<frame<<" nmis="<<nmis
    <<" max_nmi_master_clocks="<<max_nmi_clocks<<" unsafe_vram_oam_writes="<<audit.unsafe_writes<<"\n";
}
