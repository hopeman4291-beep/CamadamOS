/* CamadamOS PCI configuration space access */
#include "kernel.h"
u32 pci_read(int bus,int dev,int fn,int off){
    outl(0xCF8, 0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xFC)); return inl(0xCFC);
}
void pci_write(int bus,int dev,int fn,int off,u32 v){
    outl(0xCF8, 0x80000000u|(bus<<16)|(dev<<11)|(fn<<8)|(off&0xFC)); outl(0xCFC,v);
}
/* find the n-th device with this class/subclass (and prog-if, or -1 for any) */
int pci_find(int cls,int sub,int progif,int n,int *ob,int *od,int *of){
    for(int bus=0;bus<256;bus++) for(int dev=0;dev<32;dev++) for(int fn=0;fn<8;fn++){
        u32 id=pci_read(bus,dev,fn,0);
        if((id&0xFFFF)==0xFFFF){ if(fn==0) break; else continue; }
        u32 c=pci_read(bus,dev,fn,8);
        if((int)(c>>24)==cls && (int)((c>>16)&255)==sub && (progif<0 || (int)((c>>8)&255)==progif)){
            if(n--==0){ *ob=bus; *od=dev; *of=fn; return 1; }
        }
        if(fn==0 && !(pci_read(bus,dev,0,0x0C)&0x00800000)) break;
    }
    return 0;
}
/* read a memory BAR (handles 64-bit BARs; we only use addresses below 4 GB) */
/* highest end address of any memory BAR below 4 GB (to find a free gap) */
static u32 top_of_bars(void){
    u32 top=0x80000000u;
    for(int bus=0;bus<256;bus++) for(int dev=0;dev<32;dev++) for(int fn=0;fn<8;fn++){
        u32 id=pci_read(bus,dev,fn,0);
        if((id&0xFFFF)==0xFFFF){ if(fn==0) break; else continue; }
        int hdr=(pci_read(bus,dev,fn,0x0C)>>16)&0x7F; int nbar = hdr==0 ? 6 : hdr==1 ? 2 : 0;
        for(int i=0;i<nbar;i++){
            u32 off=0x10+i*4, v=pci_read(bus,dev,fn,off);
            if(v&1) continue;
            int is64=((v>>1)&3)==2; u32 hi = is64 ? pci_read(bus,dev,fn,off+4) : 0;
            if(!hi && (v&~0xFu)){
                pci_write(bus,dev,fn,off,0xFFFFFFFF); u32 sz=~(pci_read(bus,dev,fn,off)&~0xFu)+1; pci_write(bus,dev,fn,off,v);
                u32 end=(v&~0xFu)+sz; if(end>top && end<=0xFEC00000u) top=end;
            }
            if(is64) i++;
        }
        if(fn==0 && !(pci_read(bus,dev,0,0x0C)&0x00800000)) break;
    }
    return top;
}
u32 pci_bar_mem(int b,int d,int f,int idx){
    u32 off=0x10+idx*4, v=pci_read(b,d,f,off);
    if(((v>>1)&3)==2 && pci_read(b,d,f,off+4)){
        /* 64-bit BAR placed above 4 GB (common with UEFI): move it below 4 GB.
           Only safe for devices on bus 0 (built into the chipset). */
        if(b!=0) return 0;
        u32 cmd=pci_read(b,d,f,4);
        pci_write(b,d,f,4,cmd&~2u);
        pci_write(b,d,f,off,0xFFFFFFFF); u32 sz=~(pci_read(b,d,f,off)&~0xFu)+1; 
        u32 base=(top_of_bars()+sz-1)&~(sz-1);
        if(!sz || base+sz>0xFEC00000u || base<0x80000000u){ pci_write(b,d,f,off,v); pci_write(b,d,f,4,cmd); return 0; }
        pci_write(b,d,f,off,base|(v&0xF)); pci_write(b,d,f,off+4,0);
        pci_write(b,d,f,4,cmd|2u);
        return base;
    }
    return v&~0xFu;
}
void pci_enable(int b,int d,int f){ u32 c=pci_read(b,d,f,4); pci_write(b,d,f,4,(c&0xFFFF0000)|(c&0xFFFF)|0x7); }
