/* CamadamOS USB: xHCI host controller driver with HID boot-protocol
 * mouse and keyboard support. Polled from the main loop. */
#include "kernel.h"

usbinfo_t usb;
typedef struct { volatile u32 d0, d1, d2, d3; } trb_t;
typedef struct { trb_t *t; int n, idx, cycle; } ring_t;

static u32 xb, op, rt, db;           /* capability, operational, runtime, doorbell bases */
static int csz, maxports, maxslots;
static u64 *dcbaa;
static ring_t cmd;
static trb_t *evr; static int ev_idx, ev_cycle;
static u32 erst[4] __attribute__((aligned(64)));

static void *amalloc(u32 size,u32 align){ u8 *p=kzalloc(size+align); if(!p) return 0; u32 a=((u32)p+align-1)&~(align-1); return (void*)a; }
static void ring_init(ring_t *r,int n){ r->t=amalloc(n*16,64); r->n=n; r->idx=0; r->cycle=1; }
static trb_t *ring_put(ring_t *r,u32 d0,u32 d1,u32 d2,u32 ctrl){
    trb_t *t=&r->t[r->idx];
    t->d0=d0; t->d1=d1; t->d2=d2; t->d3=(ctrl&~1u)|r->cycle;
    if(++r->idx==r->n-1){
        trb_t *l=&r->t[r->idx]; l->d0=(u32)r->t; l->d1=0; l->d2=0; l->d3=(6<<10)|(1<<1)|r->cycle;
        r->idx=0; r->cycle^=1;
    }
    return t;
}
static void doorbell(int slot,int target){ mmio_w32(db+slot*4,target); }
static int next_event(trb_t *out){
    trb_t *e=&evr[ev_idx];
    if((e->d3&1)!=(u32)ev_cycle) return 0;
    out->d0=e->d0; out->d1=e->d1; out->d2=e->d2; out->d3=e->d3;
    if(++ev_idx==256){ ev_idx=0; ev_cycle^=1; }
    mmio_w32(rt+0x20+0x18,(u32)&evr[ev_idx]|8); mmio_w32(rt+0x20+0x1C,0);
    return 1;
}

/* ---------- devices ---------- */
#define MAXDEV 8
#define MAXEP 3
typedef struct {                         /* where the mouse fields sit inside a report */
    int ok, id; int btn_off, btn_n; int x_off, x_size, y_off, y_size, w_off, w_size; int sx, sy, sw;
} mlayout_t;
typedef struct {
    int dci, mps, kind, iface;           /* kind: 1 boot keyboard, 2 boot mouse, 3 mouse via report descriptor */
    ring_t ring; u8 *buf; u8 prev[8]; mlayout_t ml;
} uep_t;
typedef struct {
    int used, slot, port, speed, info, msc;
    ring_t ep0; u8 *out_ctx, *in_ctx;
    uep_t ep[MAXEP]; int nep;
} udev_t;
static udev_t devs[MAXDEV];
static void msc_attach(udev_t *d,int iface,int in_ep,int in_mps,int out_ep,int out_mps,int burst);
static void msc_detach(udev_t *d);
static void handle_report(uep_t *e,int len);
static void requeue(udev_t *d,uep_t *e){ ring_put(&e->ring,(u32)e->buf,0,e->mps,(1<<10)|(1<<5)|(1<<2)); doorbell(d->slot,e->dci); }
static void dispatch_transfer(trb_t *ev){
    int es=(ev->d3>>24)&255, dci=(ev->d3>>16)&31, cc=(ev->d2>>24)&255;
    for(int i=0;i<MAXDEV;i++){ udev_t *d=&devs[i]; if(!d->used || d->slot!=es) continue;
        for(int k=0;k<d->nep;k++){ uep_t *e=&d->ep[k]; if(e->dci!=dci) continue;
            if(cc==1||cc==13) handle_report(e,e->mps-(int)(ev->d2&0xFFFFFF));
            requeue(d,e); return; } }
}
/* wait for a specific event type, handling HID transfer events that arrive meanwhile */
static int wait_event(int type,int slot,trb_t *out,u32 ms){
    u32 t0=ticks;
    while(ticks-t0<ms){
        trb_t e;
        while(next_event(&e)){
            int et=(e.d3>>10)&63, es=(e.d3>>24)&255;
            if(et==type && (slot<0 || es==slot)){ if(out) *out=e; return 1; }
            if(et==32) dispatch_transfer(&e);
        }
        udelay(20);
    }
    return 0;
}
static int command(u32 d0,u32 d1,u32 d2,u32 ctrl,trb_t *ev){
    ring_put(&cmd,d0,d1,d2,ctrl); doorbell(0,0);
    trb_t e; if(!wait_event(33,-1,&e,1000)) return -1;
    if(ev) *ev=e;
    return (e.d2>>24)&255;                    /* completion code, 1 = success */
}
static u8 *ctx(u8 *base,int idx){ return base+idx*csz; }
static int control(udev_t *d,u8 rt_,u8 req,u16 val,u16 idx,u16 len,void *data){
    u32 trt = len ? ((rt_&0x80)?3:2) : 0;
    ring_put(&d->ep0,rt_|(req<<8)|(val<<16),idx|(len<<16),8,(2<<10)|(1<<6)|(trt<<16));
    if(len) ring_put(&d->ep0,(u32)data,0,len,(3<<10)|((rt_&0x80)?(1<<16):0));
    ring_put(&d->ep0,0,0,0,(4<<10)|(1<<5)|((len && (rt_&0x80))?0:(1<<16)));
    doorbell(d->slot,1);
    trb_t e; if(!wait_event(32,d->slot,&e,1000)) return 0;
    int cc=(e.d2>>24)&255; return cc==1||cc==13;
}
static void port_neutral_write(int p,u32 extra){
    u32 a=op+0x400+0x10*(p-1); u32 v=mmio_r32(a);
    u32 keep=v & (0x1|0x8|0x3C00|0x40000000|0x1E0|0x200|0xC000|0x0E000000);
    mmio_w32(a,keep|extra);
}
static int ep0_mps(int speed){ return speed==4?512:speed==3?64:8; }

