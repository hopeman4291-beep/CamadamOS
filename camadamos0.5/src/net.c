/* CamadamOS networking: PCI scan, Ethernet drivers (Intel e1000 for QEMU /
 * VirtualBox, Realtek RTL8101/8106/8168 for real laptops), and a small
 * TCP/IP stack: ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP and HTTP/1.0.
 * Everything is polled from the main loop; no network interrupts are used. */
#include "kernel.h"

netinfo_t net;

/* ======================= PCI ======================= */
/* ======================= drivers ======================= */
#define NRX 32
#define NTX 8
#define BUFSZ 2048
static int drv; /* 0 none, 1 e1000, 2 realtek */
static u8 rxbuf[NRX][BUFSZ] __attribute__((aligned(16)));
static u8 txbuf[NTX][BUFSZ] __attribute__((aligned(16)));
static int rx_cur, tx_cur;

/* ---- Intel e1000 (82540EM and friends) ---- */
typedef struct { u32 addr_lo, addr_hi; u16 len, csum; u8 status, errors; u16 special; } __attribute__((packed)) e1k_rx_t;
typedef struct { u32 addr_lo, addr_hi; u16 len; u8 cso, cmd, status, css; u16 special; } __attribute__((packed)) e1k_tx_t;
static e1k_rx_t e_rx[NRX] __attribute__((aligned(128)));
static e1k_tx_t e_tx[NTX] __attribute__((aligned(128)));
static volatile u8 *e_mmio;
static u32 er(u32 r){ return *(volatile u32*)(e_mmio+r); }
static void ew(u32 r,u32 v){ *(volatile u32*)(e_mmio+r)=v; }
static void e1k_init(u32 bar){
    e_mmio=(volatile u8*)(bar&~0xFu);
    ew(0x00D8,0xFFFFFFFF);                       /* mask all interrupts */
    ew(0x0000, er(0x0000)|(1u<<26));             /* reset */
    for(volatile int i=0;i<200000;i++);
    ew(0x00D8,0xFFFFFFFF);
    ew(0x0000, (er(0x0000)|(1<<6)|(1<<5)) & ~(1u<<3) & ~(1u<<31)); /* link up, auto speed */
    u32 lo=er(0x5400), hi=er(0x5404);
    if(!lo && !(hi&0xFFFF)){ /* read from EEPROM */
        for(int i=0;i<3;i++){ ew(0x0014,1|(i<<8)); u32 v; int t=0; while(!((v=er(0x0014))&(1<<4)) && t++<100000); net.mac[i*2]=(v>>16)&255; net.mac[i*2+1]=v>>24; }
    } else { for(int i=0;i<4;i++) net.mac[i]=(lo>>(8*i))&255; net.mac[4]=hi&255; net.mac[5]=(hi>>8)&255; }
    for(int i=0;i<128;i++) ew(0x5200+i*4,0);     /* clear multicast table */
    for(int i=0;i<NRX;i++){ e_rx[i].addr_lo=(u32)rxbuf[i]; e_rx[i].addr_hi=0; e_rx[i].status=0; }
    ew(0x2800,(u32)e_rx); ew(0x2804,0); ew(0x2808,sizeof e_rx); ew(0x2810,0); ew(0x2818,NRX-1);
    ew(0x0100,(1<<1)|(1<<15)|(1<<26)|(1<<4)); /* EN, broadcast, strip CRC, multicast promisc */
    for(int i=0;i<NTX;i++){ e_tx[i].addr_lo=(u32)txbuf[i]; e_tx[i].addr_hi=0; e_tx[i].status=1; e_tx[i].cmd=0; }
    ew(0x3800,(u32)e_tx); ew(0x3804,0); ew(0x3808,sizeof e_tx); ew(0x3810,0); ew(0x3818,0);
    ew(0x0410,0x0060200A);
    ew(0x0400,(1<<1)|(1<<3)|(0x10<<4)|(0x40<<12));
    rx_cur=0; tx_cur=0;
}
static int e1k_link(void){ return (er(0x0008)>>1)&1; }
static void e1k_send(const void *d,u16 n){
    e1k_tx_t *t=&e_tx[tx_cur];
    for(int i=0;i<100000 && !(t->status&1);i++);
    memcpy(txbuf[tx_cur],d,n);
    t->len=n; t->cmd=(1<<0)|(1<<1)|(1<<3); t->status=0;
    tx_cur=(tx_cur+1)%NTX; ew(0x3818,tx_cur);
}
static int e1k_recv(u8 **p){
    e1k_rx_t *r=&e_rx[rx_cur];
    if(!(r->status&1)) return -1;
    *p=rxbuf[rx_cur]; return r->len;
}
static void e1k_recv_done(void){
    e_rx[rx_cur].status=0; ew(0x2818,rx_cur); rx_cur=(rx_cur+1)%NRX;
}

