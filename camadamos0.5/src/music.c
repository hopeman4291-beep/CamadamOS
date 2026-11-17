/* CamadamOS Music player, Piano and About */
#include "kernel.h"

/* ================= Music player ================= */
typedef struct { int sel, autoplay, last_playing; u32 last_anim; int wav; } mp_t;
#define LIST_Y 210
static void mp_draw(window_t *w){
    mp_t *m=w->st; int cw=win_cw(w), ch=win_ch(w);
    int playing=music_playing();
    gradient_v(0,0,cw,LIST_Y-10,RGB(50,25,90),RGB(25,15,50));
    int show = playing>=0 ? playing : m->sel;
    int wavp = snd_wav_playing() && m->wav>=0;
    draw_text(20,14,(playing>=0||wavp)?"NOW PLAYING":"READY",RGB(255,190,60));
    if(wavp){ draw_text_big(20,34,fs_nodes[m->wav].name,RGB(255,255,255),2); draw_text(20,70,"WAV sound file",RGB(200,185,235)); }
    else { draw_text_big(20,34,songs[show].title,RGB(255,255,255),2); draw_text(20,70,songs[show].composer,RGB(200,185,235)); }
    if(snd.hda){ ui_button(cw-170,8,160,24,instrument_names[snd.instrument],0); draw_text(cw-262,12,"Instrument",RGB(200,185,235)); }
    else draw_text(cw-8-text_width(snd.codec),12,snd.codec,RGB(200,185,235));
    /* visualiser */
    u32 f=music_current_freq();
    for(int i=0;i<28;i++){
        int h=4;
        if(wavp || (snd.hda && playing>=0)){ int lv=snd_level(); int target=(int)((i*37+ticks/40*(i%5+1))%90); h=imax(4,lv*target/40+4); }
        else if(playing>=0 && f){
            int target=(int)((f/4+i*37+ticks/40*(i%5+1))%90);
            int center=(int)((f-200)*28/1400); int d=iabs(i-center);
            h=imax(4,target*imax(0,12-d)/12+6);
        }
        int x=20+i*((cw-40)/28), bh=imin(h,80);
        fill_rect(x,170-bh,(cw-40)/28-3,bh,mix(RGB(255,190,60),RGB(240,90,160),i*255/28));
    }
    /* progress */
    int pr=music_progress();
    fill_round_rect(20,180,cw-40,6,3,RGB(80,60,120));
    if(playing>=0) fill_round_rect(20,180,imax(6,(cw-40)*pr/1000),6,3,RGB(255,190,60));
    /* controls */
    int bx=cw-196;
    ui_button(bx,96,56,30,"|<",0);
    ui_button(bx+62,96,68,30,(playing>=0||wavp)?"Stop":"Play",playing>=0||wavp);
    ui_button(bx+136,96,56,30,">|",0);
    draw_text(bx,134,m->autoplay?"[x] Play all":"[ ] Play all",RGB(220,210,240));
    /* playlist */
    fill_rect(0,LIST_Y-10,cw,ch-LIST_Y+10,RGB(250,249,253));
    for(int i=0;i<song_count;i++){
        int y=LIST_Y+i*28;
        if(i==playing) fill_round_rect(8,y-4,cw-16,26,6,RGB(255,236,200));
        else if(i==m->sel) fill_round_rect(8,y-4,cw-16,26,6,RGB(232,225,250));
        draw_icon(IC_SONG,16,y,1,0);
        draw_text(40,y,songs[i].title,RGB(30,30,45));
        draw_text(cw-20-text_width(songs[i].composer),y,songs[i].composer,RGB(130,125,150));
    }
}
static void mp_mouse(window_t *w,int ev,int x,int y,int b){
    mp_t *m=w->st; int cw=win_cw(w); (void)b;
    if(ev!=EV_DOWN && ev!=EV_DBL) return;
    int bx=cw-196;
    if(ev==EV_DOWN){
        if(hit(x,y,bx,96,56,30)){ m->sel=(m->sel+song_count-1)%song_count; music_play(m->sel); return; }
        if(hit(x,y,bx+62,96,68,30)){ if(snd_wav_playing()){ snd_stop_wav(); return; } if(music_playing()>=0) music_stop(); else music_play(m->sel); return; }
        if(snd.hda && hit(x,y,cw-170,8,160,24)){ snd.instrument=(snd.instrument+1)%INST_COUNT; return; }
        if(hit(x,y,bx+136,96,56,30)){ m->sel=(m->sel+1)%song_count; music_play(m->sel); return; }
        if(hit(x,y,bx,130,120,22)){ m->autoplay=!m->autoplay; return; }
    }
    if(y>=LIST_Y-4){ int i=(y-LIST_Y+4)/28; if(i>=0&&i<song_count){ m->sel=i; if(ev==EV_DBL || ev==EV_DOWN) music_play(i); } }
}
static void mp_key(window_t *w,int k){
    mp_t *m=w->st;
    if(k==KEY_DOWN) m->sel=(m->sel+1)%song_count;
    else if(k==KEY_UP) m->sel=(m->sel+song_count-1)%song_count;
    else if(k=='\n') music_play(m->sel);
    else if(k==' '){ if(music_playing()>=0) music_stop(); else music_play(m->sel); }
}
static void mp_tick(window_t *w){
    mp_t *m=w->st; int p=music_playing();
    if(p>=0 || snd_wav_playing()){ if(p>=0) m->sel=p; if(ticks-m->last_anim>=50){ m->last_anim=ticks; wm_invalidate(); } }
    if(m->last_playing>=0 && p<0){
        wm_invalidate();
        if(m->autoplay){ m->sel=(m->last_playing+1)%song_count; music_play(m->sel); p=m->sel; }
    }
    m->last_playing=p;
}
static void mp_close(window_t *w){ (void)w; music_stop(); snd_stop_wav(); }
void app_music(int file){
    window_t *w=wm_open("Music",480,LIST_Y+song_count*28+8,IC_MUSIC); if(!w) return;
    mp_t *m=kzalloc(sizeof *m); m->autoplay=1; m->last_playing=-1; m->wav=-1;
    w->st=m; w->draw=mp_draw; w->mouse=mp_mouse; w->key=mp_key; w->tick=mp_tick; w->close=mp_close;
    if(file>=0 && fs_nodes[file].type==FT_SONG && fs_nodes[file].song>=0){ m->sel=fs_nodes[file].song; music_play(m->sel); }
    if(file>=0 && fs_nodes[file].type==FT_BIN && fs_nodes[file].data){
        music_stop();
        if(snd_play_wav((u8*)fs_nodes[file].data,fs_nodes[file].size)) m->wav=file;
        else wm_message("Music",snd.hda?"This WAV file uses a format CamadamOS can't play.\nIt needs plain PCM, 8 or 16 bit.":"WAV files need the HD Audio sound chip.\nThe PC speaker can only beep.");
    }
}