/* ---------- HID report descriptor parser (finds mouse buttons, X, Y, wheel) ---------- */
static void parse_mouse(const u8 *r,int n,mlayout_t *m){
    memset(m,0,sizeof *m);
    int page=0, rsize=0, rcount=0, rid=0, lmin=0, in_mouse=0, depth=0, mouse_depth=-1;
    int usages[16], nus=0, umin=-1, umax=-1;
    int off[16]; int ids[16]; int nid=1; ids[0]=0; off[0]=0;
    int *cur=&off[0];
    for(int i=0;i<n;){
        u8 p=r[i]; int sz=p&3; if(sz==3) sz=4;
        if(p==0xFE){ i+=3+(i+1<n?r[i+1]:0); continue; }    /* long item */
        u32 v=0; for(int k=0;k<sz && i+1+k<n;k++) v|=(u32)r[i+1+k]<<(8*k);
        s32 sv = sz==1?(s8)v : sz==2?(s16)v : (s32)v;
        int tag=p&0xFC;
        switch(tag){
        case 0x04: page=v; break;                         /* usage page */
        case 0x14: lmin=sv; break;                        /* logical minimum */
        case 0x74: rsize=v; break;                        /* report size */
        case 0x94: rcount=v; break;                       /* report count */
        case 0x84: rid=v; { int f=-1; for(int k=0;k<nid;k++) if(ids[k]==rid) f=k; if(f<0 && nid<16){ ids[nid]=rid; off[nid]=8; f=nid++; } if(f>=0) cur=&off[f]; } break;
        case 0x08: if(nus<16) usages[nus++]= sz==4 ? (int)v : (int)(v|(page<<16)); break;
        case 0x18: umin=v; break;
        case 0x28: umax=v; break;
        case 0xA0: depth++; if(page==1 && nus && (usages[0]&0xFFFF)==2 && mouse_depth<0){ mouse_depth=depth; in_mouse=1; } nus=0; umin=umax=-1; break;
        case 0xC0: if(depth==mouse_depth){ in_mouse=0; mouse_depth=-1; } depth--; break;
        case 0x80:{                                        /* input item */
            int constant=v&1, relative=(v>>2)&1;
            for(int k=0;k<rcount;k++){
                int u;
                if(umin>=0) u=(page<<16)|(umin+k);
                else if(nus) u=usages[k<nus?k:nus-1];
                else u=page<<16;
                int up=u>>16, us=u&0xFFFF; if(up==0) up=page;
                if(!constant && in_mouse && !m->ok){
                    if(up==9 && us>=1 && us<=8 && !m->btn_n){ m->btn_off=*cur; m->btn_n=imin(rcount,3); m->id=rid; }
                    if(up==1 && us==0x30 && relative){ m->x_off=*cur; m->x_size=rsize; m->sx=lmin<0; m->id=rid; }
                    if(up==1 && us==0x31 && relative){ m->y_off=*cur; m->y_size=rsize; m->sy=lmin<0; }
                    if(up==1 && us==0x38){ m->w_off=*cur; m->w_size=rsize; m->sw=lmin<0; }
                }
                *cur+=rsize;
            }
            if(m->x_size && m->y_size && in_mouse) m->ok=1;
            nus=0; umin=umax=-1; break; }
        case 0x90: case 0xB0: nus=0; umin=umax=-1; break; /* output / feature: not part of input reports */
        }
        i+=1+sz;
    }
    if(m->x_size && m->y_size) m->ok=1;
}
static s32 bits_at(const u8 *b,int len,int off,int size,int sgn){
    u32 v=0; for(int i=0;i<size;i++){ int bit=off+i; if(bit/8>=len) break; v|=((u32)((b[bit/8]>>(bit%8))&1))<<i; }
    if(sgn && size<32 && (v>>(size-1))&1) v|=~0u<<size;
    return (s32)v;
}

