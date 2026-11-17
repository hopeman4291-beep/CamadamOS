/* CamadamOS Settings: appearance, mouse & clock, network */
#include "kernel.h"

#define SIDE 150
typedef struct { int tab; char ping[64]; int name_focus; } set_t;
static const char *tabs[4]={"Appearance","Mouse & Clock","Network","Sound"};
static const char *accent_names[N_ACCENTS]={"Purple","Ocean blue","Forest green","Sunset orange","Cherry red","Graphite"};

static void wallpaper_thumb(int i,int x,int y,int w,int h){
    int a,b,c,d; gfx_get_clip(&a,&b,&c,&d);
    gfx_set_clip(gfx_ox+x,gfx_oy+y,w,h);
    switch(i){
    case 0: gradient_v(x,y,w,h,RGB(28,30,72),RGB(120,50,120)); draw_circle(x+w-20,y+h-14,30,RGB(200,140,110)); break;
    case 1: gradient_v(x,y,w,h,RGB(10,40,90),RGB(20,140,160)); for(int k=0;k<3;k++) hline(x+6,y+h/2+k*12,w-12,RGB(120,220,230)); break;
    case 2: gradient_v(x,y,w,h*2/3,RGB(70,40,120),RGB(255,140,70)); fill_rect(x,y+h*2/3,w,h-h*2/3,RGB(30,40,90)); fill_circle(x+w/2,y+h*2/3,14,RGB(255,210,120)); fill_rect(x,y+h*2/3,w,h-h*2/3,RGB(30,40,90)); break;
    case 3: gradient_v(x,y,w,h,RGB(20,60,40),RGB(80,140,80)); for(int k=0;k<4;k++){ int tx=x+10+k*24; for(int r=0;r<20;r++) hline(tx+10-r/2,y+h-26+r,r,RGB(20,50,30)); } break;
    case 4: fill_rect(x,y,w,h,RGB(0,128,128)); break;
    case 5: fill_rect(x,y,w,h,RGB(10,8,30)); fill_circle(x+w/2,y+h/2-4,14,RGB(255,110,80)); fill_rect(x,y+h/2,w,h/2,RGB(14,6,36)); for(int k=1;k<6;k++) hline(x,y+h/2+k*k*h/50,w,RGB(255,60,200)); for(int k=-4;k<=4;k++) draw_line(x+w/2+k*6,y+h/2,x+w/2+k*30,y+h,RGB(80,200,255)); break;
    }
    gfx_set_clip_raw(a,b,c,d);
}
static void st_draw(window_t *w){
    set_t *s=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(250,249,253));
    fill_rect(0,0,SIDE,ch,RGB(240,238,248)); vline(SIDE-1,0,ch,RGB(220,216,232));
    for(int i=0;i<4;i++){
        if(i==s->tab) fill_round_rect(8,14+i*40,SIDE-16,32,8,accent_light());
        draw_text(22,22+i*40,tabs[i],i==s->tab?RGB(255,255,255):RGB(50,50,70));
    }
    int x0=SIDE+20, y=18;
    if(s->tab==0){
        draw_text_bold(x0,y,"Wallpaper",RGB(40,40,60)); y+=24;
        for(int i=0;i<N_WALLPAPERS;i++){
            int tx=x0+(i%3)*134, ty=y+(i/3)*104;
            if(i==theme_wallpaper) fill_round_rect(tx-4,ty-4,128,82,6,accent_light());
            wallpaper_thumb(i,tx,ty,120,74);
            draw_text_center(tx,ty+78,120,wallpaper_names[i],RGB(60,60,80));
        }
        y+=2*104+10;
        draw_text_bold(x0,y,"Accent colour",RGB(40,40,60)); y+=24;
        for(int i=0;i<N_ACCENTS;i++){
            int tx=x0+i*66;
            if(i==theme_accent_idx) fill_circle(tx+20,y+20,22,RGB(40,40,60));
            fill_circle(tx+20,y+20,18,accent_colors[i]);
        }
        draw_text(x0,y+48,accent_names[theme_accent_idx],RGB(90,90,110));
        y+=78; draw_text_bold(x0,y+6,"Your name",RGB(40,40,60));
        fill_round_rect(x0+100,y,200,28,6,RGB(255,255,255)); draw_rect(x0+100,y,200,28,s->name_focus?accent_dark():RGB(190,186,204));
        draw_text(x0+108,y+6,user_name,RGB(30,30,45)); if(s->name_focus && (ticks/500)%2==0) vline(x0+108+(int)strlen(user_name)*8,y+5,18,RGB(30,30,45));
        draw_text(x0+310,y+6,"(lock screen)",RGB(130,130,150));
    } else if(s->tab==1){
        draw_text_bold(x0,y,"Mouse / touchpad speed",RGB(40,40,60)); y+=28;
        ui_button(x0,y,32,28,"-",0);
        for(int i=0;i<10;i++) fill_round_rect(x0+44+i*22,y+6,18,16,4,i<mouse_speed?accent_light():RGB(220,218,230));
        ui_button(x0+270,y,32,28,"+",0);
        y+=46; draw_text(x0,y,"Shortcuts: F2 slower, F3 faster, F4 keyboard mouse,",RGB(110,110,130)); y+=18;
        draw_text(x0,y,"F5 Mouse Test.",RGB(110,110,130)); y+=40;
        draw_text_bold(x0,y,"Clock",RGB(40,40,60)); y+=28;
        char b[48], n[8]; strncpy0(b,"Time zone offset: ",sizeof b); if(clock_offset>=0) strcat0(b,"+",sizeof b); itoa10(clock_offset,n); strcat0(b,n,sizeof b); strcat0(b," h",sizeof b);
        draw_text(x0,y+6,b,RGB(40,40,60));
        ui_button(x0+220,y,32,28,"-",0); ui_button(x0+258,y,32,28,"+",0);
        y+=40;
        draw_text(x0,y,"If the clock is wrong, change the offset. In Turkey",RGB(110,110,130)); y+=18;
        draw_text(x0,y,"that's usually +0 (PC set by Windows) or +3 (UTC).",RGB(110,110,130)); y+=30;
        ui_button(x0,y,160,28,clock_12h?"12-hour clock":"24-hour clock",0);
        draw_text(x0+172,y+6,"(click to switch)",RGB(110,110,130));
    } else if(s->tab==3){
        draw_text_bold(x0,y,"Sound",RGB(40,40,60)); y+=28;
        draw_text(x0,y,"Device",RGB(110,110,130)); draw_text(x0+110,y,snd.hda?snd.codec:"PC speaker (beeps only)",RGB(30,30,45)); y+=22;
        if(snd.hda){ char n[8]; itoa10(snd.pins,n); draw_text(x0,y,"Outputs",RGB(110,110,130)); draw_text(x0+110,y,n,RGB(30,30,45)); y+=22; }
        y+=12; draw_text_bold(x0,y,"Volume",RGB(40,40,60)); y+=26;
        ui_button(x0,y,32,28,"-",0);
        for(int i=0;i<10;i++) fill_round_rect(x0+44+i*22,y+6,18,16,4,i<snd.volume/10?accent_light():RGB(220,218,230));
        ui_button(x0+270,y,32,28,"+",0);
        y+=46; draw_text_bold(x0,y,"Music instrument",RGB(40,40,60)); y+=26;
        for(int i=0;i<INST_COUNT;i++) ui_button(x0+(i%3)*134,y+(i/3)*34,126,28,instrument_names[i],snd.instrument==i);
        y+=80; ui_button(x0,y,150,28,"Test sound",0);
        if(!snd.hda){ y+=40; draw_text(x0,y,"No HD Audio chip was found, so music uses the",RGB(110,110,130)); draw_text(x0,y+18,"PC speaker, which plays one beep at a time.",RGB(110,110,130)); }
    } else {
        draw_text_bold(x0,y,"Network",RGB(40,40,60)); y+=28;
        char b[100], ip[20];
        #define ROW(l,v) do{ draw_text(x0,y,l,RGB(110,110,130)); draw_text(x0+110,y,v,RGB(30,30,45)); y+=22; }while(0)
        ROW("Card",net.chip);
        if(net.present){
            const char *h="0123456789ABCDEF"; b[0]=0; for(int i=0;i<6;i++){ char t[4]={h[net.mac[i]>>4],h[net.mac[i]&15],i<5?':':0,0}; strcat0(b,t,sizeof b); }
            ROW("MAC address",b);
            ROW("Cable",net.link?"Connected":"Not connected");
            ROW("Status",net.dhcp==3?"Online":net.dhcp==2?"Getting an address...":"Looking for a DHCP server...");
            if(net.dhcp==3){ ip_to_str(net.ip,ip); ROW("IP address",ip); ip_to_str(net.gw,ip); ROW("Router",ip); ip_to_str(net.dns,ip); ROW("DNS server",ip); }
            char c[16]; itoa10((int)net.rx_pkts,c); strncpy0(b,c,sizeof b); strcat0(b," in / ",sizeof b); itoa10((int)net.tx_pkts,c); strcat0(b,c,sizeof b); strcat0(b," out",sizeof b);
            ROW("Packets",b);
            y+=10; ui_button(x0,y,180,28,"Test connection",0);
            draw_text(x0+192,y+6,s->ping,RGB(30,30,45));
        }
        y+=50;
        draw_text(x0,y,"CamadamOS supports wired Ethernet only (Intel e1000",RGB(110,110,130)); y+=18;
        draw_text(x0,y,"and Realtek RTL810x/8168 chips). Wi-Fi isn't supported.",RGB(110,110,130));
        #undef ROW
    }
}
static void st_mouse(window_t *w,int ev,int x,int y,int b){
    set_t *s=w->st; (void)b;
    if(ev!=EV_DOWN) return;
    for(int i=0;i<4;i++) if(hit(x,y,8,14+i*40,SIDE-16,32)){ s->tab=i; return; }
    int x0=SIDE+20;
    if(s->tab==0){
        for(int i=0;i<N_WALLPAPERS;i++) if(hit(x,y,x0+(i%3)*134,42+(i/3)*104,120,74)){ theme_wallpaper=i; theme_apply(); return; }
        int ay=42+2*104+10+24;
        for(int i=0;i<N_ACCENTS;i++) if(hit(x,y,x0+i*66,ay,40,40)){ theme_accent_idx=i; theme_apply(); return; }
        s->name_focus=hit(x,y,x0+100,ay+78,200,28);
    } else if(s->tab==1){
        int y0=46;
        if(hit(x,y,x0,y0,32,28) && mouse_speed>1) mouse_speed--;
        if(hit(x,y,x0+270,y0,32,28) && mouse_speed<10) mouse_speed++;
        for(int i=0;i<10;i++) if(hit(x,y,x0+44+i*22,y0,20,28)) mouse_speed=i+1;
        int cy=46+46+18+40+28;
        if(hit(x,y,x0+220,cy,32,28) && clock_offset>-12) clock_offset--;
        if(hit(x,y,x0+258,cy,32,28) && clock_offset<14) clock_offset++;
        if(hit(x,y,x0,cy+40+18+30,160,28)) clock_12h=!clock_12h;
    } else if(s->tab==3){
        int y0=18+28+22+(snd.hda?22:0)+12+26;
        if(hit(x,y,x0,y0,32,28) && snd.volume>10) snd.volume-=10;
        if(hit(x,y,x0+270,y0,32,28) && snd.volume<100) snd.volume+=10;
        for(int i=0;i<10;i++) if(hit(x,y,x0+44+i*22,y0,20,28)) snd.volume=(i+1)*10;
        int iy=y0+46+26;
        for(int i=0;i<INST_COUNT;i++) if(hit(x,y,x0+(i%3)*134,iy+(i/3)*34,126,28)){ snd.instrument=i; snd_note(523,i,300,100); snd_note(659,i,300,90); snd_note(784,i,300,90); }
        if(hit(x,y,x0,iy+80,150,28)){ static const u16 c[4]={523,659,784,1046}; for(int i=0;i<4;i++) snd_note(c[i],snd.instrument,500,100); if(!snd.hda) piano_note(880,300); }
    } else if(net.present){
        int ty=18+28+22*4+(net.dhcp==3?66:0)+22+10;
        if(hit(x,y,x0,ty,180,28)){
            if(net.dhcp!=3){ strncpy0(s->ping,"Not online yet.",sizeof s->ping); return; }
            strncpy0(s->ping,"Testing...",sizeof s->ping); wm_invalidate(); wm_pump();
            int ms=net_ping(net.gw,2000); char n[12];
            if(ms<0) strncpy0(s->ping,"Router didn't answer.",sizeof s->ping);
            else { itoa10(ms,n); strncpy0(s->ping,"Router answered in ",sizeof s->ping); strcat0(s->ping,n,sizeof s->ping); strcat0(s->ping," ms",sizeof s->ping); }
        }
    }
}
static void st_key(window_t *w,int k){
    set_t *s=w->st; if(!s->name_focus) return;
    int L=(int)strlen(user_name);
    if(k=='\n'||k==KEY_ESC){ s->name_focus=0; return; }
    if(k=='\b'){ if(L) user_name[L-1]=0; return; }
    if(k>=32 && k<127 && L<20){ user_name[L]=(char)k; user_name[L+1]=0; }
}
static void st_tick(window_t *w){ set_t *s=w->st; static u32 last; if((s->tab==2||s->name_focus) && ticks-last>500){ last=ticks; wm_invalidate(); } }
void app_settings(void){
    window_t *w=wm_open("Settings",SIDE+440,460,IC_SETTINGS); if(!w) return;
    set_t *s=kzalloc(sizeof *s);
    w->st=s; w->draw=st_draw; w->mouse=st_mouse; w->tick=st_tick; w->key=st_key;
}
