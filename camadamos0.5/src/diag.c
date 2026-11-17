/* CamadamOS Mouse Test: shows the raw bytes coming from the mouse/touchpad,
 * so problems on real hardware can be diagnosed from a photo of the screen. */
#include "kernel.h"

static void hex2(u8 v,char *o){ const char *h="0123456789ABCDEF"; o[0]=h[v>>4]; o[1]=h[v&15]; o[2]=0; }
static void num(int v,char *o,int sign){ if(sign && v>=0){ *o++='+'; } itoa10(v,o); }
static const char *onoff(int v){ return v?"ON ":"OFF"; }

static void mt_draw(window_t *w){
    int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(250,250,253));
    draw_text_bold(14,10,"Mouse Test",RGB(90,40,170));
    draw_text(14,30,"Move your finger slowly to the RIGHT only, then take a photo.",RGB(60,60,80));
    char b[96], t[16];
    int y=56;
    strncpy0(b,"Reset: ",sizeof b); strcat0(b,ps2d.reset_ok?"OK":"FAILED",sizeof b);
    strcat0(b,"   Device ID: ",sizeof b); itoa10(ps2d.id,t); strcat0(b,t,sizeof b);
    strcat0(b,"   Config: ",sizeof b); hex2((u8)ps2d.cfg,t); strcat0(b,t,sizeof b);
    draw_text(14,y,b,RGB(30,30,45)); y+=18;
    strncpy0(b,"Packets ok: ",sizeof b); itoa10((int)ps2d.ok,t); strcat0(b,t,sizeof b);
    strcat0(b,"  bad start: ",sizeof b); itoa10((int)ps2d.badfirst,t); strcat0(b,t,sizeof b);
    strcat0(b,"  overflow: ",sizeof b); itoa10((int)ps2d.ovf,t); strcat0(b,t,sizeof b);
    strcat0(b,"  jumps: ",sizeof b); itoa10((int)ps2d.jump,t); strcat0(b,t,sizeof b);
    draw_text(14,y,b,RGB(30,30,45)); y+=26;

    /* raw bytes: 40 most recent, oldest first */
    draw_text_bold(14,y,"Raw bytes (M = mouse, K = keyboard, +ms since previous byte)",RGB(110,60,200)); y+=20;
    int n=imin(40,ps2d.rawn);
    for(int i=0;i<n;i++){
        int k=(ps2d.rawn-n+i)&63;
        int x=14+(i%8)*(cw-28)/8, yy=y+(i/8)*36;
        char hb[4]; hex2(ps2d.raw[k],hb);
        u32 col=ps2d.rawsrc[k]?RGB(30,30,45):RGB(200,100,40);
        char lbl[8]; lbl[0]=ps2d.rawsrc[k]?'M':'K'; lbl[1]=' '; lbl[2]=hb[0]; lbl[3]=hb[1]; lbl[4]=0;
        int bit3=(ps2d.raw[k]&8)!=0;
        if(ps2d.rawsrc[k] && bit3) fill_round_rect(x-3,yy-2,46,20,4,RGB(232,225,250));
        draw_text_bold(x,yy,lbl,col);
        char dt[12]; dt[0]='+'; itoa10(ps2d.rawdt[k],dt+1); draw_text(x,yy+16,dt,RGB(140,135,160));
    }
    y+=5*36+6;

    draw_text_bold(14,y,"Decoded packets (newest last)",RGB(110,60,200)); y+=20;
    int dn=imin(8,ps2d.dn);
    for(int i=0;i<dn;i++){
        int k=(ps2d.dn-dn+i)&7;
        int x=14+(i%4)*(cw-28)/4, yy=y+(i/4)*18;
        strncpy0(b,"x",sizeof b); num(ps2d.ddx[k],t,1); strcat0(b,t,sizeof b);
        strcat0(b," y",sizeof b); num(ps2d.ddy[k],t,1); strcat0(b,t,sizeof b);
        if(ps2d.dbtn[k]&1) strcat0(b," L",sizeof b);
        if(ps2d.dbtn[k]&2) strcat0(b," R",sizeof b);
        draw_text(x,yy,b,RGB(30,30,45));
    }
    y+=44;

    draw_text_bold(14,y,"Settings - press the key to change",RGB(110,60,200)); y+=20;
    char p[4]; itoa10(ps2d.pkt_size,p);
    strncpy0(b,"[3]/[4] packet size: ",sizeof b); strcat0(b,p,sizeof b);
    strcat0(b,"    [T] timing resync: ",sizeof b); strcat0(b,onoff(ps2d.resync),sizeof b);
    strcat0(b,"   [J] jump filter: ",sizeof b); strcat0(b,onoff(ps2d.filter),sizeof b);
    draw_text(14,y,b,RGB(30,30,45)); y+=18;
    strncpy0(b,"[S] swap X/Y: ",sizeof b); strcat0(b,onoff(ps2d.swap_xy),sizeof b);
    strcat0(b,"   [I] invert Y: ",sizeof b); strcat0(b,onoff(ps2d.invert_y),sizeof b);
    strcat0(b,"   [Z] reset touchpad   [C] clear",sizeof b);
    draw_text(14,y,b,RGB(30,30,45)); y+=24;

    strncpy0(b,"Last keyboard scancodes: ",sizeof b);
    int kn=imin(10,ps2d.kn);
    for(int i=0;i<kn;i++){ hex2(ps2d.kraw[(ps2d.kn-kn+i)&15],t); strcat0(b,t,sizeof b); strcat0(b," ",sizeof b); }
    draw_text(14,y,b,RGB(30,30,45));
    (void)ch;
}
static void mt_key(window_t *w,int k){
    (void)w;
    if(k>='A'&&k<='Z') k+=32;
    switch(k){
    case '3': ps2d.pkt_size=3; break;
    case '4': ps2d.pkt_size=4; break;
    case 't': ps2d.resync=!ps2d.resync; break;
    case 'j': ps2d.filter=!ps2d.filter; break;
    case 's': ps2d.swap_xy=!ps2d.swap_xy; break;
    case 'i': ps2d.invert_y=!ps2d.invert_y; break;
    case 'z': mouse_reinit(); wm_toast("Touchpad reset"); break;
    case 'c': ps2d.rawn=0; ps2d.dn=0; ps2d.ok=ps2d.badfirst=ps2d.ovf=ps2d.jump=0; break;
    }
}
static void mt_tick(window_t *w){ static u32 last; (void)w; if(ticks-last>=100){ last=ticks; wm_invalidate(); } }
void app_mousetest(void){
    window_t *w=wm_open("Mouse Test",640,imin(560,scr_h-90),IC_ABOUT); if(!w) return;
    w->draw=mt_draw; w->key=mt_key; w->tick=mt_tick;
}