static u8 port_busy[256];               /* 1 = a device on this port has been handled */
static u32 port_seen[256];               /* when a connection was first noticed (debounce) */
static void setup_device(int port){
    u32 ps=mmio_r32(op+0x400+0x10*(port-1));
    if(!(ps&1)) return;
    if(!(ps&2)){                                   /* USB2 port: reset to enable */
        port_neutral_write(port,1<<4);
        u32 t0=ticks; while(ticks-t0<200 && !(mmio_r32(op+0x400+0x10*(port-1))&(1<<21))) udelay(50);
        port_neutral_write(port,(1<<21)|(1<<17)|(1<<18));
        sleep_ms(20);
        ps=mmio_r32(op+0x400+0x10*(port-1));
        if(!(ps&2)) return;
    }
    int speed=(ps>>10)&15;
    int k=-1; for(int i=0;i<MAXDEV;i++) if(!devs[i].used){ k=i; break; }
    if(k<0) return;
    udev_t *d=&devs[k]; memset(d,0,sizeof *d);
    trb_t ev;
    port_busy[port]=1;
    if(command(0,0,0,9<<10,&ev)!=1) return;        /* Enable Slot */
    d->slot=(ev.d3>>24)&255; d->port=port; d->speed=speed;
    d->out_ctx=amalloc(32*csz,64); d->in_ctx=amalloc(33*csz,64);
    ring_init(&d->ep0,32);
    dcbaa[d->slot]=(u32)d->out_ctx;
    u32 *icc=(u32*)ctx(d->in_ctx,0); icc[1]=3;
    u32 *sl=(u32*)ctx(d->in_ctx,1); sl[0]=(speed<<20)|(1u<<27); sl[1]=port<<16;
    u32 *e0=(u32*)ctx(d->in_ctx,2); int mps=ep0_mps(speed);
    e0[1]=(3<<1)|(4<<3)|(mps<<16); e0[2]=(u32)d->ep0.t|1; e0[3]=0; e0[4]=8;
    if(command((u32)d->in_ctx,0,0,(11<<10)|(d->slot<<24),0)!=1) return;
    sleep_ms(10);
    u8 *desc=amalloc(64,64);
    if(!control(d,0x80,6,0x100,0,8,desc)) return;
    if(desc[7] && desc[7]!=mps && speed<3){          /* fix EP0 max packet size */
        mps=desc[7]; memset(d->in_ctx,0,33*csz);
        icc=(u32*)ctx(d->in_ctx,0); icc[1]=2;
        e0=(u32*)ctx(d->in_ctx,2); e0[1]=(3<<1)|(4<<3)|(mps<<16); e0[2]=(u32)&d->ep0.t[d->ep0.idx]|d->ep0.cycle; e0[4]=8;
        command((u32)d->in_ctx,0,0,(13<<10)|(d->slot<<24),0);
    }
    control(d,0x80,6,0x100,0,18,desc);
    d->info = usb.ndev<8 ? usb.ndev : -1;
    usbdev_info_t *info = usb.ndev<8 ? &usb.dev[usb.ndev++] : 0;
    if(info){ memset(info,0,sizeof *info); info->vid=desc[8]|(desc[9]<<8); info->pid=desc[10]|(desc[11]<<8); info->cls=desc[4]; info->speed=speed; }
    u8 *cfg=amalloc(512,64);
    if(!control(d,0x80,6,0x200,0,9,cfg)) return;
    int tl=cfg[2]|(cfg[3]<<8); if(tl>512) tl=512;
    if(!control(d,0x80,6,0x200,0,tl,cfg)) return;
    /* collect every HID interface with an interrupt IN endpoint */
    struct { int iface, sub, proto, ep, mps, interval, rdlen; } hid[MAXEP]; int nh=0;
    int cur_hid=-1, in_msc=0, ms_if=-1, ms_in=0, ms_out=0, ms_inmps=0, ms_outmps=0, ms_burst=0;
    for(int p=0;p+1<tl && cfg[p];p+=cfg[p]){
        u8 t=cfg[p+1];
        if(t==4){ cur_hid=-1; in_msc = cfg[p+5]==8 && cfg[p+6]==6 && cfg[p+7]==0x50 && ms_if<0; if(in_msc) ms_if=cfg[p+2]; if(cfg[p+5]==3 && nh<MAXEP){ cur_hid=nh++; hid[cur_hid].iface=cfg[p+2]; hid[cur_hid].sub=cfg[p+6]; hid[cur_hid].proto=cfg[p+7]; hid[cur_hid].ep=0; hid[cur_hid].rdlen=0; }
                   if(info && info->nif<4){ info->ifc[info->nif]=cfg[p+5]; info->ifs[info->nif]=cfg[p+6]; info->ifp[info->nif]=cfg[p+7]; info->nif++; } }
        else if(t==5 && in_msc && (cfg[p+3]&3)==2){
            int mp=(cfg[p+4]|(cfg[p+5]<<8))&0x7FF;
            if((cfg[p+2]&0x80) && !ms_in){ ms_in=cfg[p+2]&15; ms_inmps=mp; } else if(!(cfg[p+2]&0x80) && !ms_out){ ms_out=cfg[p+2]&15; ms_outmps=mp; } }
        else if(t==0x30 && in_msc) ms_burst=cfg[p+2];
        else if(t==0x21 && cur_hid>=0 && p+8<tl) hid[cur_hid].rdlen=cfg[p+7]|(cfg[p+8]<<8);
        else if(t==5 && cur_hid>=0 && !hid[cur_hid].ep && (cfg[p+2]&0x80) && (cfg[p+3]&3)==3){
            hid[cur_hid].ep=cfg[p+2]&15; hid[cur_hid].mps=(cfg[p+4]|(cfg[p+5]<<8))&0x7FF; hid[cur_hid].interval=cfg[p+6]; }
    }
    control(d,0x00,9,cfg[5],0,0,0);                 /* SET_CONFIGURATION */
    u8 *rd=amalloc(1024,64);
    for(int h=0;h<nh;h++){
        if(!hid[h].ep) continue;
        uep_t *e=&d->ep[d->nep];
        memset(e,0,sizeof *e);
        e->iface=hid[h].iface; e->dci=hid[h].ep*2+1; e->mps=imin(hid[h].mps,64);
        if(hid[h].sub==1 && hid[h].proto==1){       /* boot keyboard */
            control(d,0x21,0x0B,0,e->iface,0,0); control(d,0x21,0x0A,0,e->iface,0,0);
            e->kind=1;
        } else {
            int rl=imin(hid[h].rdlen?hid[h].rdlen:512,1024);
            memset(rd,0,1024);
            if(control(d,0x81,6,0x2200,e->iface,rl,rd)) parse_mouse(rd,rl,&e->ml);
            if(e->ml.ok){ e->kind=3; if(hid[h].sub==1) control(d,0x21,0x0B,1,e->iface,0,0); }  /* report protocol */
            else if(hid[h].sub==1 && hid[h].proto==2){ control(d,0x21,0x0B,0,e->iface,0,0); e->kind=2; }
            else continue;                           /* some other HID function (media keys etc.) */
            control(d,0x21,0x0A,0,e->iface,0,0);
        }
        int interval=hid[h].interval, iv;
        if(speed>=3) iv=clampi(interval-1,0,15);
        else { iv=3; int fr=interval?interval:1; while(fr>1 && iv<10){ fr>>=1; iv++; } }
        e->buf=amalloc(64,64); ring_init(&e->ring,32);
        e->ml.id = e->ml.id; /* keep */
        e->prev[0]=0;
        e->dci=hid[h].ep*2+1;
        e->iface=hid[h].iface;
        e->mps=imin(hid[h].mps?hid[h].mps:8,64);
        /* remember interval in the ring struct slot we don't use */
        e->ring.n = e->ring.n;
        d->ep[d->nep].kind=e->kind;
        hid[h].interval=iv;
        d->nep++;
    }
    if(!d->nep && ms_if>=0 && ms_in && ms_out){
        if(info) info->msc=1;
        msc_attach(d,ms_if,ms_in,ms_inmps,ms_out,ms_outmps,ms_burst); return; }
    if(!d->nep){ usb.other++; return; }
    memset(d->in_ctx,0,33*csz);
    icc=(u32*)ctx(d->in_ctx,0); icc[1]=1;
    int maxdci=1;
    for(int k2=0,h=0;k2<d->nep;k2++){
        uep_t *e=&d->ep[k2];
        while(h<nh && (!hid[h].ep || hid[h].ep*2+1!=e->dci)) h++;
        int iv = h<nh ? hid[h].interval : 3;
        icc[1]|=1u<<e->dci; if(e->dci>maxdci) maxdci=e->dci;
        u32 *ep=(u32*)ctx(d->in_ctx,e->dci+1);
        ep[0]=iv<<16; ep[1]=(3<<1)|(7<<3)|(e->mps<<16); ep[2]=(u32)e->ring.t|1; ep[3]=0; ep[4]=e->mps|(e->mps<<16);
        h=0;
    }
    sl=(u32*)ctx(d->in_ctx,1); sl[0]=(speed<<20)|((u32)maxdci<<27); sl[1]=port<<16;
    if(command((u32)d->in_ctx,0,0,(12<<10)|(d->slot<<24),0)!=1){ usb.other++; return; }
    d->used=1;
    for(int k2=0;k2<d->nep;k2++){
        uep_t *e=&d->ep[k2];
        if(e->kind==1) usb.keyboards++; else usb.mice++;
        if(info){ if(e->kind==1) info->kbd=1; else if(e->kind==2) info->mouse=1; else info->mouse=2; }
        requeue(d,e);
    }
}

