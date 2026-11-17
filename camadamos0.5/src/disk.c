/* CamadamOS disk driver: AHCI (SATA) hard disks and SSDs */
#include "kernel.h"

disk_t disks[MAXDISKS]; int ndisks;
static u32 abar;
typedef struct { u32 dw0, prdbc, ctba, ctbau, rsv[4]; } cmdhdr_t;
typedef struct { u8 cfis[64]; u8 acmd[16]; u8 rsv[48]; struct { u32 dba, dbau, rsv, dbc; } prdt[8]; } cmdtbl_t;
typedef struct { cmdhdr_t *cl; u8 *fis; cmdtbl_t *tbl; } port_t;
static port_t ports[32];

static void *amalloc(u32 size,u32 align){ u8 *p=kzalloc(size+align); if(!p) return 0; return (void*)(((u32)p+align-1)&~(align-1)); }
static u32 preg(int p,int r){ return abar+0x100+p*0x80+r; }
static int port_stop(int p){
    mmio_w32(preg(p,0x18),mmio_r32(preg(p,0x18))&~1u);
    for(int t=0;t<500 && (mmio_r32(preg(p,0x18))&(1<<15));t++) sleep_ms(1);
    mmio_w32(preg(p,0x18),mmio_r32(preg(p,0x18))&~(1u<<4));
    for(int t=0;t<500 && (mmio_r32(preg(p,0x18))&(1<<14));t++) sleep_ms(1);
    return !(mmio_r32(preg(p,0x18))&((1<<15)|(1<<14)));
}
static void port_start(int p){
    for(int t=0;t<500 && (mmio_r32(preg(p,0x18))&(1<<15));t++) sleep_ms(1);
    mmio_w32(preg(p,0x18),mmio_r32(preg(p,0x18))|(1u<<4));
    mmio_w32(preg(p,0x18),mmio_r32(preg(p,0x18))|1u);
}
static int issue(int p,u8 cmd,u32 lba,u32 count,void *buf,u32 bytes,int write){
    port_t *pt=&ports[p];
    mmio_w32(preg(p,0x10),0xFFFFFFFF);                    /* clear interrupt status */
    cmdhdr_t *h=&pt->cl[0];
    int nprd=0; u32 left=bytes, addr=(u32)buf;
    cmdtbl_t *t=pt->tbl; memset(t,0,sizeof *t);
    while(left && nprd<8){ u32 chunk=left>0x400000?0x400000:left; t->prdt[nprd].dba=addr; t->prdt[nprd].dbau=0; t->prdt[nprd].dbc=chunk-1; addr+=chunk; left-=chunk; nprd++; }
    h->dw0=5|(write?(1<<6):0)|(nprd<<16); h->prdbc=0; h->ctba=(u32)t; h->ctbau=0;
    u8 *f=t->cfis;
    f[0]=0x27; f[1]=0x80; f[2]=cmd; f[3]=0;
    f[4]=lba; f[5]=lba>>8; f[6]=lba>>16; f[7]=0x40;
    f[8]=lba>>24; f[9]=0; f[10]=0; f[11]=0;
    f[12]=count; f[13]=count>>8;
    for(int tt=0;tt<1000 && (mmio_r32(preg(p,0x20))&0x88);tt++) sleep_ms(1);
    mmio_w32(preg(p,0x38),1);
    u32 t0=ticks;
    while(ticks-t0<10000){
        if(!(mmio_r32(preg(p,0x38))&1)) break;
        if(mmio_r32(preg(p,0x10))&(1u<<30)) return 0;     /* task file error */
    }
    if(mmio_r32(preg(p,0x38))&1) return 0;
    if(mmio_r32(preg(p,0x20))&1) return 0;               /* ERR bit */
    return 1;
}
int disk_read(int d,u32 lba,u32 count,void *buf){
    if(d<0||d>=ndisks) return 0;
    u8 *b=buf;
    while(count){ u32 n=count>128?128:count; if(!issue(disks[d].port,0x25,lba,n,b,n*512,0)) return 0; lba+=n; b+=n*512; count-=n; }
    return 1;
}
int disk_write(int d,u32 lba,u32 count,const void *buf){
    if(d<0||d>=ndisks) return 0;
    const u8 *b=buf;
    while(count){ u32 n=count>128?128:count; if(!issue(disks[d].port,0x35,lba,n,(void*)b,n*512,1)) return 0; lba+=n; b+=n*512; count-=n; }
    return 1;
}
int disk_flush(int d){ if(d<0||d>=ndisks) return 0; return issue(disks[d].port,0xEA,0,0,0,0,0); }