/* ================= Piano ================= */
#define KW 40
#define KH 170
#define KTOP 70
typedef struct { int octave; int lit; u32 lit_until; } piano_t;
static const char *white_names[7]={"C","D","E","F","G","A","B"};
static const int white_semi[7]={0,2,4,5,7,9,11};
static const int black_after[5]={0,1,3,4,5}; /* black key after these white keys */
static const int black_semi[5]={1,3,6,8,10};
static const char *note_names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static void play_semi(piano_t *p,int semi_abs){ /* semitones above C0 */
    piano_note(note_freq(semi_abs),380); p->lit=semi_abs; p->lit_until=ticks+250; wm_invalidate();
}
static void pn_draw(window_t *w){
    piano_t *p=w->st; int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(40,30,30),RGB(15,10,10));
    int lit = ticks<p->lit_until ? p->lit : -1;
    char t[32]="Octave "; char o[4]; itoa10(p->octave,o); strcat0(t,o,sizeof t);
    draw_text(20,16,t,RGB(230,200,160));
    draw_text(20,36,"Keys: A W S E D F T G Y H U J K   Z/X: octave",RGB(170,150,130));
    if(lit>=0){ char nn[8]; strncpy0(nn,note_names[lit%12],8); char oo[4]; itoa10(lit/12,oo); strcat0(nn,oo,8); draw_text_big(cw-120,10,nn,RGB(255,190,60),3); }
    int x0=(cw-14*KW)/2;
    for(int oc=0;oc<2;oc++) for(int i=0;i<7;i++){
        int x=x0+(oc*7+i)*KW, semi=(p->octave+oc)*12+white_semi[i];
        fill_round_rect(x+1,KTOP,KW-2,KH,4,semi==lit?RGB(255,210,120):RGB(250,248,240));
        if(i==0){ char nn[6]; strncpy0(nn,"C",6); char oo[4]; itoa10(p->octave+oc,oo); strcat0(nn,oo,6); draw_text_center(x,KTOP+KH-22,KW,nn,RGB(140,130,120)); }
        else draw_text_center(x,KTOP+KH-22,KW,white_names[i],RGB(180,170,160));
    }
    for(int oc=0;oc<2;oc++) for(int i=0;i<5;i++){
        int x=x0+(oc*7+black_after[i])*KW+KW-13, semi=(p->octave+oc)*12+black_semi[i];
        fill_round_rect(x,KTOP,26,KH*6/10,3,semi==lit?RGB(255,170,60):RGB(25,22,22));
        if(semi!=lit) fill_rect(x+4,KTOP+2,18,KH*6/10-12,RGB(50,45,45));
    }
}
static void pn_mouse(window_t *w,int ev,int x,int y,int b){
    piano_t *p=w->st; (void)b;
    if(ev!=EV_DOWN) return;
    int cw=win_cw(w), x0=(cw-14*KW)/2;
    if(y<KTOP||y>KTOP+KH) return;
    if(y<KTOP+KH*6/10) for(int oc=0;oc<2;oc++) for(int i=0;i<5;i++){
        int bx=x0+(oc*7+black_after[i])*KW+KW-13;
        if(hit(x,y,bx,KTOP,26,KH*6/10)){ play_semi(p,(p->octave+oc)*12+black_semi[i]); return; }
    }
    int k=(x-x0)/KW; if(x<x0||k>=14) return;
    play_semi(p,(p->octave+k/7)*12+white_semi[k%7]);
}
static void pn_key(window_t *w,int k){
    piano_t *p=w->st;
    static const char map[]="awsedftgyhujk";
    if(k>='A'&&k<='Z') k+=32;
    if(k=='z'){ if(p->octave>2) p->octave--; return; }
    if(k=='x'){ if(p->octave<6) p->octave++; return; }
    for(int i=0;map[i];i++) if(map[i]==k){ play_semi(p,p->octave*12+i); return; }
    (void)w;
}
static void pn_tick(window_t *w){ piano_t *p=w->st; if(p->lit>=0 && ticks>=p->lit_until && ticks<p->lit_until+30) wm_invalidate(); }
void app_piano(void){
    window_t *w=wm_open("Piano",14*KW+40,KTOP+KH+24,IC_PIANO); if(!w) return;
    piano_t *p=kzalloc(sizeof *p); p->octave=4; p->lit=-1;
    w->st=p; w->draw=pn_draw; w->mouse=pn_mouse; w->key=pn_key; w->tick=pn_tick;
}