/* ---- Realtek RTL8101/8102/8103/8105/8106/8107 (10EC:8136), 8168, 8169 ---- */
typedef struct { volatile u32 opts1, opts2, addr_lo, addr_hi; } rtl_desc_t;
static rtl_desc_t r_rx[NRX] __attribute__((aligned(256)));
static rtl_desc_t r_tx[NTX] __attribute__((aligned(256)));
static u16 rio;
#define R_OWN (1u<<31)
#define R_EOR (1u<<30)
#define R_FS  (1u<<29)
#define R_LS  (1u<<28)
static void rtl_init(u32 bar){
    rio=(u16)(bar&~3u);
    outb(rio+0x37,0x10);                          /* software reset */
    for(int i=0;i<1000000 && (inb(rio+0x37)&0x10);i++);
    for(int i=0;i<6;i++) net.mac[i]=inb(rio+i);
    outb(rio+0x50,0xC0);                          /* unlock config registers */
    outl(rio+0xF0, inl(rio+0xF0) & ~(1u<<19));    /* clear RXDV gate (newer chips) */
    outw(rio+0xE0, inw(rio+0xE0) | (1<<3));       /* C+ : PCI multiple read/write */
    outw(rio+0xDA, BUFSZ-1);                      /* max RX packet size */
    outb(rio+0xEC, 0x3B);                         /* max TX packet size */
    for(int i=0;i<NRX;i++){ r_rx[i].addr_lo=(u32)rxbuf[i]; r_rx[i].addr_hi=0; r_rx[i].opts2=0; r_rx[i].opts1=R_OWN|(i==NRX-1?R_EOR:0)|BUFSZ; }
    for(int i=0;i<NTX;i++){ r_tx[i].addr_lo=(u32)txbuf[i]; r_tx[i].addr_hi=0; r_tx[i].opts2=0; r_tx[i].opts1=(i==NTX-1?R_EOR:0); }
    outl(rio+0x20,(u32)r_tx); outl(rio+0x24,0);
    outl(rio+0xE4,(u32)r_rx); outl(rio+0xE8,0);
    outb(rio+0x37,0x0C);                          /* enable RX + TX */
    outl(rio+0x40,0x03000700);                    /* TX config: normal IFG, unlimited DMA burst */
    outl(rio+0x44,0x0000E70E);                    /* RX config: unlimited DMA, accept broadcast/multicast/my MAC */
    outl(rio+0x08,0xFFFFFFFF); outl(rio+0x0C,0xFFFFFFFF); /* multicast filter: accept all */
    outw(rio+0x3C,0);                             /* no interrupts, we poll */
    outw(rio+0x3E,0xFFFF);
    outb(rio+0x50,0x00);                          /* lock config */
    rx_cur=0; tx_cur=0;
}
static int rtl_link(void){ return (inb(rio+0x6C)>>1)&1; }
static void rtl_send(const void *d,u16 n){
    rtl_desc_t *t=&r_tx[tx_cur];
    for(int i=0;i<200000 && (t->opts1&R_OWN);i++);
    memcpy(txbuf[tx_cur],d,n);
    if(n<60){ memset(txbuf[tx_cur]+n,0,60-n); n=60; }
    t->opts2=0;
    t->opts1=R_OWN|R_FS|R_LS|(tx_cur==NTX-1?R_EOR:0)|n;
    outb(rio+0x38,0x40);                          /* poll the normal-priority TX queue */
    tx_cur=(tx_cur+1)%NTX;
}
static int rtl_recv(u8 **p){
    rtl_desc_t *r=&r_rx[rx_cur];
    if(r->opts1&R_OWN) return -1;
    int len=(int)(r->opts1&0x3FFF)-4;            /* minus CRC */
    *p=rxbuf[rx_cur];
    if(len<14||len>BUFSZ) return 0;
    return len;
}
static void rtl_recv_done(void){
    r_rx[rx_cur].opts2=0;
    r_rx[rx_cur].opts1=R_OWN|(rx_cur==NRX-1?R_EOR:0)|BUFSZ;
    rx_cur=(rx_cur+1)%NRX;
    outw(rio+0x3E,0xFFFF);
}

static void drv_send(const void *d,u16 n){
    if(drv==1) e1k_send(d,n); else if(drv==2) rtl_send(d,n); else return;
    net.tx_pkts++;
}