/* ---------- HID reports ---------- */
static int key_held, key_shift; static u32 key_t0, key_rep;
static int usage_to_key(int u,int shift){
    static const char *lo="abcdefghijklmnopqrstuvwxyz1234567890", *hi="ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";
    if(u>=4 && u<=39) return (shift?hi:lo)[u-4];
    if(u==70) return KEY_PRTSC;
    switch(u){
    case 40: return '\n'; case 41: return KEY_ESC; case 42: return '\b'; case 43: return '\t'; case 44: return ' ';
    case 45: return shift?'_':'-'; case 46: return shift?'+':'='; case 47: return shift?'{':'['; case 48: return shift?'}':']';
    case 49: return shift?'|':'\\'; case 51: return shift?':':';'; case 52: return shift?'"':'\''; case 53: return shift?'~':'`';
    case 54: return shift?'<':','; case 55: return shift?'>':'.'; case 56: return shift?'?':'/';
    case 58: return KEY_F1; case 59: return KEY_F2; case 60: return KEY_F3; case 61: return KEY_F4; case 62: return KEY_F5; case 69: return KEY_F12;
    case 74: return KEY_HOME; case 76: return KEY_DEL; case 77: return KEY_END;
    case 79: return KEY_RIGHT; case 80: return KEY_LEFT; case 81: return KEY_DOWN; case 82: return KEY_UP;
    }
    return 0;
}
static void handle_report(uep_t *d,int len){
    u8 *r=d->buf;
    if(d->kind==3 && len>0){
        mlayout_t *m=&d->ml;
        if(m->id && r[0]!=m->id) return;             /* a different report (e.g. media keys) */
        usb.reports++;
        int btn=0; for(int i=0;i<m->btn_n;i++) if(bits_at(r,len,m->btn_off+i,1,0)) btn|=1<<i;
        int dx=bits_at(r,len,m->x_off,m->x_size,1), dy=bits_at(r,len,m->y_off,m->y_size,1);
        int wh=m->w_size?bits_at(r,len,m->w_off,m->w_size,1):0;
        dx=clampi(dx,-127,127); dy=clampi(dy,-127,127);
        cli(); mouse_input(dx,-dy,btn,-clampi(wh,-2,2)); sti();
    } else if(d->kind==2 && len>=3){
        usb.reports++;
        cli(); mouse_input((s8)r[1],-(s8)r[2],r[0]&7,len>=4?-(s8)r[3]:0); sti();
    } else if(d->kind==1 && len>=8){
        usb.reports++;
        int shift=(r[0]&0x22)!=0;
        for(int i=2;i<8;i++){
            int u=r[i]; if(u<4) continue;
            int was=0; for(int j=2;j<8;j++) if(d->prev[j]==u) was=1;
            if(!was){ int k=usage_to_key(u,shift); if((r[0]&0x11) && u>=4 && u<=29) k=KEY_CTRL('a'+u-4); if(k){ cli(); kbd_inject(k); sti(); key_held=u; key_shift=shift; key_t0=ticks; key_rep=0; } }
        }
        int still=0; for(int i=2;i<8;i++) if(r[i]==key_held) still=1;
        if(!still) key_held=0;
        memcpy(d->prev,r,8);
    }
}
static void port_gone(int port){
    for(int i=0;i<MAXDEV;i++){ udev_t *d=&devs[i]; if(!d->used || d->port!=port) continue;
        for(int k=0;k<d->nep;k++){ if(d->ep[k].kind==1) usb.keyboards--; else usb.mice--; }
        if(d->msc) msc_detach(d);
        if(d->info>=0){ usb.dev[d->info].kbd=0; usb.dev[d->info].mouse=0; usb.dev[d->info].gone=1; }
        command(0,0,0,(10<<10)|(d->slot<<24),0);   /* Disable Slot */
        d->used=0;
    }
    port_busy[port]=0; port_seen[port]=0;
}
static void scan_ports(void){
    for(int p=1;p<=maxports;p++){
        u32 v=mmio_r32(op+0x400+0x10*(p-1));
        if(v&(1<<17)) port_neutral_write(p,1<<17);          /* clear "connect status changed" */
        if(v&1){
            if(port_busy[p]) continue;
            if(!port_seen[p]){ port_seen[p]=ticks?ticks:1; continue; }
            if(ticks-port_seen[p]<150) continue;             /* let the connection settle */
            setup_device(p);
        } else if(port_busy[p]||port_seen[p]) port_gone(p);
    }
}
void usb_poll(void){
    if(!usb.present) return;
    trb_t e; int changed=0;
    while(next_event(&e)){ int t=(e.d3>>10)&63; if(t==32) dispatch_transfer(&e); else if(t==34) changed=1; }
    static u32 last_scan;
    if(changed || ticks-last_scan>500){ last_scan=ticks; scan_ports(); }
    if(key_held && ticks-key_t0>500 && ticks-key_rep>40){ key_rep=ticks; int k=usage_to_key(key_held,key_shift); if(k){ cli(); kbd_inject(k); sti(); } }
}