/* ================= About ================= */
static char cpu_vendor[13];
static void ab_draw(window_t *w){
    int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(250,248,255),RGB(235,228,250));
    fill_circle(70,70,44,RGB(255,190,60)); fill_circle(70,70,36,RGB(60,30,110));
    draw_text_big(58,38,"C",RGB(255,190,60),4);
    draw_text_big(134,30,OS_NAME,RGB(90,40,170),3);
    draw_text(136,82,"Version " OS_VERSION " - made from scratch in C + assembly",RGB(90,80,120));
    int y=130; char b[64], n[16];
    #define ROW(label) draw_text(30,y,label,RGB(120,110,150)); draw_text(170,y,b,RGB(30,30,45)); y+=22;
    itoa10(scr_w,n); strncpy0(b,n,sizeof b); strcat0(b," x ",sizeof b); itoa10(scr_h,n); strcat0(b,n,sizeof b); ROW("Screen");
    itoa10((int)(mem_total_kb/1024),n); strncpy0(b,n,sizeof b); strcat0(b," MB",sizeof b); ROW("Memory");
    strncpy0(b,cpu_vendor[0]?cpu_vendor:"unknown",sizeof b); ROW("Processor");
    u32 s=ticks/1000; itoa10((int)(s/60),n); strncpy0(b,n,sizeof b); strcat0(b," min ",sizeof b); itoa10((int)(s%60),n); strcat0(b,n,sizeof b); strcat0(b," s",sizeof b); ROW("Running for");
    itoa10((int)(heap_free_bytes()/1024),n); strncpy0(b,n,sizeof b); strcat0(b," KB",sizeof b); ROW("Free app memory");
    draw_text(30,y,"Mouse speed",RGB(120,110,150));
    ui_button(170,y-5,26,24,"-",0); itoa10(mouse_speed,n); draw_text_center(198,y,30,n,RGB(30,30,45)); ui_button(230,y-5,26,24,"+",0);
    draw_text(268,y,"(or F2 / F3)",RGB(150,140,170)); y+=26;
    draw_text(30,y,"Double-click the desktop icons to start a program.",RGB(60,60,80)); y+=18;
    draw_text(30,y,"Open Web for a tour of how this OS works.",RGB(60,60,80));
    ui_button(cw-110,ch-42,94,28,"Close",0);
}
static void ab_mouse(window_t *w,int ev,int x,int y,int b){
    (void)b; if(ev!=EV_DOWN) return;
    if(hit(x,y,win_cw(w)-110,win_ch(w)-42,94,28)){ wm_close(w); return; }
    int sy=130+5*22;
    if(hit(x,y,170,sy-5,26,24) && mouse_speed>1) mouse_speed--;
    if(hit(x,y,230,sy-5,26,24) && mouse_speed<10) mouse_speed++;
}
static void ab_tick(window_t *w){ static u32 last; (void)w; if(ticks/1000!=last){ last=ticks/1000; wm_invalidate(); } }
static void ab_key(window_t *w,int k){ if(k==KEY_ESC||k=='\n') wm_close(w); }
void app_about(void){
    u32 a,bb,c,d; __asm__ volatile("cpuid":"=a"(a),"=b"(bb),"=c"(c),"=d"(d):"a"(0));
    memcpy(cpu_vendor,&bb,4); memcpy(cpu_vendor+4,&d,4); memcpy(cpu_vendor+8,&c,4); cpu_vendor[12]=0;
    window_t *w=wm_open("About CamadamOS",520,350,IC_ABOUT); if(!w) return;
    w->draw=ab_draw; w->mouse=ab_mouse; w->tick=ab_tick; w->key=ab_key;
}