/* ======================= helpers ======================= */
static u16 get16(const u8 *p){ return (p[0]<<8)|p[1]; }
static u32 get32(const u8 *p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static void put16(u8 *p,u16 v){ p[0]=v>>8; p[1]=v; }
static void put32(u8 *p,u32 v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static u32 csum_add(u32 s,const u8 *p,int n){ for(int i=0;i+1<n;i+=2) s+=(p[i]<<8)|p[i+1]; if(n&1) s+=p[n-1]<<8; return s; }
static u16 csum_fin(u32 s){ while(s>>16) s=(s&0xFFFF)+(s>>16); return (u16)~s; }
void ip_to_str(u32 ip,char *o){
    char t[4]; o[0]=0;
    for(int i=3;i>=0;i--){ itoa10((ip>>(8*i))&255,t); strcat0(o,t,16); if(i) strcat0(o,".",16); }
}
int parse_ip(const char *s,u32 *ip){
    u32 v=0; int parts=0;
    while(parts<4){
        if(*s<'0'||*s>'9') return 0;
        int n=0; while(*s>='0'&&*s<='9'){ n=n*10+(*s++-'0'); if(n>255) return 0; }
        v=(v<<8)|n; parts++;
        if(parts<4){ if(*s!='.') return 0; s++; }
    }
    if(*s) return 0;
    *ip=v; return 1;
}
static const u8 bcast[6]={255,255,255,255,255,255};

/* ======================= Ethernet / ARP ======================= */
static u8 pkt[1600];
typedef struct { u32 ip; u8 mac[6]; } arp_t;
static arp_t arpc[16]; static int arp_n;
static void eth_send(const u8 *dst,u16 type,const u8 *payload,int n){
    memcpy(pkt,dst,6); memcpy(pkt+6,net.mac,6); put16(pkt+12,type);
    memcpy(pkt+14,payload,n);
    drv_send(pkt,14+n);
}
static void arp_store(u32 ip,const u8 *mac){
    for(int i=0;i<arp_n;i++) if(arpc[i].ip==ip){ memcpy(arpc[i].mac,mac,6); return; }
    int i = arp_n<16 ? arp_n++ : (int)(rand32()%16);
    arpc[i].ip=ip; memcpy(arpc[i].mac,mac,6);
}
static void arp_send(int op,const u8 *tmac,u32 tip){
    u8 a[28];
    put16(a,1); put16(a+2,0x0800); a[4]=6; a[5]=4; put16(a+6,op);
    memcpy(a+8,net.mac,6); put32(a+14,net.ip);
    memcpy(a+18,op==1?(const u8*)"\0\0\0\0\0\0":tmac,6); put32(a+24,tip);
    eth_send(op==1?bcast:tmac,0x0806,a,28);
}
static void arp_input(const u8 *a,int n){
    if(n<28) return;
    u32 sip=get32(a+14), tip=get32(a+24);
    if(sip) arp_store(sip,a+8);
    if(get16(a+6)==1 && net.ip && tip==net.ip) arp_send(2,a+8,sip);
}
static int arp_lookup(u32 ip,u8 *mac){
    for(int i=0;i<arp_n;i++) if(arpc[i].ip==ip){ memcpy(mac,arpc[i].mac,6); return 1; }
    return 0;
}
static int arp_resolve(u32 ip,u8 *mac){
    if(ip==0xFFFFFFFF){ memcpy(mac,bcast,6); return 1; }
    u32 hop = ((ip & net.mask)==(net.ip & net.mask)) ? ip : net.gw;
    if(arp_lookup(hop,mac)) return 1;
    for(int tries=0;tries<4;tries++){
        arp_send(1,0,hop);
        u32 t=ticks; while(ticks-t<500){ net_poll(); if(arp_lookup(hop,mac)) return 1; }
    }
    return 0;
}

/* ======================= IPv4 ======================= */
static u16 ip_id=1;
static int ip_send(u32 dst,u8 proto,const u8 *payload,int n){
    static u8 b[1600];
    u8 mac[6];
    if(!arp_resolve(dst,mac)) return 0;
    b[0]=0x45; b[1]=0; put16(b+2,20+n); put16(b+4,ip_id++); put16(b+6,0x4000); b[8]=64; b[9]=proto;
    put16(b+10,0); put32(b+12,net.ip); put32(b+16,dst);
    put16(b+10,csum_fin(csum_add(0,b,20)));
    memcpy(b+20,payload,n);
    eth_send(mac,0x0800,b,20+n);
    return 1;
}

/* ======================= ICMP (ping) ======================= */
static volatile int ping_got; static u16 ping_seq;
static void icmp_input(u32 src,const u8 *p,int n){
    if(n<8) return;
    if(p[0]==8){ /* echo request -> reply */
        static u8 r[1500]; if(n>1400) return;
        memcpy(r,p,n); r[0]=0; put16(r+2,0); put16(r+2,csum_fin(csum_add(0,r,n)));
        ip_send(src,1,r,n);
    } else if(p[0]==0 && get16(p+6)==ping_seq) ping_got=1;
}
int net_ping(u32 ip,u32 timeout){
    u8 p[40]; memset(p,0,sizeof p);
    ping_seq++; p[0]=8; put16(p+4,0xCA4D); put16(p+6,ping_seq);
    memcpy(p+8,"CamadamOS ping!!",16);
    put16(p+2,csum_fin(csum_add(0,p,24)));
    ping_got=0; u32 t=ticks;
    if(!ip_send(ip,1,p,24)) return -1;
    while(ticks-t<timeout){ net_poll(); if(ping_got) return (int)(ticks-t); }
    return -1;
}

/* ======================= UDP ======================= */
static int udp_send(u32 dst,u16 sport,u16 dport,const u8 *d,int n){
    static u8 b[1500];
    put16(b,sport); put16(b+2,dport); put16(b+4,8+n); put16(b+6,0);
    memcpy(b+8,d,n);
    return ip_send(dst,17,b,8+n);
}
static void dhcp_input(const u8 *p,int n);
static void dns_input(const u8 *p,int n);
static void udp_input(const u8 *p,int n){
    if(n<8) return;
    u16 dport=get16(p+2);
    if(dport==68) dhcp_input(p+8,n-8);
    else if(dport==53053) dns_input(p+8,n-8);
}

/* ======================= DHCP ======================= */
static u32 dhcp_xid, dhcp_offer, dhcp_server, dhcp_t;
static int dhcp_tries;
static void dhcp_send(int type){
    static u8 d[300]; memset(d,0,sizeof d);
    d[0]=1; d[1]=1; d[2]=6; put32(d+4,dhcp_xid); put16(d+10,0x8000);
    memcpy(d+28,net.mac,6);
    put32(d+236,0x63825363);
    int o=240;
    d[o++]=53; d[o++]=1; d[o++]=type;
    if(type==3){ d[o++]=50; d[o++]=4; put32(d+o,dhcp_offer); o+=4; d[o++]=54; d[o++]=4; put32(d+o,dhcp_server); o+=4; }
    d[o++]=12; d[o++]=9; memcpy(d+o,"camadamos",9); o+=9;
    d[o++]=55; d[o++]=3; d[o++]=1; d[o++]=3; d[o++]=6;
    d[o++]=255;
    u32 saved=net.ip; net.ip=0;
    udp_send(0xFFFFFFFF,68,67,d,o<300?300:o);
    net.ip=saved;
}
static void dhcp_input(const u8 *d,int n){
    if(n<240 || d[0]!=2 || get32(d+4)!=dhcp_xid || get32(d+236)!=0x63825363) return;
    int type=0; u32 mask=0,gw=0,dns=0,srv=0;
    for(int o=240;o<n && d[o]!=255;){
        if(d[o]==0){ o++; continue; }
        int c=d[o], l=d[o+1]; const u8 *v=d+o+2;
        if(c==53) type=v[0]; else if(c==1&&l>=4) mask=get32(v); else if(c==3&&l>=4) gw=get32(v);
        else if(c==6&&l>=4) dns=get32(v); else if(c==54&&l>=4) srv=get32(v);
        o+=2+l;
    }
    if(type==2 && net.dhcp==1){ dhcp_offer=get32(d+16); dhcp_server=srv; net.dhcp=2; dhcp_t=ticks; dhcp_send(3); }
    else if(type==5 && net.dhcp==2){
        net.ip=get32(d+16); net.mask=mask?mask:0xFFFFFF00; net.gw=gw; net.dns=dns?dns:gw; net.dhcp=3;
    } else if(type==6 && net.dhcp==2){ net.dhcp=1; dhcp_t=0; }
}
static void dhcp_tick(void){
    if(!net.present || net.dhcp==3) return;
    if(net.dhcp==0){ net.dhcp=1; dhcp_t=0; }
    if(dhcp_t==0 || ticks-dhcp_t>(u32)(net.dhcp==1?3000:4000)){
        if(net.dhcp==2 && ++dhcp_tries>3){ net.dhcp=1; dhcp_tries=0; }
        dhcp_t=ticks; if(!dhcp_t) dhcp_t=1;
        if(net.dhcp==1){ dhcp_xid=rand32(); dhcp_send(1); } else dhcp_send(3);
    }
}
int net_wait_ready(u32 ms){
    u32 t=ticks;
    while(ticks-t<ms){ net_poll(); if(net.dhcp==3) return 1; if(!net.present) return 0; }
    return net.dhcp==3;
}

/* ======================= DNS ======================= */
static volatile int dns_done; static u32 dns_ans; static u16 dns_id;
static void dns_input(const u8 *p,int n){
    if(n<12 || get16(p)!=dns_id) return;
    int qd=get16(p+4), an=get16(p+6), o=12;
    for(int i=0;i<qd;i++){ while(o<n && p[o]){ if((p[o]&0xC0)==0xC0){ o++; break; } o+=p[o]+1; } o+=5; }
    for(int i=0;i<an && o+10<=n;i++){
        if((p[o]&0xC0)==0xC0) o+=2; else { while(o<n && p[o]) o+=p[o]+1; o++; }
        u16 type=get16(p+o), rl=get16(p+o+8); o+=10;
        if(type==1 && rl==4 && o+4<=n){ dns_ans=get32(p+o); dns_done=1; return; }
        o+=rl;
    }
    dns_done=-1;
}
int dns_resolve(const char *host,u32 *ip){
    if(parse_ip(host,ip)) return 1;
    if(!net_wait_ready(8000)) return 0;
    static u8 q[300]; int o=12;
    for(int tries=0;tries<3;tries++){
        memset(q,0,12); dns_id=(u16)rand32(); put16(q,dns_id); put16(q+2,0x0100); put16(q+4,1);
        o=12;
        const char *h=host;
        while(*h && o<280){
            const char *dot=h; while(*dot && *dot!='.') dot++;
            int l=(int)(dot-h); if(l>63) return 0;
            q[o++]=l; memcpy(q+o,h,l); o+=l; h=*dot?dot+1:dot;
        }
        q[o++]=0; put16(q+o,1); put16(q+o+2,1); o+=4;
        dns_done=0;
        if(!udp_send(net.dns,53053,53,q,o)) continue;
        u32 t=ticks; while(ticks-t<2500 && !dns_done) net_poll();
        if(dns_done==1){ *ip=dns_ans; return 1; }
        if(dns_done==-1) return 0;
    }
    return 0;
}

/* ======================= TCP (several client connections) ======================= */
enum { T_CLOSED, T_SYN_SENT, T_EST, T_DONE };
#define NSOCK 6
typedef struct {
    int used, state; u32 rip; u16 lport, rport;
    u32 snd_nxt, snd_una, rcv_nxt;
    u8 *buf; u32 cap, len; int fin, rst; u32 last_rx;
} sock_t;
static sock_t socks[NSOCK];
static void tcp_send(sock_t *tc,u8 flags,const u8 *d,int n){
    static u8 b[1500]; int hl=20;
    put16(b,tc->lport); put16(b+2,tc->rport); put32(b+4,tc->snd_nxt); put32(b+8,tc->rcv_nxt);
    if(flags&0x02){ hl=24; b[20]=2; b[21]=4; put16(b+22,1400); }
    b[12]=(hl/4)<<4; b[13]=flags;
    u32 win=tc->cap>tc->len?tc->cap-tc->len:0; if(win>65535) win=65535;
    put16(b+14,(u16)win); put16(b+16,0); put16(b+18,0);
    if(n) memcpy(b+hl,d,n);
    u8 ph[12]; put32(ph,net.ip); put32(ph+4,tc->rip); ph[8]=0; ph[9]=6; put16(ph+10,hl+n);
    put16(b+16,csum_fin(csum_add(csum_add(0,ph,12),b,hl+n)));
    ip_send(tc->rip,6,b,hl+n);
}
static void tcp_input(u32 src,const u8 *p,int n){
    if(n<20) return;
    sock_t *tc=0;
    for(int i=0;i<NSOCK;i++){ sock_t *s=&socks[i]; if(s->used && s->state!=T_CLOSED && src==s->rip && get16(p)==s->rport && get16(p+2)==s->lport){ tc=s; break; } }
    if(!tc) return;
    u32 seq=get32(p+4), ack=get32(p+8); int hl=(p[12]>>4)*4; u8 fl=p[13];
    const u8 *d=p+hl; int dl=n-hl; if(dl<0) return;
    tc->last_rx=ticks;
    if(fl&0x04){ tc->rst=1; tc->state=T_DONE; return; }
    if(tc->state==T_SYN_SENT){
        if((fl&0x12)==0x12 && ack==tc->snd_nxt){ tc->rcv_nxt=seq+1; tc->snd_una=ack; tc->state=T_EST; tcp_send(tc,0x10,0,0); }
        return;
    }
    if(fl&0x10){ if((s32)(ack-tc->snd_una)>0) tc->snd_una=ack; }
    if(dl>0){
        if(seq==tc->rcv_nxt){
            u32 room=tc->cap-tc->len; u32 take=(u32)dl<room?(u32)dl:room;
            memcpy(tc->buf+tc->len,d,take); tc->len+=take; tc->rcv_nxt+=take;
        }
        tcp_send(tc,0x10,0,0);
    }
    if((fl&0x01) && seq+(u32)dl==tc->rcv_nxt && !tc->fin){
        tc->rcv_nxt++; tc->fin=1;
        if(tc->state==T_EST){ tcp_send(tc,0x11,0,0); tc->snd_nxt++; }
        tc->state=T_DONE;
    }
}
int tcp_open(u32 ip,u16 port,u32 bufcap){
    int id=-1; for(int i=0;i<NSOCK;i++) if(!socks[i].used){ id=i; break; }
    if(id<0) return -1;
    sock_t *tc=&socks[id]; memset(tc,0,sizeof *tc);
    tc->buf=kmalloc(bufcap); if(!tc->buf) return -1;
    tc->used=1; tc->cap=bufcap; tc->state=T_SYN_SENT; tc->rip=ip; tc->rport=port; tc->lport=40000+(u16)(rand32()%20000);
    tc->snd_nxt=rand32(); tc->snd_una=tc->snd_nxt;
    for(int tries=0;tries<5;tries++){
        tcp_send(tc,0x02,0,0); tc->snd_nxt++;
        u32 t=ticks; while(ticks-t<1500){ net_poll(); if(tc->state==T_EST) return id; if(tc->rst) break; }
        if(tc->rst) break;
        tc->snd_nxt--;
    }
    kfree(tc->buf); tc->used=0; return -1;
}
int tcp_write(int id,const void *data,int n){
    if(id<0||id>=NSOCK||!socks[id].used) return 0;
    sock_t *tc=&socks[id]; const u8 *d=data;
    while(n>0){
        if(tc->state!=T_EST) return 0;
        int chunk=n>1400?1400:n, ok=0;
        for(int tries=0;tries<5 && !ok;tries++){
            u32 start=tc->snd_nxt;
            tcp_send(tc,0x18,d,chunk); tc->snd_nxt+=chunk;
            u32 t=ticks; while(ticks-t<2000){ net_poll(); if((s32)(tc->snd_una-tc->snd_nxt)>=0){ ok=1; break; } if(tc->rst) return 0; }
            if(!ok) tc->snd_nxt=start;
        }
        if(!ok) return 0;
        d+=chunk; n-=chunk;
    }
    return 1;
}
int tcp_read(int id,void *out,int max){
    if(id<0||id>=NSOCK||!socks[id].used) return 0;
    sock_t *tc=&socks[id]; int n=imin(max,(int)tc->len);
    if(n<=0) return 0;
    u32 room_before=tc->cap-tc->len;
    memcpy(out,tc->buf,n); memmove(tc->buf,tc->buf+n,tc->len-n); tc->len-=n;
    if(room_before<4096 && tc->state==T_EST) tcp_send(tc,0x10,0,0); /* window update */
    return n;
}
int tcp_is_open(int id){ return id>=0 && id<NSOCK && socks[id].used && socks[id].state==T_EST && !socks[id].rst; }
int tcp_avail(int id){ return (id>=0 && id<NSOCK && socks[id].used) ? (int)socks[id].len : 0; }
void tcp_close(int id){
    if(id<0||id>=NSOCK||!socks[id].used) return;
    sock_t *tc=&socks[id];
    if(tc->state==T_EST){ tcp_send(tc,0x11,0,0); tc->snd_nxt++; }
    kfree(tc->buf); tc->used=0; tc->state=T_CLOSED;
}

/* ======================= IP input / poll ======================= */
static void ip_input(const u8 *p,int n){
    if(n<20 || (p[0]>>4)!=4) return;
    int hl=(p[0]&15)*4, tl=get16(p+2); if(tl>n) tl=n;
    u32 src=get32(p+12), dst=get32(p+16);
    if(net.ip && dst!=net.ip && dst!=0xFFFFFFFF) { if(!(p[9]==17 && net.dhcp!=3)) return; }
    if(get16(p+6)&0x3FFF) return; /* fragments not supported */
    const u8 *pl=p+hl; int pn=tl-hl;
    if(p[9]==1) icmp_input(src,pl,pn);
    else if(p[9]==17) udp_input(pl,pn);
    else if(p[9]==6) tcp_input(src,pl,pn);
}
void net_poll(void){
    if(!net.present) return;
    for(int k=0;k<16;k++){
        u8 *p; int n = drv==1 ? e1k_recv(&p) : rtl_recv(&p);
        if(n<0) break;
        if(n>=14){
            net.rx_pkts++;
            u16 type=get16(p+12);
            if(type==0x0806) arp_input(p+14,n-14);
            else if(type==0x0800) ip_input(p+14,n-14);
        }
        if(drv==1) e1k_recv_done(); else rtl_recv_done();
    }
    static u32 last_link;
    if(ticks-last_link>500){ last_link=ticks; net.link = drv==1 ? e1k_link() : rtl_link(); }
    dhcp_tick();
}

void net_init(void){
    memset(&net,0,sizeof net);
    strncpy0(net.chip,"No supported network card found",sizeof net.chip);
    for(int bus=0;bus<256 && !drv;bus++) for(int dev=0;dev<32 && !drv;dev++) for(int fn=0;fn<8 && !drv;fn++){
        u32 id=pci_read(bus,dev,fn,0);
        if((id&0xFFFF)==0xFFFF){ if(fn==0) break; else continue; }
        u32 cls=pci_read(bus,dev,fn,8)>>16;
        u16 ven=id&0xFFFF, did=id>>16;
        if(cls==0x0200){
            u32 cmd=pci_read(bus,dev,fn,4);
            pci_write(bus,dev,fn,4,(cmd&0xFFFF0000)|(cmd&0xFFFF)|0x7); /* I/O, memory, bus master */
            if(ven==0x8086){
                u32 bar=pci_read(bus,dev,fn,0x10);
                net.vendor=ven; net.device=did; drv=1;
                strncpy0(net.chip,"Intel PRO/1000 (e1000)",sizeof net.chip);
                e1k_init(bar);
            } else if(ven==0x10EC && (did==0x8136||did==0x8168||did==0x8169||did==0x8161)){
                u32 bar=pci_read(bus,dev,fn,0x10);
                if(!(bar&1)) bar=pci_read(bus,dev,fn,0x14);
                net.vendor=ven; net.device=did; drv=2;
                strncpy0(net.chip,did==0x8136?"Realtek RTL810x Fast Ethernet":"Realtek RTL8168/8169 Gigabit",sizeof net.chip);
                rtl_init(bar);
            } else if(!net.vendor){
                net.vendor=ven; net.device=did;
                strncpy0(net.chip,"Unsupported network card",sizeof net.chip);
            }
        }
        if(fn==0 && !(pci_read(bus,dev,0,0x0C)&0x00800000)) break; /* single-function device */
    }
    net.present = drv!=0;
}

/* ======================= HTTP/1.0 ======================= */
const char *http_err_str(int e){
    switch(e){
    case HTTP_ERR_URL: return "That address doesn't look right.";
    case HTTP_ERR_HTTPS: return "This site needs HTTPS (encryption), which CamadamOS can't do yet.";
    case HTTP_ERR_NONET: return "No network. Plug an Ethernet cable into the computer and try again.";
    case HTTP_ERR_DNS: return "Couldn't find that site (DNS lookup failed).";
    case HTTP_ERR_CONNECT: return "The site didn't answer (connection failed).";
    case HTTP_ERR_TIMEOUT: return "The site stopped responding.";
    case HTTP_ERR_MEM: return "Not enough memory for that page.";
    }
    return "Unknown error.";
}
static int lower_eq(const char *a,const char *b,int n){ for(int i=0;i<n;i++){ char x=a[i],y=b[i]; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return 0; } return 1; }
int http_get(const char *url,char **body,u32 *blen,char *location,int locn,char *ctype,int ctn,status_fn st){
    *body=0; *blen=0; if(location) location[0]=0; if(ctype) ctype[0]=0;
    if(!strncmp(url,"https://",8)) return HTTP_ERR_HTTPS;
    if(strncmp(url,"http://",7)) return HTTP_ERR_URL;
    char host[128]; const char *p=url+7; int hn=0;
    while(*p && *p!='/' && *p!=':' && hn<127) host[hn++]=*p++;
    host[hn]=0; if(!hn) return HTTP_ERR_URL;
    u16 port=80; if(*p==':'){ p++; port=0; while(*p>='0'&&*p<='9') port=port*10+(*p++-'0'); }
    const char *path=*p?p:"/";
    if(!net.present) return HTTP_ERR_NONET;
    if(st) st("Waiting for the network...");
    if(!net_wait_ready(10000)) return HTTP_ERR_NONET;
    char msg[160]; strncpy0(msg,"Looking up ",sizeof msg); strcat0(msg,host,sizeof msg); strcat0(msg,"...",sizeof msg);
    if(st) st(msg);
    u32 ip; if(!dns_resolve(host,&ip)) return HTTP_ERR_DNS;
    strncpy0(msg,"Connecting to ",sizeof msg); strcat0(msg,host,sizeof msg); strcat0(msg,"...",sizeof msg);
    if(st) st(msg);
    int id=tcp_open(ip,port,4*1024*1024);
    if(id<0) return HTTP_ERR_CONNECT;
    sock_t *tc=&socks[id];
    static char req[1024];
    strncpy0(req,"GET ",sizeof req); strcat0(req,path,sizeof req); strcat0(req," HTTP/1.0\r\nHost: ",sizeof req); strcat0(req,host,sizeof req);
    if(port!=80){ char pn[8]; itoa10(port,pn); strcat0(req,":",sizeof req); strcat0(req,pn,sizeof req); }
    strcat0(req,"\r\nUser-Agent: Mozilla/3.0 (compatible; CamadamOS " OS_VERSION ")\r\nAccept: */*\r\nConnection: close\r\n\r\n",sizeof req);
    if(!tcp_write(id,req,(int)strlen(req))){ tcp_close(id); return HTTP_ERR_CONNECT; }
    tc->last_rx=ticks; u32 last_report=0;
    while(tc->state==T_EST && !tc->rst && ticks-tc->last_rx<15000 && tc->len<tc->cap){
        net_poll();
        if(st && ticks-last_report>250){
            last_report=ticks; char kb[16]; itoa10((int)(tc->len/1024),kb);
            strncpy0(msg,"Receiving... ",sizeof msg); strcat0(msg,kb,sizeof msg); strcat0(msg," KB",sizeof msg); st(msg);
        }
    }
    if(!tc->fin && !tc->len){ tcp_close(id); return HTTP_ERR_TIMEOUT; }
    /* parse the response */
    char *r=(char*)tc->buf; u32 n=tc->len;
    int status=0; u32 i=0;
    if(n>12 && !strncmp(r,"HTTP/",5)){ while(i<n && r[i]!=' ') i++; i++; while(i<n && r[i]>='0'&&r[i]<='9') status=status*10+(r[i++]-'0'); }
    u32 hdr_end=0; int chunked=0;
    for(u32 k=0;k+3<n;k++) if(r[k]=='\r'&&r[k+1]=='\n'&&r[k+2]=='\r'&&r[k+3]=='\n'){ hdr_end=k+4; break; }
    if(!hdr_end){ for(u32 k=0;k+1<n;k++) if(r[k]=='\n'&&r[k+1]=='\n'){ hdr_end=k+2; break; } }
    for(u32 k=0;k<hdr_end;k++){
        if(r[k]=='\n'){
            char *h=r+k+1;
            if(location && lower_eq(h,"location:",9)){ char *v=h+9; while(*v==' ') v++; int m=0; while(v[m]&&v[m]!='\r'&&v[m]!='\n'&&m<locn-1){ location[m]=v[m]; m++; } location[m]=0; }
            if(ctype && lower_eq(h,"content-type:",13)){ char *v=h+13; while(*v==' ') v++; int m=0; while(v[m]&&v[m]!='\r'&&v[m]!='\n'&&v[m]!=';'&&m<ctn-1){ char c=v[m]; if(c>='A'&&c<='Z') c+=32; ctype[m]=c; m++; } ctype[m]=0; }
            if(lower_eq(h,"transfer-encoding:",18)){ char *v=h+18; while(*v==' ') v++; if(lower_eq(v,"chunked",7)) chunked=1; }
        }
    }
    u32 bl=n-hdr_end;
    char *out=kmalloc(bl+1); if(!out){ tcp_close(id); return HTTP_ERR_MEM; }
    if(chunked){
        u32 o=0, k=hdr_end;
        while(k<n){
            u32 sz=0; while(k<n){ char c=r[k]; int v=(c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1; if(v<0) break; sz=sz*16+v; k++; }
            while(k<n && r[k]!='\n') k++; k++;
            if(!sz) break;
            if(k+sz>n) sz=n-k;
            memcpy(out+o,r+k,sz); o+=sz; k+=sz+2;
        }
        bl=o;
    } else memcpy(out,r+hdr_end,bl);
    out[bl]=0;
    tcp_close(id);
    *body=out; *blen=bl;
    return status?status:200;
}