/* ---------- mass storage: Bulk-Only Transport + SCSI ---------- */
usbstick_t usbstick;
static struct { udev_t *d; int iface, in_dci, out_dci, in_ep, out_ep; ring_t rin, rout; u32 tag; u8 *bounce, *cbw, *csw; } ms;

static int wait_xfer(int slot,int dci,trb_t *out,u32 ms_){
    u32 t0=ticks;
    while(ticks-t0<ms_){
        trb_t e;
        while(next_event(&e)){
            int et=(e.d3>>10)&63, es=(e.d3>>24)&255, ed=(e.d3>>16)&31;
            if(et==32 && es==slot && ed==dci){ *out=e; return 1; }
            if(et==32) dispatch_transfer(&e);
        }
        udelay(10);
    }
    return 0;
}
static void ms_clear_halt(int in){
    int dci = in ? ms.in_dci : ms.out_dci; ring_t *r = in ? &ms.rin : &ms.rout;
    command(0,0,0,(14<<10)|(ms.d->slot<<24)|(dci<<16),0);                                   /* Reset Endpoint */
    command(((u32)&r->t[r->idx])|r->cycle,0,0,(16<<10)|(ms.d->slot<<24)|(dci<<16),0);       /* Set TR Dequeue Pointer */
    control(ms.d,0x02,1,0,in ? (0x80|ms.in_ep) : ms.out_ep,0,0);                            /* CLEAR_FEATURE(ENDPOINT_HALT) */
}
/* returns completion code; *got = bytes transferred */
static int bulk(int in,void *buf,u32 len,u32 *got){
    ring_t *r = in ? &ms.rin : &ms.rout; int dci = in ? ms.in_dci : ms.out_dci;
    ring_put(r,(u32)buf,0,len,(1<<10)|(1<<5)|(in?(1<<2):0));
    doorbell(ms.d->slot,dci);
    trb_t e;
    if(!wait_xfer(ms.d->slot,dci,&e,5000)){
        command(0,0,0,(15<<10)|(ms.d->slot<<24)|(dci<<16),0);    /* Stop Endpoint */
        trb_t e2; wait_xfer(ms.d->slot,dci,&e2,50);
        command(((u32)&r->t[r->idx])|r->cycle,0,0,(16<<10)|(ms.d->slot<<24)|(dci<<16),0);
        return -1;
    }
    int cc=(e.d2>>24)&255;
    if(got) *got = len - (e.d2&0xFFFFFF);
    return cc;
}
static void ms_reset_recovery(void){
    control(ms.d,0x21,0xFF,0,ms.iface,0,0);
    ms_clear_halt(1); ms_clear_halt(0);
}
/* run a SCSI command; data goes through the bounce buffer. returns 0 ok, 1 failed, -1 transport error */
static int scsi(const u8 *cb,int cblen,int in,u32 len){
    u8 *c=ms.cbw; memset(c,0,32);
    u32 tag=++ms.tag;
    c[0]=0x55; c[1]=0x53; c[2]=0x42; c[3]=0x43;
    memcpy(c+4,&tag,4); memcpy(c+8,&len,4);
    c[12]= in ? 0x80 : 0; c[13]=0; c[14]=cblen; memcpy(c+15,cb,cblen);
    int cc=bulk(0,c,31,0);
    if(cc==6){ ms_reset_recovery(); return -1; }
    if(cc!=1) return -1;
    if(len){
        u32 got=0; cc=bulk(in,ms.bounce,len,&got);
        if(cc==6) ms_clear_halt(in);
        else if(cc!=1 && cc!=13) return -1;
    }
    u32 got=0;
    cc=bulk(1,ms.csw,13,&got);
    if(cc==6){ ms_clear_halt(1); cc=bulk(1,ms.csw,13,&got); }
    if(cc!=1 && cc!=13) return -1;
    u8 *w=ms.csw;
    if(got<13 || w[0]!=0x55 || w[1]!=0x53 || w[2]!=0x42 || w[3]!=0x53){ ms_reset_recovery(); return -1; }
    if(w[12]==2){ ms_reset_recovery(); return -1; }
    return w[12] ? 1 : 0;
}
static int sense(void){ u8 cb[6]={0x03,0,0,0,18,0}; scsi(cb,6,1,18); return ms.bounce[2]&15; }
static int rw10(int write,u32 lba,u32 n){
    u8 cb[10]={write?0x2A:0x28,0,(u8)(lba>>24),(u8)(lba>>16),(u8)(lba>>8),(u8)lba,0,(u8)(n>>8),(u8)n,0};
    for(int tries=0;tries<3;tries++){
        int r=scsi(cb,10,!write,n*512);
        if(r==0) return 1;
        if(r==1) sense();
    }
    return 0;
}
int usb_msc_read(u32 lba,u32 count,void *buf){
    if(!usbstick.present) return 0;
    u8 *b=buf;
    while(count){ u32 n=count>128?128:count; if(!rw10(0,lba,n)) return 0; memcpy(b,ms.bounce,n*512); b+=n*512; lba+=n; count-=n; }
    return 1;
}
int usb_msc_write(u32 lba,u32 count,const void *buf){
    if(!usbstick.present) return 0;
    const u8 *b=buf;
    while(count){ u32 n=count>128?128:count; memcpy(ms.bounce,b,n*512); if(!rw10(1,lba,n)) return 0; b+=n*512; lba+=n; count-=n; }
    return 1;
}
static void trimcpy(char *o,const u8 *s,int n){ memcpy(o,s,n); o[n]=0; for(int i=n-1;i>=0 && o[i]==' ';i--) o[i]=0; }
static void msc_attach(udev_t *d,int iface,int in_ep,int in_mps,int out_ep,int out_mps,int burst){
    /* one stick at a time; a stick we can't read (e.g. the CamadamOS boot stick) gives way to the new one */
    if(ms.d && ms.d->used && fat.mounted){ d->used=1; return; }
    if(ms.d) ms.d->msc=0;
    memset(&usbstick,0,sizeof usbstick);
    ms.d=d; ms.iface=iface; ms.in_ep=in_ep; ms.out_ep=out_ep; ms.in_dci=in_ep*2+1; ms.out_dci=out_ep*2;
    if(!ms.bounce){ ms.bounce=amalloc(65536,65536); ms.cbw=amalloc(64,64); ms.csw=amalloc(64,64); }
    if(!ms.bounce){ strncpy0(usbstick.status,"out of memory",sizeof usbstick.status); return; }
    ring_init(&ms.rin,64); ring_init(&ms.rout,64);
    memset(d->in_ctx,0,33*csz);
    u32 *icc=(u32*)ctx(d->in_ctx,0); icc[1]=1|(1u<<ms.in_dci)|(1u<<ms.out_dci);
    int maxdci = ms.in_dci>ms.out_dci ? ms.in_dci : ms.out_dci;
    if(d->speed<4) burst=0;
    u32 *ep=(u32*)ctx(d->in_ctx,ms.in_dci+1);
    ep[0]=0; ep[1]=(3<<1)|(6<<3)|(burst<<8)|((u32)in_mps<<16); ep[2]=(u32)ms.rin.t|1; ep[3]=0; ep[4]=1024;
    ep=(u32*)ctx(d->in_ctx,ms.out_dci+1);
    ep[0]=0; ep[1]=(3<<1)|(2<<3)|(burst<<8)|((u32)out_mps<<16); ep[2]=(u32)ms.rout.t|1; ep[3]=0; ep[4]=1024;
    u32 *sl=(u32*)ctx(d->in_ctx,1); sl[0]=((u32)d->speed<<20)|((u32)maxdci<<27); sl[1]=d->port<<16;
    if(command((u32)d->in_ctx,0,0,(12<<10)|(d->slot<<24),0)!=1){ usb.other++; ms.d=0; return; }
    d->used=1; d->msc=1;
    usbstick.present=1; usbstick.port=d->port;
    strncpy0(usbstick.status,"starting",sizeof usbstick.status);
    /* INQUIRY */
    u8 inq[6]={0x12,0,0,0,36,0};
    if(scsi(inq,6,1,36)==0){ trimcpy(usbstick.vendor,ms.bounce+8,8); trimcpy(usbstick.product,ms.bounce+16,16); }
    /* wait until the medium is ready */
    int ok=0;
    for(int i=0;i<40;i++){
        u8 tur[6]={0,0,0,0,0,0};
        int r=scsi(tur,6,0,0);
        if(r==0){ ok=1; break; }
        if(r==1) sense();
        sleep_ms(100);
    }
    u8 cap[10]={0x25,0,0,0,0,0,0,0,0,0};
    if(ok && scsi(cap,10,1,8)==0){
        u8 *c=ms.bounce;
        usbstick.sectors=((u32)c[0]<<24|(u32)c[1]<<16|(u32)c[2]<<8|c[3])+1;
        usbstick.bsize=(u32)c[4]<<24|(u32)c[5]<<16|(u32)c[6]<<8|c[7];
    }
    if(!usbstick.sectors){ strncpy0(usbstick.status,"no medium / not ready",sizeof usbstick.status); return; }
    if(usbstick.bsize!=512){ strncpy0(usbstick.status,"unsupported sector size",sizeof usbstick.status); return; }
    strncpy0(usbstick.status,"ready",sizeof usbstick.status);
    usbstick.changed++;
    fat_mount();
}
static void msc_detach(udev_t *d){
    if(ms.d!=d) return;
    fat_unmount();
    usbstick.present=0; usbstick.changed++; strncpy0(usbstick.status,"removed",sizeof usbstick.status);
    ms.d=0;
}

