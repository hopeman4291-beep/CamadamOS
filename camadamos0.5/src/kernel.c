/* CamadamOS kernel core: CPU setup, interrupts, timer, keyboard, mouse,
 * clock, PC speaker, memory, and the boot sequence. */
#include "kernel.h"

/* ================= libc-ish helpers ================= */
void *memset(void *d, int c, size_t n){ u8 *p=d; while(n--) *p++=(u8)c; return d; }
void *memcpy(void *d, const void *s, size_t n){
    void *r=d;
    __asm__ volatile("cld; rep movsl; movl %3,%%ecx; rep movsb"
        : "+D"(d), "+S"(s), "=c"(n) : "r"(n&3), "2"(n>>2) : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n){
    u8 *dp=d; const u8 *sp=s;
    if(dp<sp) return memcpy(d,s,n);
    while(n--) dp[n]=sp[n];
    return d;
}
void memset32(u32 *d, u32 v, u32 n){ __asm__ volatile("cld; rep stosl":"+D"(d),"+c"(n):"a"(v):"memory"); }
int memcmp(const void *a,const void *b,size_t n){ const u8 *x=a,*y=b; for(size_t i=0;i<n;i++) if(x[i]!=y[i]) return x[i]-y[i]; return 0; }
size_t strlen(const char *s){ size_t n=0; while(s[n]) n++; return n; }
int strcmp(const char *a, const char *b){ while(*a && *a==*b){a++;b++;} return (u8)*a-(u8)*b; }
int strncmp(const char *a, const char *b, size_t n){ while(n && *a && *a==*b){a++;b++;n--;} return n?(u8)*a-(u8)*b:0; }
int atoi(const char *s){ while(*s==32) s++; int neg=0; if(*s==45){neg=1;s++;} int v=0; while(*s>=48&&*s<=57) v=v*10+(*s++-48); return neg?-v:v; }
void strncpy0(char *d, const char *s, size_t n){ size_t i=0; if(!n) return; while(i+1<n && s[i]){d[i]=s[i];i++;} d[i]=0; }
void strcat0(char *d, const char *s, size_t n){ size_t l=strlen(d); strncpy0(d+l,s,n>l?n-l:0); }
const char *strrchr0(const char *s, char c){ const char *r=0; while(*s){ if(*s==c) r=s; s++; } return r; }
void itoa10(int v, char *o){
    char t[12]; int i=0, neg=v<0; u32 u=neg?(u32)(-v):(u32)v;
    do{ t[i++]='0'+u%10; u/=10; }while(u);
    if(neg) *o++='-';
    while(i) *o++=t[--i];
    *o=0;
}
int ends_with(const char *s, const char *suf){ size_t a=strlen(s), b=strlen(suf); return a>=b && strcmp(s+a-b,suf)==0; }
static u32 rng=2463534242u;
void srand32(u32 s){ rng = s ? s : 2463534242u; }
u32 rand32(void){ rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
int rnd(int n){ return n<=0?0:(int)(rand32()%(u32)n); }

/* 64-bit division helpers (normally in libgcc) */
static u64 udivmod64(u64 n, u64 d, u64 *rem){
    u64 q=0, r=0;
    if(d==0){ if(rem) *rem=0; return 0; }
    for(int i=63;i>=0;i--){ r=(r<<1)|((n>>i)&1); if(r>=d){ r-=d; q|=(u64)1<<i; } }
    if(rem) *rem=r; return q;
}
u64 __udivmoddi4(u64 n,u64 d,u64 *r){ return udivmod64(n,d,r); }
s64 __divmoddi4(s64 n,s64 d,s64 *r){ int neg=(n<0)!=(d<0); u64 ur; u64 q=udivmod64(n<0?-(u64)n:(u64)n, d<0?-(u64)d:(u64)d,&ur); if(r) *r = n<0?-(s64)ur:(s64)ur; return neg?-(s64)q:(s64)q; }
u64 __udivdi3(u64 n,u64 d){ return udivmod64(n,d,0); }
u64 __umoddi3(u64 n,u64 d){ u64 r; udivmod64(n,d,&r); return r; }
s64 __divdi3(s64 n,s64 d){ int neg=(n<0)!=(d<0); u64 q=udivmod64(n<0?-(u64)n:(u64)n, d<0?-(u64)d:(u64)d,0); return neg?-(s64)q:(s64)q; }
s64 __moddi3(s64 n,s64 d){ u64 r; udivmod64(n<0?-(u64)n:(u64)n, d<0?-(u64)d:(u64)d,&r); return n<0?-(s64)r:(s64)r; }

/* ================= clipboard ================= */
static char *clip; static int clip_len;
void clip_set(const char *t,int n){
    if(n<0) n=(int)strlen(t);
    char *c=kmalloc(n+1); if(!c) return;
    memcpy(c,t,n); c[n]=0; kfree(clip); clip=c; clip_len=n;
}
const char *clip_get(void){ return clip?clip:""; }

/* ================= heap ================= */
#define HEAP_SIZE (48u*1024*1024)
static u8 heap_area[HEAP_SIZE] __attribute__((aligned(16)));
typedef struct blk { u32 size; u32 free; struct blk *next; u32 pad; } blk_t;
static blk_t *heap_head;
void heap_init(void){
    heap_head=(blk_t*)heap_area;
    heap_head->size=HEAP_SIZE-sizeof(blk_t); heap_head->free=1; heap_head->next=0;
}
void *kmalloc(u32 size){
    size=(size+15)&~15u;
    for(blk_t *b=heap_head;b;b=b->next){
        if(b->free && b->size>=size){
            if(b->size >= size+sizeof(blk_t)+64){
                blk_t *n=(blk_t*)((u8*)(b+1)+size);
                n->size=b->size-size-sizeof(blk_t); n->free=1; n->next=b->next;
                b->size=size; b->next=n;
            }
            b->free=0;
            return b+1;
        }
    }
    return 0;
}
void *kzalloc(u32 size){ void *p=kmalloc(size); if(p) memset(p,0,size); return p; }
void kfree(void *p){
    if(!p) return;
    blk_t *b=(blk_t*)p-1; b->free=1;
    for(blk_t *c=heap_head;c;c=c->next)
        while(c->free && c->next && c->next->free){ c->size+=sizeof(blk_t)+c->next->size; c->next=c->next->next; }
}
/* add a second, larger heap region from the BIOS memory map (above the kernel and GRUB modules) */
static void heap_add(u32 base,u32 len){
    blk_t *last=heap_head; while(last->next) last=last->next;
    blk_t *sent=(blk_t*)base; sent->size=0; sent->free=0;          /* never-free separator: blocks are only merged inside a region */
    blk_t *b=sent+1; b->size=len-2*sizeof(blk_t); b->free=1; b->next=0; sent->next=b;
    last->next=sent;
}
u32 heap_extra_mb;
static void heap_extend(u32 *mbw){
    u32 flags=mbw[0]; if(!(flags&(1<<6))) return;
    extern u8 _kernel_end[];
    u32 lo=(u32)_kernel_end;
    #define BUMP(a) do{ u32 _a=(u32)(a); if(_a>lo) lo=_a; }while(0)
    BUMP((u32)mbw+128); BUMP(mbw[12]+mbw[11]);                      /* info block, memory map */
    if(flags&(1<<2)) BUMP(mbw[4]+512);                               /* command line */
    if(flags&(1<<9)) BUMP(mbw[16]+128);                              /* boot loader name */
    if((flags&(1<<3)) && mbw[5]){ u32 *m=(u32*)mbw[6]; BUMP(mbw[6]+mbw[5]*16); for(u32 i=0;i<mbw[5];i++){ BUMP(m[i*4+1]); if(m[i*4+2]) BUMP(m[i*4+2]+256); } }
    #undef BUMP
    lo=((lo+0xFFFF)&~0xFFFFu)+0x10000;
    u32 fb=mbw[22];
    u32 best=0, bestlen=0, off=0;
    while(off<mbw[11]){
        u32 *e=(u32*)(mbw[12]+off);
        if(e[5]==1 && e[2]==0){
            u32 s=e[1], end = e[4] ? 0xF0000000u : e[1]+e[3];
            if(end<e[1] || end>0xF0000000u) end=0xF0000000u;
            if(s<lo) s=lo;
            if(fb>s && fb<end) end=fb;
            if(end>s && end-s>bestlen){ best=s; bestlen=end-s; }
        }
        off+=e[0]+4;
    }
    if(bestlen<(16u<<20)) return;
    if(bestlen>(1024u<<20)) bestlen=1024u<<20;
    heap_add(best,bestlen); heap_extra_mb=bestlen>>20;
}
u32 heap_free_bytes(void){ u32 t=0; for(blk_t *b=heap_head;b;b=b->next) if(b->free) t+=b->size; return t; }

/* ================= GDT / IDT ================= */
struct gdt_entry { u16 lim; u16 base_lo; u8 base_mid; u8 access; u8 gran; u8 base_hi; } __attribute__((packed));
struct dtr { u16 limit; u32 base; } __attribute__((packed));
struct idt_entry { u16 off_lo; u16 sel; u8 zero; u8 flags; u16 off_hi; } __attribute__((packed));
static struct gdt_entry gdt[3];
static struct idt_entry idt[256];
static struct dtr gdtr, idtr;
extern void gdt_flush(struct dtr*);
extern void idt_load(struct dtr*);
extern u32 isr_table[48];

static void gdt_set(int i, u8 access, u8 gran){
    gdt[i].lim=0xFFFF; gdt[i].base_lo=0; gdt[i].base_mid=0; gdt[i].base_hi=0;
    gdt[i].access=access; gdt[i].gran=gran;
}
static void gdt_init(void){
    memset(gdt,0,sizeof gdt);
    gdt_set(1,0x9A,0xCF); gdt_set(2,0x92,0xCF);
    gdtr.limit=sizeof(gdt)-1; gdtr.base=(u32)gdt;
    gdt_flush(&gdtr);
}
static void pic_remap(void){
    outb(0x20,0x11); io_wait(); outb(0xA0,0x11); io_wait();
    outb(0x21,0x20); io_wait(); outb(0xA1,0x28); io_wait();
    outb(0x21,4); io_wait(); outb(0xA1,2); io_wait();
    outb(0x21,1); io_wait(); outb(0xA1,1); io_wait();
    outb(0x21,~((1<<0)|(1<<1)|(1<<2)) & 0xFF);  /* timer, keyboard, cascade */
    outb(0xA1,~(1<<4) & 0xFF);                   /* PS/2 mouse (IRQ12) */
}
static void idt_init(void){
    memset(idt,0,sizeof idt);
    for(int i=0;i<48;i++){
        u32 a=isr_table[i];
        idt[i].off_lo=a&0xFFFF; idt[i].off_hi=a>>16; idt[i].sel=0x08; idt[i].flags=0x8E;
    }
    idtr.limit=sizeof(idt)-1; idtr.base=(u32)idt;
    idt_load(&idtr);
}

/* ================= timer ================= */
volatile u32 ticks;
static void pit_init(void){
    u32 div=1193;  /* ~1000 Hz */
    outb(0x43,0x36); outb(0x40,div&0xFF); outb(0x40,div>>8);
}
void sleep_ms(u32 ms){ u32 t=ticks; while(ticks-t<ms) hlt(); }

/* ================= PC speaker ================= */
void speaker_tone(u32 f){
    if(f<20){ speaker_off(); return; }
    u32 div=1193180/f;
    outb(0x43,0xB6); outb(0x42,div&0xFF); outb(0x42,(div>>8)&0xFF);
    u8 t=inb(0x61); if((t&3)!=3) outb(0x61,t|3);
}
void speaker_off(void){ outb(0x61, inb(0x61)&0xFC); }

/* ================= keyboard ================= */
static volatile int kq[64]; static volatile int kq_h, kq_t;
static int shift, caps, ext, ctrl;
static const char km[128]={0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
 'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',
 0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '};
static const char kms[128]={0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',
 0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '};
void kbd_inject(int k);
static void kq_push(int k){ int n=(kq_h+1)&63; if(n!=kq_t){ kq[kq_h]=k; kq_h=n; } }
void kbd_inject(int k){ kq_push(k); }
int kbd_get(void){ if(kq_h==kq_t) return -1; int k=kq[kq_t]; kq_t=(kq_t+1)&63; return k; }
static void kbd_byte(u8 sc){
    if(sc==0xE0){ ext=1; return; }
    if(ext){
        ext=0;
        if(sc==0x1D){ ctrl=1; return; }
        if(sc==0x9D){ ctrl=0; return; }
        if(sc==0x37){ kq_push(KEY_PRTSC); return; }
        if(sc&0x80) return;
        switch(sc){ case 0x48:kq_push(KEY_UP);break; case 0x50:kq_push(KEY_DOWN);break;
            case 0x4B:kq_push(KEY_LEFT);break; case 0x4D:kq_push(KEY_RIGHT);break;
            case 0x53:kq_push(KEY_DEL);break; case 0x47:kq_push(KEY_HOME);break; case 0x4F:kq_push(KEY_END);break;
            case 0x1C:kq_push('\n');break; }
        return;
    }
    if(sc==0x2A||sc==0x36){ shift=1; return; }
    if(sc==0xAA||sc==0xB6){ shift=0; return; }
    if(sc==0x1D){ ctrl=1; return; }
    if(sc==0x9D){ ctrl=0; return; }
    if(sc==0x3A){ caps=!caps; return; }
    if(sc&0x80) return;
    if(sc==0x01){ kq_push(KEY_ESC); return; }
    if(sc==0x3B){ kq_push(KEY_F1); return; }
    if(sc==0x3C){ kq_push(KEY_F2); return; }
    if(sc==0x3D){ kq_push(KEY_F3); return; }
    if(sc==0x3E){ kq_push(KEY_F4); return; }
    if(sc==0x3F){ kq_push(KEY_F5); return; }
    if(sc==0x58){ kq_push(KEY_F12); return; }
    if(sc<128){
        char c = shift ? kms[sc] : km[sc];
        if(ctrl && km[sc]>='a' && km[sc]<='z'){ kq_push(KEY_CTRL(km[sc])); return; }
        if(caps && c>='a'&&c<='z') c-=32; else if(caps && c>='A'&&c<='Z') c+=32;
        if(c) kq_push(c);
    }
}

/* ================= PS/2 mouse / touchpad =================
 * Works with real PS/2 mice and laptop touchpads, and with USB mice that the
 * firmware emulates as PS/2. The device is fully reset first, because a
 * touchpad can still be in a vendor-specific mode left over from Windows. */
volatile int mouse_x, mouse_y, mouse_btn;
int mouse_speed = 5;                 /* 1..10, pointer moves speed/8 px per count */
ps2diag_t ps2d;
static u8 mpkt[4]; static int mcycle;
static u32 mlast_byte, mlast_any;
static int acc_x, acc_y;
typedef struct { int x,y,btn,wheel; } mev_t;
static volatile mev_t mq[128]; static volatile int mq_h, mq_t;
static int mwait_w(void){ for(int i=0;i<200000;i++) if(!(inb(0x64)&2)) return 1; return 0; }
static int mwait_r_n(int n){ for(int i=0;i<n;i++) if(inb(0x64)&1) return 1; return 0; }
static void mflush(void){ for(int i=0;i<64 && (inb(0x64)&1);i++){ (void)inb(0x60); io_wait(); } }
static int mread(void){ if(!mwait_r_n(300000)) return -1; return inb(0x60); }
static int mread_long(void){ if(!mwait_r_n(4000000)) return -1; return inb(0x60); }
static int mcmd(u8 v){                 /* send a byte to the mouse, wait for ACK (0xFA) */
    for(int tries=0;tries<3;tries++){
        mwait_w(); outb(0x64,0xD4); mwait_w(); outb(0x60,v);
        for(int k=0;k<4;k++){ int r=mread(); if(r==0xFA) return 1; if(r==0xFE) break; if(r<0) break; }
    }
    return 0;
}
static void mouse_init(void){
    mflush();
    mwait_w(); outb(0x64,0xA8);                              /* enable mouse port */
    mwait_w(); outb(0x64,0x20); int st=mread(); if(st<0) st=0x47;
    st|=3; st&=~0x30;                                         /* IRQ1+IRQ12 on, both clocks on */
    mwait_w(); outb(0x64,0x60); mwait_w(); outb(0x60,(u8)st);
    ps2d.cfg=st;
    mflush();
    /* full reset: device answers FA (ack), then AA (self-test ok), then 00 (id) */
    ps2d.reset_ok=0;
    if(mcmd(0xFF)){ int a=mread_long(); int id=mread(); ps2d.reset_ok=(a==0xAA); (void)id; }
    mflush();
    mcmd(0xF5);                                               /* stop reporting while configuring */
    mcmd(0xF6);                                               /* defaults */
    mcmd(0xF3); mcmd(100);                                    /* 100 reports per second */
    mcmd(0xE8); mcmd(2);                                      /* 4 counts per mm */
    mcmd(0xE6);                                               /* 1:1 scaling */
    ps2d.id=-1; if(mcmd(0xF2)) ps2d.id=mread();
    mflush();
    mcmd(0xF4);                                               /* start reporting */
    mflush();
    mcycle=0;
}
void mouse_reinit(void){ cli(); mouse_init(); sti(); }
int mouse_get(int *x, int *y, int *btn, int *wheel){
    if(mq_h==mq_t) return 0;
    *x=mq[mq_t].x; *y=mq[mq_t].y; *btn=mq[mq_t].btn; *wheel=mq[mq_t].wheel;
    mq_t=(mq_t+1)&127; return 1;
}
void mouse_inject(int btn){            /* used by the keyboard mouse */
    int n=(mq_h+1)&127; mouse_btn=btn;
    if(n!=mq_t){ mq[mq_h].x=mouse_x; mq[mq_h].y=mouse_y; mq[mq_h].btn=btn; mq[mq_h].wheel=0; mq_h=n; }
}
static void mouse_packet(void){
    u8 b0=mpkt[0];
    if(b0&0xC0){ ps2d.ovf++; return; }                        /* overflow: ignore */
    int dx = (b0&0x10) ? (int)mpkt[1]-256 : mpkt[1];
    int dy = (b0&0x20) ? (int)mpkt[2]-256 : mpkt[2];
    int k=ps2d.dn&7; ps2d.ddx[k]=dx; ps2d.ddy[k]=dy; ps2d.dbtn[k]=b0&7; ps2d.dn++;
    if(ps2d.filter && (dx>160||dx<-160||dy>160||dy<-160)){ ps2d.jump++; return; }
    ps2d.ok++;
    int wh=0; if(ps2d.pkt_size==4){ s8 z=(s8)(mpkt[3]<<4)>>4; if(z>=-2 && z<=2) wh=z; }
    mouse_input(dx,dy,b0&7,wh);
}
void mouse_input(int dx,int dy,int btn,int wh){
    acc_x += dx*mouse_speed; acc_y += dy*mouse_speed;
    int mx=acc_x/8, my=acc_y/8; acc_x-=mx*8; acc_y-=my*8;
    if(ps2d.swap_xy){ int t=mx; mx=my; my=t; }
    if(ps2d.invert_y) my=-my;
    mouse_x=clampi(mouse_x+mx,0,scr_w-1);
    mouse_y=clampi(mouse_y-my,0,scr_h-1);
    int prevb=mouse_btn; mouse_btn=btn;
    if(btn==prevb && !wh && mq_h!=mq_t){                      /* coalesce plain moves */
        int last=(mq_h-1)&127;
        if(mq[last].btn==btn && !mq[last].wheel){ mq[last].x=mouse_x; mq[last].y=mouse_y; return; }
    }
    int n=(mq_h+1)&127;
    if(n!=mq_t){ mq[mq_h].x=mouse_x; mq[mq_h].y=mouse_y; mq[mq_h].btn=btn; mq[mq_h].wheel=wh; mq_h=n; }
}
static void mouse_byte(u8 b){
    u32 now=ticks;
    if(ps2d.resync && now-mlast_byte>30) mcycle=0;            /* device was idle: new packet */
    mlast_byte=now;
    if(mcycle==0 && (!(b&8) || (b&0xC0))){ ps2d.badfirst++; return; } /* byte 1 has bit 3 set */
    mpkt[mcycle++]=b;
    if(mcycle>=ps2d.pkt_size){ mcycle=0; mouse_packet(); }
}
/* The keyboard and mouse share data port 0x60. Status bit 5 says which
   device each byte came from, so both interrupts route bytes by that bit. */
static void ps2_drain(void){
    for(int i=0;i<16;i++){
        u8 st=inb(0x64);
        if(!(st&1)) break;
        u8 b=inb(0x60);
        u32 now=ticks; int k=ps2d.rawn&63;
        ps2d.raw[k]=b; ps2d.rawsrc[k]=(st&0x20)?1:0; ps2d.rawdt[k]=(u16)imin(9999,(int)(now-mlast_any)); ps2d.rawn++;
        mlast_any=now;
        if(st&0x20) mouse_byte(b); else { ps2d.kraw[ps2d.kn&15]=b; ps2d.kn++; kbd_byte(b); }
    }
}

/* ================= real-time clock ================= */
static u8 cmos(u8 r){ outb(0x70,r); return inb(0x71); }
static int bcd(int v, int binmode){ return binmode ? v : (v&15)+(v>>4)*10; }
void rtc_time(int *h, int *m, int *s){
    for(int i=0;i<10000 && (cmos(0x0A)&0x80);i++);
    u8 b=cmos(0x0B); int bin=b&4;
    int hh=cmos(0x04), pm=hh&0x80; hh&=0x7F;
    *s=bcd(cmos(0x00),bin); *m=bcd(cmos(0x02),bin); hh=bcd(hh,bin);
    if(!(b&2)){ if(pm) hh=(hh%12)+12; else if(hh==12) hh=0; }
    *h=hh;
}
void rtc_date(int *d, int *mo, int *y){
    for(int i=0;i<10000 && (cmos(0x0A)&0x80);i++);
    int bin=cmos(0x0B)&4;
    *d=bcd(cmos(0x07),bin); *mo=bcd(cmos(0x08),bin); *y=2000+bcd(cmos(0x09),bin);
}

/* ================= power ================= */
void reboot(void){
    cli();
    for(int i=0;i<100000 && (inb(0x64)&2);i++);
    outb(0x64,0xFE);
    struct dtr z={0,0}; idt_load(&z); __asm__ volatile("int $3"); /* triple fault fallback */
    for(;;) hlt();
}
void shutdown(void){
    outw(0x604,0x2000);   /* QEMU */
    outw(0xB004,0x2000);  /* Bochs / old QEMU */
    outw(0x4004,0x3400);  /* VirtualBox */
}

/* ================= interrupt dispatch ================= */
typedef struct { u32 gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,int_no,err,eip,cs,eflags; } regs_t;
static const char *exc_names[]={"Divide by zero","Debug","NMI","Breakpoint","Overflow","Bound range",
 "Invalid opcode","Device not available","Double fault","Coprocessor","Invalid TSS","Segment not present",
 "Stack fault","General protection fault","Page fault","Reserved","FPU error","Alignment check","Machine check",
 "SIMD error"};
static void hex(u32 v, char *o){ const char *h="0123456789ABCDEF"; o[0]='0';o[1]='x'; for(int i=0;i<8;i++) o[2+i]=h[(v>>(28-4*i))&15]; o[10]=0; }
void isr_handler(regs_t *r){
    u32 n=r->int_no;
    if(n<32){
        cli(); speaker_off();
        gfx_reset_clip(); gfx_ox=gfx_oy=0;
        fill_rect(0,0,scr_w,scr_h,RGB(30,60,140));
        draw_text_big(60,60,":(",RGB(255,255,255),6);
        draw_text_big(60,200,"CamadamOS ran into a problem.",RGB(255,255,255),2);
        draw_text(60,250,n<20?exc_names[n]:"CPU exception",RGB(255,255,255));
        char b[16]; hex(r->eip,b); draw_text(60,270,"At address:",RGB(200,210,255)); draw_text(160,270,b,RGB(255,255,255));
        draw_text(60,310,"Please restart your computer.",RGB(200,210,255));
        gfx_present();
        for(;;) hlt();
    }
    if(n==32){ ticks++; music_tick_ms(); sound_tick(); }
    else if(n==33 || n==44) ps2_drain();
    if(n>=40) outb(0xA0,0x20);
    if(n>=32) outb(0x20,0x20);
}

/* ================= multiboot ================= */
typedef struct {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4]; u32 mmap_length, mmap_addr, drives_length, drives_addr, config_table;
    u32 boot_loader_name, apm_table, vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_if_seg, vbe_if_off, vbe_if_len;
    u32 fb_addr_lo, fb_addr_hi, fb_pitch, fb_width, fb_height;
    u8 fb_bpp, fb_type; u16 pad; /* GRUB aligns the colour info to 4 bytes */
    u8 red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} __attribute__((packed)) mbinfo_t;
u32 mem_total_kb;

static void text_error(const char *msg){
    volatile u16 *v=(u16*)0xB8000;
    for(int i=0;i<80*25;i++) v[i]=0x1F20;
    for(int i=0;msg[i];i++) v[i]=0x1F00|(u8)msg[i];
    for(;;) hlt();
}

static void splash(void){
    u32 start=ticks;
    while(ticks-start<2200){
        int t=(int)(ticks-start);
        gradient_v(0,0,scr_w,scr_h,RGB(18,16,40),RGB(60,24,90));
        int cx=scr_w/2, cy=scr_h/2-40;
        /* logo: a saxophone-ish golden circle with a music note */
        fill_circle(cx,cy-40,46,RGB(255,190,60));
        fill_circle(cx,cy-40,38,RGB(40,24,70));
        draw_text_big(cx-12,cy-72,"C",RGB(255,190,60),4);
        draw_text_big(cx-text_width(OS_NAME)*3/2, cy+30, OS_NAME, RGB(255,255,255), 3);
        draw_text_center(0, cy+90, scr_w, "Starting up...", RGB(200,190,230));
        int bw=260, bx=cx-bw/2, by=cy+120;
        fill_round_rect(bx,by,bw,10,5,RGB(70,60,110));
        int p=imin(bw, t*bw/2000);
        if(p>10) fill_round_rect(bx,by,p,10,5,RGB(255,190,60));
        gfx_present();
        hlt();
    }
}

void kmain(u32 magic, mbinfo_t *mb){
    if(magic!=0x2BADB002) text_error("CamadamOS: not loaded by a Multiboot bootloader.");
    if(!(mb->flags & (1<<12)) || mb->fb_type!=1 || (mb->fb_bpp!=32 && mb->fb_bpp!=24))
        text_error("CamadamOS: no graphics mode available (need 24/32-bit framebuffer).");
    if(mb->flags & (1<<6)){  /* sum all usable RAM regions from the memory map */
        u32 off=0;
        while(off < mb->mmap_length){
            u32 *e=(u32*)(mb->mmap_addr+off); /* size, base_lo, base_hi, len_lo, len_hi, type */
            if(e[5]==1) mem_total_kb += (e[3]>>10) | (e[4]<<22);
            off += e[0]+4;
        }
    }
    if(!mem_total_kb && (mb->flags & 1)) mem_total_kb = mb->mem_upper + 1024;
    gdt_init();
    pic_remap();
    idt_init();
    pit_init();
    gfx_init(mb->fb_addr_lo, mb->fb_pitch, mb->fb_width, mb->fb_height, mb->fb_bpp, mb->red_pos, mb->green_pos, mb->blue_pos);
    heap_init();
    { u32 cr0; __asm__ volatile("mov %%cr0,%0":"=r"(cr0)); cr0=(cr0&~0xCu)|0x22u; __asm__ volatile("mov %0,%%cr0; fninit"::"r"(cr0)); }
    ps2d.pkt_size=3; ps2d.resync=1; ps2d.filter=1;
    mouse_x=scr_w/2; mouse_y=scr_h/2;
    mouse_init();
    while(inb(0x64)&1) (void)inb(0x60);
    sti();
    int h,m,s; rtc_time(&h,&m,&s); srand32((u32)(h*3600+m*60+s)*2654435761u + ticks);
    if((mb->flags & (1<<3)) && mb->mods_count){
        u32 *mod=(u32*)mb->mods_addr; install_img=(const u8*)mod[0]; install_len=mod[1]-mod[0];
    }
    { void *mbv=mb; heap_extend((u32*)mbv); }
    fs_init();
    disk_init();
    store_init();
    net_init();
    sound_init();
    int usb_on=1;
    if((mb->flags & (1<<2)) && mb->cmdline){ const char *c=(const char*)mb->cmdline; for(;*c;c++) if(!strncmp(c,"nousb",5)) usb_on=0; }
    usb_init(usb_on);
    splash();
    wm_run();
}