void disk_init(void){
    ndisks=0;
    int b,dv,f;
    if(!pci_find(1,6,1,0,&b,&dv,&f)) return;
    pci_enable(b,dv,f);
    abar=pci_bar_mem(b,dv,f,5); if(!abar) return;
    if(mmio_r32(abar+0x24)&1){                             /* BIOS/OS handoff */
        mmio_w32(abar+0x28,mmio_r32(abar+0x28)|2);
        for(int t=0;t<300 && (mmio_r32(abar+0x28)&1);t++) sleep_ms(1);
    }
    mmio_w32(abar+0x04,mmio_r32(abar+0x04)|(1u<<31));     /* AHCI mode */
    u32 pi=mmio_r32(abar+0x0C);
    for(int p=0;p<32 && ndisks<MAXDISKS;p++){
        if(!(pi&(1u<<p))) continue;
        u32 ssts=mmio_r32(preg(p,0x28));
        if((ssts&15)!=3) continue;
        if(mmio_r32(preg(p,0x24))!=0x00000101) continue;  /* SATA disk (not CD-ROM) */
        if(!port_stop(p)) continue;
        port_t *pt=&ports[p];
        pt->cl=amalloc(1024,1024); pt->fis=amalloc(256,256); pt->tbl=amalloc(sizeof(cmdtbl_t),128);
        if(!pt->cl||!pt->fis||!pt->tbl) continue;
        mmio_w32(preg(p,0x00),(u32)pt->cl); mmio_w32(preg(p,0x04),0);
        mmio_w32(preg(p,0x08),(u32)pt->fis); mmio_w32(preg(p,0x0C),0);
        mmio_w32(preg(p,0x30),0xFFFFFFFF); mmio_w32(preg(p,0x10),0xFFFFFFFF); mmio_w32(preg(p,0x14),0);
        port_start(p);
        u16 *id=amalloc(512,16);
        if(!issue(p,0xEC,0,0,id,512,0)) continue;
        disk_t *d=&disks[ndisks];
        d->port=p;
        u32 lba48 = id[100]|((u32)id[101]<<16); u32 lba28=id[60]|((u32)id[61]<<16);
        d->sectors = (id[83]&(1<<10)) && lba48 ? lba48 : lba28;
        if(id[102]||id[103]) d->sectors=0xFFFFFFFF;        /* > 2 TB: we use the first 2 TB */
        for(int i=0;i<20;i++){ d->model[i*2]=id[27+i]>>8; d->model[i*2+1]=id[27+i]&0xFF; }
        d->model[40]=0; for(int i=39;i>=0 && d->model[i]==' ';i--) d->model[i]=0;
        if(!d->sectors) continue;
        ndisks++;
    }
}
void disk_size_str(int d,char *o){
    u32 mb=disks[d].sectors/2048; char n[12];
    if(mb>=1024){ u32 g=mb*10/1024; itoa10((int)(g/10),n); strncpy0(o,n,16); if(g<1000){ strcat0(o,".",16); char t[2]={(char)('0'+g%10),0}; strcat0(o,t,16); } strcat0(o," GB",16); }
    else { itoa10((int)mb,n); strncpy0(o,n,16); strcat0(o," MB",16); }
}