/* ---------- controller ---------- */
static const char *ufail="";
static int xhci_start(int b,int d,int f){
    pci_enable(b,d,f);
    u32 id=pci_read(b,d,f,0);
    if((id&0xFFFF)==0x8086){                        /* Intel: route shared USB2/USB3 ports to xHCI */
        pci_write(b,d,f,0xD8,pci_read(b,d,f,0xDC));
        pci_write(b,d,f,0xD0,pci_read(b,d,f,0xD4));
    }
    xb=pci_bar_mem(b,d,f,0); if(!xb){ ufail="no BAR"; return 0; }
    u32 caplen=mmio_r8(xb), hcs1=mmio_r32(xb+4), hcs2=mmio_r32(xb+8), hcc1=mmio_r32(xb+0x10);
    op=xb+caplen; rt=xb+(mmio_r32(xb+0x18)&~0x1Fu); db=xb+(mmio_r32(xb+0x14)&~3u);
    maxslots=hcs1&255; maxports=(hcs1>>24)&255; csz=(hcc1&4)?64:32;
    /* take ownership from the BIOS */
    u32 xecp=(hcc1>>16)*4;
    while(xecp){
        u32 c=mmio_r32(xb+xecp);
        if((c&255)==1){
            mmio_w32(xb+xecp,c|(1u<<24));
            for(int t=0;t<1000 && (mmio_r32(xb+xecp)&(1u<<16));t++) sleep_ms(1);
            mmio_w32(xb+xecp,mmio_r32(xb+xecp)&~(1u<<16));
            mmio_w32(xb+xecp+4,0xE0000000u);           /* disable SMIs, clear status */
        }
        u32 nx=(c>>8)&255; if(!nx) break; xecp+=nx*4;
    }
    /* stop and reset */
    mmio_w32(op,mmio_r32(op)&~1u);
    for(int t=0;t<200 && !(mmio_r32(op+4)&1);t++) sleep_ms(1);
    mmio_w32(op,mmio_r32(op)|2);
    for(int t=0;t<1000 && ((mmio_r32(op)&2)||(mmio_r32(op+4)&(1<<11)));t++) sleep_ms(1);
    if(mmio_r32(op+4)&(1<<11)){ ufail="reset timeout"; return 0; }
    int slots=imin(maxslots,16);
    mmio_w32(op+0x38,slots);
    dcbaa=amalloc(256*8,64);
    int nsp=((hcs2>>27)&31)|(((hcs2>>21)&31)<<5);
    if(nsp){ u64 *sp=amalloc(nsp*8,64); for(int i=0;i<nsp;i++){ void *pg=amalloc(4096,4096); if(!pg){ ufail="memory"; return 0; } sp[i]=(u32)pg; } dcbaa[0]=(u32)sp; }
    mmio_w32(op+0x30,(u32)dcbaa); mmio_w32(op+0x34,0);
    ring_init(&cmd,64);
    mmio_w32(op+0x18,(u32)cmd.t|1); mmio_w32(op+0x1C,0);
    evr=amalloc(256*16,64); ev_idx=0; ev_cycle=1;
    erst[0]=(u32)evr; erst[1]=0; erst[2]=256; erst[3]=0;
    mmio_w32(rt+0x20+0x08,1);
    mmio_w32(rt+0x20+0x18,(u32)evr); mmio_w32(rt+0x20+0x1C,0);
    mmio_w32(rt+0x20+0x10,(u32)erst); mmio_w32(rt+0x20+0x14,0);
    mmio_w32(rt+0x20+0x00,0);                         /* no interrupts: we poll */
    mmio_w32(op,mmio_r32(op)|1);                      /* run */
    for(int t=0;t<100 && (mmio_r32(op+4)&1);t++) sleep_ms(1);
    sleep_ms(50);
    for(int p=1;p<=maxports;p++){ u32 v=mmio_r32(op+0x400+0x10*(p-1)); if(!(v&(1<<9))) port_neutral_write(p,1<<9); }
    sleep_ms(100);
    for(int round=0;round<6;round++){ scan_ports(); sleep_ms(100); }
    return 1;
}
void usb_init(int enabled){
    memset(&usb,0,sizeof usb);
    if(!enabled){ strncpy0(usb.status,"Turned off at boot",sizeof usb.status); return; }
    int b,d,f;
    for(int n=0;n<4;n++){
        if(!pci_find(0x0C,0x03,0x30,n,&b,&d,&f)) break;
        usb.controllers++;
        if(xhci_start(b,d,f)){ usb.present=1; break; }   /* one controller is enough for now */
    }
    if(!usb.controllers) strncpy0(usb.status,"No USB 3 (xHCI) controller found",sizeof usb.status);
    else if(!usb.present){ strncpy0(usb.status,"The USB controller didn't start: ",sizeof usb.status); strcat0(usb.status,ufail,sizeof usb.status); }
    else strncpy0(usb.status,"Running",sizeof usb.status);
}

/* one character per root port: '.' empty, 'c' connected, 'E' enabled; for diagnosis */
int usb_port_summary(char *o,int n){
    int k=0;
    for(int p=1;p<=maxports && k<n-1;p++){ u32 v=mmio_r32(op+0x400+0x10*(p-1)); o[k++]= !(v&1)?'.':(v&2)?'E':'c'; }
    o[k]=0; return k;
}
