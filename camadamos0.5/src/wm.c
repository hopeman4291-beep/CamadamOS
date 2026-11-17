/* CamadamOS window manager: desktop, windows, taskbar, start menu */
#include "kernel.h"

u32 ui_accent = RGB(110,60,200);
int clock_offset=0, clock_12h=0, theme_wallpaper=0, theme_accent_idx=0;
const char *const wallpaper_names[N_WALLPAPERS]={"Camadam Night","Ocean","Sunset","Forest","Classic","Neon Grid"};
const u32 accent_colors[N_ACCENTS]={RGB(110,60,200),RGB(30,110,200),RGB(40,140,80),RGB(230,120,30),RGB(200,40,60),RGB(80,84,96)};
u32 accent_light(void){ return mix(accent_colors[theme_accent_idx],0xFFFFFF,60); }
u32 accent_dark(void){ return mix(accent_colors[theme_accent_idx],0,80); }
#define MAXWIN 24
#define TASKBAR_H 40
static window_t wins[MAXWIN];
static int zorder[MAXWIN], zcount;
static int dirty=1;
static int hide_cursor;
static int menu_open;
static int drag_win=-1, drag_dx, drag_dy, capture_win=-1, resize_win=-1, rs_w, rs_h, rs_mx, rs_my;
static int sel_icon=-1;
static u32 *bg;
static int last_min=-1;
static int kbmouse, kb_hold, kb_run; static u32 kb_last;

void wm_invalidate(void){ dirty=1; }
static char toast[100]; static u32 toast_until;
void wm_toast(const char *m){ strncpy0(toast,m,sizeof toast); toast_until=ticks+1500; dirty=1; }
static void speed_toast(void){
    char t[48]="Mouse speed: "; char n[4]; itoa10(mouse_speed,n); strcat0(t,n,sizeof t); strcat0(t," / 10",sizeof t);
    wm_toast(t);
}

/* ---------------- icons (16x16 pixel art) ---------------- */
static const char *const ic_mines[]={
"................",".......k........",".......k........","...k..kkk..k....","....kkkkkkk.....",
"....kwwkkkk.....","...kkwwkkkkk....",".kkkkkkkkkkkkk..","...kkkkkkkkk....","....kkkkkkk.....",
"....kkkkkkk.....","...k..kkk..k....",".......k........",".......k........","................","................",0};
static const char *const ic_cards[]={
"................","...kkkkkkkkkk...","...kwwwwwwwwk...","...kwrwwwwwwk...","...kwwwwwwwwk...",
"...kwrrwwrrwk...","...kwrrrrrrwk...","...kwrrrrrrwk...","...kwwrrrrwwk...","...kwwwrrwwwk...",
"...kwwwwwwwwk...","...kwwwwwwrwk...","...kwwwwwwwwk...","...kkkkkkkkkk...","................","................",0};
static const char *const ic_paint[]={
"................","....nnnnnnn.....","..nnnnnnnnnnn...",".nnrrnnnbbnnnn..",".nnrrnnnbbnnnn..",
"nnnnnnnnnnnnnnn.","nnyynnn...nnnnn.","nnyynn.....nnnn.","nnnnnnn...nnnnn.","nnnnnnnnnnnggnn.",
".nnccnnnnnnggn..",".nnccnnnnnnnnn..","..nnnnnnnnnnn...","....nnnnnnn.....","................","................",0};
static const char *const ic_folder[]={
"................","................",".ooooo..........",".oyyyyo.........",".oyyyyyoooooooo.",
".oyyyyyyyyyyyyo.",".oooooooooooooo.",".oyyyyyyyyyyyyo.",".oyyyyyyyyyyyyo.",".oyyyyyyyyyyyyo.",
".oyyyyyyyyyyyyo.",".oyyyyyyyyyyyyo.",".oyyyyyyyyyyyyo.",".oooooooooooooo.","................","................",0};
static const char *const ic_globe[]={
"................",".....BBBBBB.....","...BBllGGllBB...","..BlllGGGlllBB..","..BlGGGGllllGB..",
".BllGGGlllllGGB.",".BlllGGlllGGGGB.",".BllllGllGGGGlB.",".BlllllllGGGllB.",".BllGlllllGlllB.",
"..BlGGlllllllB..","..BllGGlllllB...","...BBlGlllBB....",".....BBBBBB.....","................","................",0};
static const char *const ic_note[]={
"................","........kkkkkkk.","......kkkkkkkkk.","......kkkk...kk.","......kk.....kk.",
"......kk.....kk.","......kk.....kk.","......kk.....kk.","......kk..kkkkk.","......kk.kkkkkk.",
"..kkkkkk.kkkkkk.",".kkkkkkk..kkkk..",".kkkkkkk........","..kkkkk.........","................","................",0};
static const char *const ic_piano[]={
"................","................","dddddddddddddddd","dwkkwkkwdwkkwkkd","dwkkwkkwdwkkwkkd",
"dwkkwkkwdwkkwkkd","dwkkwkkwdwkkwkkd","dwkkwkkwdwkkwkkd","dwwswwswdwwswwsd","dwwswwswdwwswwsd",
"dwwswwswdwwswwsd","dwwswwswdwwswwsd","dddddddddddddddd","................","................","................",0};
static const char *const ic_about[]={
"................","......wwww......","....wwwwwwww....","...wwwwbbwwww...","..wwwwwbbwwwww..",
"..wwwwwwwwwwww..",".wwwwwbbbwwwwww.",".wwwwwwbbwwwwww.",".wwwwwwbbwwwwww.",".wwwwwwbbwwwwww.",
"..wwwwwbbwwwww..","..wwwwbbbbwwww..","...wwwwwwwwww...","....wwwwwwww....","......wwww......","................",0};
static const char *const ic_text[]={
"................","..ssssssssss....","..swwwwwwwwss...","..swddddwwwwss..","..swwwwwwwwwws..",
"..swddddddddws..","..swwwwwwwwwws..","..swddddddddws..","..swwwwwwwwwws..","..swddddddwwws..",
"..swwwwwwwwwws..","..swddddddddws..","..swwwwwwwwwws..","..ssssssssssss..","................","................",0};
static const char *const ic_image[]={
"................",".ssssssssssssss.",".sllllllllllyls.",".slllllllllyyys.",".sllllllllllyls.",
".slllllllllllls.",".sllllGllllllls.",".slllGGGllllGls.",".sllGGGGGllGGGs.",".slGGGGGGGGGGGs.",
".sgggggggggggggs",".sgggggggggggggs",".ssssssssssssss.","................","................","................",0};
static const char *const ic_page[]={
"................","..ssssssssss....","..swwwwwwwwss...","..swbbbbbwwwss..","..swwwwwwwwwws..",
"..swddddddddws..","..swddddwwwwws..","..swwwwwwwwwws..","..swwBBBBBBwws..","..swwBllllBwws..",
"..swwBBBBBBwws..","..swddddddddws..","..swwwwwwwwwws..","..ssssssssssss..","................","................",0};
static const char *const ic_notepad[]={
"................","..yyyyyyyyyy....","..ywwwwwwwwyy...","..ywkkkkwwwwyy..","..ywwwwwwwwwwy..",
"..ywkkkkkkkkwy..","..ywwwwwwwwwwy..","..ywkkkkkkwwny..","..ywwwwwwwwnoy..","..ywkkkkwwnoky..",
"..ywwwwwwnokwy..","..ywwwwwnokwwy..","..ywwwwwkkwwwy..","..yyyyyyyyyyyy..","................","................",0};
static const char *const ic_calc[]={
"................","...dddddddddd...","...dggggggggd...","...dgGGGGGGgd...","...dggggggggd...",
"...dddddddddd...","...dwwdwwdood...","...dwwdwwdood...","...dddddddddd...","...dwwdwwdood...",
"...dwwdwwdood...","...dddddddddd...","...dwwdwwdood...","...dwwdwwdood...","...dddddddddd...","................",0};
static const char *const ic_term[]={
"................","kkkkkkkkkkkkkkkk","kssssssssssssssk","kkkkkkkkkkkkkkkk","kkgkkkkkkkkkkkkk",
"kkkgkkkkkkkkkkkk","kkkkgkkkkkkkkkkk","kkkgkkkkkkkkkkkk","kkgkkggggkkkkkkk","kkkkkkkkkkkkkkkk",
"kkkkkkkkkkkkkkkk","kkkkkkkkkkkkkkkk","kkkkkkkkkkkkkkkk","kkkkkkkkkkkkkkkk","................","................",0};
static const char *const ic_snake[]={
"................","................","..........rr....","....bbbbbb.r....","...bwbbbbbb.....",
"...bbbbbbbb.....","..........bb....","....bbbbbbbb....","...bbbbbbbb.....","...bb...........",
"...bbbbbbbbbb...","....bbbbbbbbbb..","............bb..","..bbbbbbbbbbb...","..bbbbbbbbbb....","................",0};
static const char *const ic_gear[]={
"................","......dddd......","..dd..dssd..dd..","..dsddssssddsd..","...dssssssssd...",
"..ddssskkssssdd.","ddsssk..kssssdd.","dssssk..ksssssd.","dssssk..ksssssd.","ddssssskksssdd..",
"..ddsssssssdd...","...dssssssssd...","..dsddssssddsd..","..dd..dssd..dd..","......dddd......","................",0};
static const char *const ic_chat[]={
"................","..bbbbbbbbbbb...",".bwwwwwwwwwwwb..",".bwkkwkkkwkkwb..",".bwwwwwwwwwwwb..",
".bwkkkwkkwkwwb..",".bwwwwwwwwwwwb..","..bbbwwbbbbbb...","....bwb.........","....bb..........",
"........gggggggg",".......gwwwwwwwg",".......gwkkwkkwg",".......gwwwwwwwg","........gggwgggg","...........gg...",0};
static const char *const ic_clock[]={
"................",".....kkkkkk.....","...kkwwwwwwkk...","..kwwwwkwwwwwk..","..kwwwwkwwwwwk..",
".kwwwwwkwwwwwwk.",".kwwwwwkwwwwwwk.",".kwwwwwkkkkkwwk.",".kwwwwwwwwwwwwk.",".kwwwwwwwwwwwwk.",
"..kwwwwwwwwwwk..","..kwwwwwwwwwwk..","...kkwwwwwwkk...",".....kkkkkk.....","................","................",0};
static const char *const ic_weather[]={
"................","....y...........","..y.yyy.y.......","...yyyyy........","..yyyyyyy.......",
".yyyyyyyyy......","..yyyyywwww.....","...yyyywwwwww...","..y.ywwwwwwwwww.","....wwwwwwwwwww.",
"...wwwwwwwwwwwww","...wwwwwwwwwwwww","....wwwwwwwwwww.","................","................","................",0};
static const char *const ic_tetris[]={
"................","................","......rrrr......","......rrrr......","......rrrr......",
"..bbbbrrrr......","..bbbbrrrr......","..bbbbrrrrgggg..","..bbbbyyyygggg..","..bbbbyyyygggg..",
"..ccccyyyygggg..","..ccccyyyypppp..","..ccccyyyypppp..","..ccccppppppppp.","................","................",0};
static const char *const ic_pong[]={
"................","................","..w.............","..w.............","..w.............",
"..w.............","..w......ww.....","..w......ww.....","..............w.","..............w.",
"..............w.","..............w.","..............w.","................","................","................",0};
static const char *const ic_breakout[]={
"................","rrrr.rrrr.rrrr..","oooo.oooo.oooo..","yyyy.yyyy.yyyy..","gggg.gggg.......",
"................","................","........ww......","........ww......","................",
"................","................","................","....wwwwwww.....","................","................",0};
static const char *const ic_2048[]={
"................",".oooooo..yyyyyy.",".owwwwo..ywwwwy.",".ooowoo..yyywyy.",".oowooo..yywyyy.",
".owwwwo..ywwwwy.",".oooooo..yyyyyy.","................","................",".nnnnnn..rrrrrr.",
".nwnnwn..rwwwwr.",".nwwwwn..rrwrrr.",".nnnnwn..rrwrrr.",".nnnnwn..rrwrrr.",".nnnnnn..rrrrrr.","................",0};
static const char *const ic_install[]={
"................","......gggg......","......gggg......","......gggg......","....gggggggg....",
".....gggggg.....","......gggg......",".......gg.......","..ssssssssssss..",".sddddddddddddds",
".sdwwwwwwwwwwwds",".sdwwwwwwwwwgwds",".sdwwwwwwwwwwwds",".sddddddddddddds","..ssssssssssss..","................",0};
static const char *const ic_chess[]={
"................","......k.k.......",".....kkkkkk.....","....kkkwkkkk....","...kkkkkkkkkk...","..kkkkkkkkkkk...","..kkk..kkkkkk...",
"......kkkkkkk...",".....kkkkkkk....","....kkkkkkkk....","....kkkkkkkk....","...dddddddddd...","..kkkkkkkkkkkk..","..kkkkkkkkkkkk..","................","................",0};
static const char *const ic_basic[]={
"................","bbbbbbbbbbbbbbbb","bBBBBBBBBBBBBBBb","bBwwwBBwBBwwwBBb","bBwBBwBwBwBBBBBb","bBwwwBwBwBwwBBBb",
"bBwBBwwwwBBBwBBb","bBwwwBwBwBwwwBBb","bBBBBBBBBBBBBBBb","bByByyBBBBBBBBBb","bBBBBBBBBBBBBBBb","bBwBwwwwBBBBBBBb","bBBBBBBBBBBBBBBb","bbbbbbbbbbbbbbbb","................","................",0};
static const char *const ic_usb[]={
"................","................","....ssssss......","....swswsws.....","....ssssss......","...bbbbbbbb.....","...bBBBBBBb.....",
"...bBlllBBb.....","...bBlBBBBb.....","...bBlllBBb.....","...bBBBBBBb.....","...bBBBBBBb.....","...bBBBBBBb.....","...bbbbbbbb.....","................","................",0};
static const char *const *icon_maps[IC_COUNT]={ic_mines,ic_cards,ic_paint,ic_folder,ic_globe,ic_note,ic_piano,ic_about,ic_text,ic_folder,ic_image,ic_note,ic_page,ic_notepad,ic_calc,ic_term,ic_snake,ic_gear,ic_chat,ic_clock,ic_weather,ic_tetris,ic_pong,ic_breakout,ic_2048,ic_install,ic_chess,ic_basic,ic_usb};
static const u32 icon_tiles[IC_COUNT]={RGB(170,180,195),RGB(40,140,70),RGB(250,230,200),RGB(255,245,210),RGB(230,240,255),
    RGB(255,200,80),RGB(240,240,245),RGB(90,130,230),RGB(245,245,250),RGB(255,245,210),RGB(230,240,255),RGB(255,200,80),RGB(240,245,255),
    RGB(255,250,220),RGB(200,205,215),RGB(60,64,70),RGB(170,220,120),RGB(215,220,230),
    RGB(235,240,255),RGB(255,190,60),RGB(110,170,240),RGB(40,36,60),RGB(20,70,40),RGB(30,30,60),RGB(250,240,220),RGB(225,245,225),RGB(240,217,181),RGB(30,40,120),RGB(225,235,250)};
void draw_icon(int id,int x,int y,int sc,int tile){
    if(id<0||id>=IC_COUNT) return;
    if(tile){ int s=16*sc+sc*4; fill_round_rect(x+2,y+3,s,s,sc*3,RGB(0,0,0)); fill_round_rect(x,y,s,s,sc*3,icon_tiles[id]); x+=sc*2; y+=sc*2; }
    draw_pixmap(x,y,icon_maps[id],sc);
}

static const char *const cursor_map[]={
"k...........","kk..........","kwk.........","kwwk........","kwwwk.......","kwwwwk......","kwwwwwk.....",
"kwwwwwwk....","kwwwwwwwk...","kwwwwwwwwk..","kwwwwwwwwwk.","kwwwwwwkkkkk","kwwwkwwk....","kwwkkwwk....",
"kwk..kwwk...","kk...kwwk...","k.....kwwk..","......kwwk..",".......kk...",0};

/* ---------------- launchers ---------------- */
static void l_paint(void){ app_paint(-1); }
static void l_files(void){ app_files(-1); }
static void l_browser(void){ app_web(0); }
static void l_music(void){ app_music(-1); }
static void l_piano(void){ app_piano(); }
static void l_about(void){ app_about(); }
static void l_notepad(void){ app_notepad(-1); }
static void l_basic(void){ app_basic(0); }
static void l_calc(void){ app_calc(); }
static void l_term(void){ app_terminal(); }
static void l_settings(void){ app_settings(); }
void app_games(void);
typedef struct { const char *name; int icon; void (*fn)(void); } launcher_t;
static launcher_t launchers[]={
    {"Files",IC_FILES,l_files},{"Web",IC_BROWSER,l_browser},{"Chat",IC_IRC,app_irc},{"Games",IC_TETRIS,app_games},
    {"Music",IC_MUSIC,l_music},{"Piano",IC_PIANO,l_piano},{"Paint",IC_PAINT,l_paint},
    {"Notepad",IC_NOTEPAD,l_notepad},{"Calculator",IC_CALC,l_calc},{"Clock",IC_CLOCK,app_clock},{"Weather",IC_WEATHER,app_weather},
    {"Terminal",IC_TERM,l_term},{"BASIC",IC_BASIC,l_basic},{"Settings",IC_SETTINGS,l_settings},{"About",IC_ABOUT,l_about},{"Install",IC_INSTALL,app_install}};
#define NLAUNCH ((int)(sizeof launchers/sizeof launchers[0]) - (install_img?0:1))

/* ---------------- windows ---------------- */
static int widx(window_t *w){ return (int)(w-wins); }
static int top_win(void){
    for(int i=zcount-1;i>=0;i--) if(!wins[zorder[i]].minimized) return zorder[i];
    return -1;
}
static void raise(int i){
    int k=-1; for(int j=0;j<zcount;j++) if(zorder[j]==i) k=j;
    if(k<0) return;
    for(int j=k;j<zcount-1;j++) zorder[j]=zorder[j+1];
    zorder[zcount-1]=i; dirty=1;
}
void wm_focus(window_t *w){ int i=w-wins; if(i<0||i>=MAXWIN||!wins[i].used) return; wins[i].minimized=0; raise(i); }
window_t *wm_open(const char *title,int cw,int ch,int icon){
    int i; for(i=0;i<MAXWIN;i++) if(!wins[i].used) break;
    if(i==MAXWIN) return 0;
    window_t *w=&wins[i]; memset(w,0,sizeof *w);
    w->used=1; w->icon=icon; strncpy0(w->title,title,sizeof w->title);
    w->w=cw+2*BORDER; w->h=ch+TITLE_H+BORDER;
    static int cascade;
    int avail_h=scr_h-TASKBAR_H;
    w->x=(scr_w-w->w)/2 + (cascade%6)*28 - 70;
    w->y=(avail_h-w->h)/2 + (cascade%6)*24 - 50;
    cascade++;
    w->x=clampi(w->x,0,imax(0,scr_w-w->w)); w->y=clampi(w->y,0,imax(0,avail_h-w->h));
    zorder[zcount++]=i; dirty=1;
    return w;
}
void wm_close(window_t *w){
    int i=widx(w); if(!w->used) return;
    if(w->close) w->close(w);
    kfree(w->st); w->st=0; w->used=0;
    int k=0; for(int j=0;j<zcount;j++) if(zorder[j]!=i) zorder[k++]=zorder[j];
    zcount=k;
    if(capture_win==i) capture_win=-1;
    if(resize_win==i) resize_win=-1;
    if(drag_win==i) drag_win=-1;
    dirty=1;
}
void wm_toggle_max(window_t *w){
    if(!w->resizable) return;
    if(w->maximized){ w->x=w->rx; w->y=w->ry; w->w=w->rw; w->h=w->rh; w->maximized=0; }
    else { w->rx=w->x; w->ry=w->y; w->rw=w->w; w->rh=w->h; w->x=0; w->y=0; w->w=scr_w; w->h=scr_h-TASKBAR_H; w->maximized=1; }
    dirty=1;
}
void wm_set_title(window_t *w,const char *t){ strncpy0(w->title,t,sizeof w->title); dirty=1; }

int ui_button(int x,int y,int w,int h,const char *label,int pressed){
    fill_round_rect(x,y,w,h,5,pressed?RGB(200,190,235):RGB(232,234,242));
    u32 bc=pressed?ui_accent:RGB(170,172,190);
    hline(x+4,y,w-8,bc); hline(x+4,y+h-1,w-8,bc); vline(x,y+4,h-8,bc); vline(x+w-1,y+4,h-8,bc);
    pset(x+1,y+2,bc);pset(x+2,y+1,bc);pset(x+3,y+1,bc);pset(x+w-2,y+2,bc);pset(x+w-3,y+1,bc);pset(x+w-4,y+1,bc);
    pset(x+1,y+h-3,bc);pset(x+2,y+h-2,bc);pset(x+3,y+h-2,bc);pset(x+w-2,y+h-3,bc);pset(x+w-3,y+h-2,bc);pset(x+w-4,y+h-2,bc);
    draw_text_center(x,y+(h-16)/2,w,label,RGB(30,30,45));
    return 0;
}

/* ---------------- message box ---------------- */
typedef struct { char msg[400]; } msg_t;
static void msg_draw(window_t *w){
    msg_t *m=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(248,248,252));
    draw_icon(IC_ABOUT,16,16,2,0);
    int y=20; const char *p=m->msg; char line[64]; int n=0;
    while(1){
        if(*p=='\n'||*p==0||n==(cw-80)/8){ line[n]=0; draw_text(64,y,line,RGB(30,30,40)); y+=18; n=0; if(!*p) break; if(*p=='\n'){p++; continue;} }
        line[n++]=*p++;
    }
    ui_button(cw-100,ch-40,84,28,"OK",0);
}
static void msg_mouse(window_t *w,int ev,int x,int y,int b){ (void)b; if(ev==EV_DOWN && hit(x,y,win_cw(w)-100,win_ch(w)-40,84,28)) wm_close(w); }
static void msg_key(window_t *w,int k){ if(k=='\n'||k==KEY_ESC||k==' ') wm_close(w); }
void wm_message(const char *title,const char *msg){
    int lines=1; for(const char *p=msg;*p;p++) if(*p=='\n') lines++;
    window_t *w=wm_open(title,420,90+lines*18,IC_ABOUT); if(!w) return;
    msg_t *m=kzalloc(sizeof *m); strncpy0(m->msg,msg,sizeof m->msg);
    w->st=m; w->draw=msg_draw; w->mouse=msg_mouse; w->key=msg_key;
}

/* ---------------- desktop background ---------------- */
static const s16 wsin[64]={0,98,195,290,383,471,556,634,707,773,831,882,924,957,981,995,1000,995,981,957,924,882,831,773,707,634,556,471,383,290,195,98,0,-98,-195,-290,-383,-471,-556,-634,-707,-773,-831,-882,-924,-957,-981,-995,-1000,-995,-981,-957,-924,-882,-831,-773,-707,-634,-556,-471,-383,-290,-195,-98};
static void build_bg(void){
    if(!bg) bg=kmalloc(scr_w*scr_h*4);
    u32 *save=gfx_backbuffer();
    int H=scr_h-TASKBAR_H, W=scr_w;
    gfx_reset_clip(); gfx_ox=gfx_oy=0;
    u32 mark=RGB(255,210,130);
    switch(theme_wallpaper){
    case 0:
        gradient_v(0,0,W,H,RGB(28,30,72),RGB(120,50,120));
        for(int k=0;k<5;k++){ int r=180+k*70; for(int a=0;a<3;a++) draw_circle(W-160,H-160,r+a,mix(RGB(80,50,130),RGB(255,190,90),40+k*10)); }
        break;
    case 1:
        gradient_v(0,0,W,H,RGB(8,30,80),RGB(20,140,160));
        for(int k=0;k<9;k++){ int y0=H/3+k*H/14; u32 c=mix(RGB(40,110,170),RGB(140,230,240),k*25);
            for(int x=0;x<W;x++){ int y=y0+wsin[(x/6+k*7)&63]*(6+k)/1000; pset(x,y,c); pset(x,y+1,c); } }
        mark=RGB(200,245,255); break;
    case 2:{
        int hz=H*62/100;
        gradient_v(0,0,W,hz,RGB(60,30,110),RGB(255,140,70));
        fill_circle(W/2,hz,110,RGB(255,200,110)); fill_circle(W/2,hz,90,RGB(255,220,140));
        gradient_v(0,hz,W,H-hz,RGB(40,40,100),RGB(15,15,40));
        for(int y=hz+4;y<H;y+=7){ int half=(110-(y-hz)/3); if(half>0) fill_rect(W/2-half+(y*13%17),y,half*2-(y*7%23),2,RGB(255,190,110)); }
        break;}
    case 3:
        gradient_v(0,0,W,H,RGB(150,200,210),RGB(230,240,220));
        for(int layer=0;layer<3;layer++){
            u32 c=mix(RGB(70,120,90),RGB(15,50,30),layer*110);
            for(int t=0;t<W/40+2;t++){ int tx=t*48-layer*20+((t*37*layer)%30), th=120+((t*53+layer*29)%90), base=H-80+layer*40;
                for(int r=0;r<th;r++){ int half=r*28/th; hline(tx-half,base-th+r,half*2+1,c); } fill_rect(tx-3,base,7,20,RGB(60,40,30)); }
        }
        fill_rect(0,H-20,W,20,RGB(20,50,30)); mark=RGB(255,255,230); break;
    case 4:
        fill_rect(0,0,W,H,RGB(0,128,128)); mark=0; break;
    case 5:{
        fill_rect(0,0,W,H,RGB(10,8,30));
        int hz=H/2; gradient_v(0,0,W,hz,RGB(10,8,30),RGB(70,20,90));
        fill_circle(W/2,hz-10,100,RGB(255,110,80)); for(int y=hz-60;y<hz;y+=9) fill_rect(W/2-110,y,220,3+(y-hz+60)/12,RGB(70,20,90));
        fill_rect(0,hz,W,H-hz,RGB(14,6,36));
        for(int k=1;k<14;k++){ int y=hz+k*k*(H-hz)/196; hline(0,y,W,RGB(255,60,200)); }
        for(int k=-20;k<=20;k++) draw_line(W/2+k*14,hz,W/2+k*120,H,RGB(80,200,255));
        mark=RGB(120,230,255); break;}
    }
    if(mark){
        draw_text_big(W-380,H-110,"CamadamOS",mark,4);
        draw_text(W-376,H-40,"version " OS_VERSION " - made from scratch",mix(mark,0xFFFFFF,120));
    }
    memcpy(bg,save,scr_w*scr_h*4);
}
void theme_apply(void){ ui_accent=accent_colors[theme_accent_idx]; build_bg(); dirty=1; }

/* ---------------- composing ---------------- */
static void draw_window(int i, int focused){
    window_t *w=&wins[i];
    gfx_reset_clip(); gfx_ox=gfx_oy=0;
    blend_rect(w->x+6,w->y+6,w->w,w->h,0,90);
    fill_rect(w->x,w->y,w->w,w->h,focused?accent_dark():RGB(150,150,165));
    if(focused) gradient_v(w->x,w->y,w->w,TITLE_H,accent_light(),accent_colors[theme_accent_idx]);
    else gradient_v(w->x,w->y,w->w,TITLE_H,RGB(185,185,200),RGB(155,155,170));
    draw_icon(w->icon,w->x+6,w->y+6,1,0);
    gfx_set_clip(w->x,w->y,w->w-60,TITLE_H);
    draw_text_bold(w->x+28,w->y+6,w->title,RGB(255,255,255));
    gfx_reset_clip();
    /* close & minimise buttons */
    int bx=w->x+w->w-26, by=w->y+5;
    fill_circle(bx+9,by+9,9,RGB(230,70,70)); draw_line(bx+5,by+5,bx+13,by+13,RGB(255,255,255)); draw_line(bx+13,by+5,bx+5,by+13,RGB(255,255,255));
    draw_line(bx+6,by+5,bx+13,by+12,RGB(255,255,255)); draw_line(bx+12,by+5,bx+5,by+12,RGB(255,255,255));
    if(w->resizable){ bx-=26; fill_circle(bx+9,by+9,9,RGB(80,190,100)); draw_rect(bx+5,by+5,9,9,RGB(255,255,255)); if(!w->maximized) hline(bx+5,by+6,9,RGB(255,255,255)); else draw_rect(bx+7,by+3,8,8,RGB(255,255,255)); }
    bx-=26; fill_circle(bx+9,by+9,9,RGB(240,190,60)); fill_rect(bx+5,by+8,9,2,RGB(255,255,255));
    gfx_set_clip(w->x+BORDER,w->y+TITLE_H,win_cw(w),win_ch(w));
    gfx_ox=w->x+BORDER; gfx_oy=w->y+TITLE_H;
    if(w->draw) w->draw(w);
    gfx_ox=gfx_oy=0; gfx_reset_clip();
    if(w->resizable && !w->maximized){ int gx=w->x+w->w-14, gy=w->y+w->h-14; u32 c=RGB(140,135,160);
        for(int k=0;k<3;k++) draw_line(gx+4+k*4,gy+13,gx+13,gy+4+k*4,c); }
}
static int menu_x(void){ return 4; }
static int menu_w(void){ return 220; }
static int menu_item_h(void){ return imin(34,(scr_h-TASKBAR_H-50-12-8-10)/(NLAUNCH+3)); }
static int menu_h(void){ return 50+NLAUNCH*menu_item_h()+12+3*menu_item_h()+8; }
static int menu_y(void){ return scr_h-TASKBAR_H-menu_h()-4; }
static void draw_menu(void){
    int x=menu_x(), y=menu_y(), w=menu_w(), h=menu_h();
    blend_rect(x+5,y+5,w,h,0,100);
    fill_round_rect(x,y,w,h,8,RGB(245,243,252));
    gradient_v(x,y,w,44,accent_light(),accent_dark());
    draw_text_bold(x+14,y+6,OS_NAME,RGB(255,255,255));
    draw_text(x+14,y+22,"Hello, Camadam!",RGB(230,220,255));
    int iy=y+50;
    for(int i=0;i<NLAUNCH;i++){
        if(hit(mouse_x,mouse_y,x,iy,w,menu_item_h())) fill_rect(x+4,iy,w-8,menu_item_h(),mix(accent_light(),0xFFFFFF,170));
        if(menu_item_h()>=33) draw_icon(launchers[i].icon,x+12,iy+1,2,0); else draw_icon(launchers[i].icon,x+20,iy+(menu_item_h()-16)/2,1,0);
        draw_text(x+52,iy+(menu_item_h()-16)/2,launchers[i].name,RGB(30,30,45));
        iy+=menu_item_h();
    }
    hline(x+10,iy+5,w-20,RGB(200,195,215)); iy+=12;
    const char *pw[3]={"Lock","Restart","Shut down"};
    for(int i=0;i<3;i++){
        if(hit(mouse_x,mouse_y,x,iy,w,menu_item_h())) fill_rect(x+4,iy,w-8,menu_item_h(),mix(accent_light(),0xFFFFFF,170));
        fill_circle(x+28,iy+menu_item_h()/2,8,i==2?RGB(220,60,60):i==1?RGB(60,140,220):RGB(255,190,60));
        draw_text(x+52,iy+(menu_item_h()-16)/2,pw[i],RGB(30,30,45));
        iy+=menu_item_h();
    }
}
static int tb_btn_w(void){ int n=0; for(int i=0;i<zcount;i++) n++; int avail=scr_w-130-110; return n? imin(180, avail/n) : 180; }
static void draw_taskbar(void){
    int y=scr_h-TASKBAR_H;
    gradient_v(0,y,scr_w,TASKBAR_H,RGB(36,30,60),RGB(20,16,36));
    hline(0,y,scr_w,RGB(90,70,140));
    /* start button */
    fill_round_rect(6,y+5,112,30,8,menu_open?accent_light():accent_colors[theme_accent_idx]);
    fill_circle(24,y+20,9,RGB(255,190,60)); fill_circle(24,y+20,6,menu_open?accent_light():accent_colors[theme_accent_idx]);
    draw_text_bold(40,y+12,"Camadam",RGB(255,255,255));
    int bw=tb_btn_w(), x=128, top=top_win();
    for(int i=0;i<MAXWIN;i++){
        if(!wins[i].used) continue;
        int act=(i==top);
        fill_round_rect(x,y+5,bw-6,30,6,act?mix(accent_colors[theme_accent_idx],RGB(40,36,60),110):RGB(55,48,85));
        if(wins[i].minimized) draw_rect(x+3,y+31,bw-12,2,RGB(120,110,160)); else hline(x+10,y+33,bw-26,RGB(255,190,60));
        draw_icon(wins[i].icon,x+6,y+12,1,0);
        gfx_set_clip(x,y,bw-12,TASKBAR_H);
        draw_text(x+28,y+12,wins[i].title,RGB(240,240,250));
        gfx_reset_clip();
        x+=bw;
    }
    int h,m,s,d,mo,yr; rtc_time(&h,&m,&s); rtc_date(&d,&mo,&yr);
    h=(h+clock_offset+24)%24;
    char t[16]; int hh=h;
    if(clock_12h){ hh=h%12; if(!hh) hh=12; }
    t[0]='0'+hh/10;t[1]='0'+hh%10;t[2]=':';t[3]='0'+m/10;t[4]='0'+m%10;t[5]=0;
    if(clock_12h){ if(t[0]=='0') t[0]=' '; strcat0(t,h<12?" AM":" PM",sizeof t); }
    draw_text_bold(scr_w-8-text_width(t),y+4,t,RGB(255,255,255));
    char ds[16]; ds[0]='0'+d/10;ds[1]='0'+d%10;ds[2]='.';ds[3]='0'+mo/10;ds[4]='0'+mo%10;ds[5]='.';itoa10(yr,ds+6);
    draw_text(scr_w-90,y+20,ds,RGB(190,180,220));
    if(music_playing()>=0){ draw_icon(IC_MUSIC,scr_w-118,y+12,1,0); }
    /* network indicator */
    if(net.present){ int nx=scr_w-140, ny=y+12; u32 c=net.dhcp==3?RGB(90,220,120):net.link?RGB(240,190,60):RGB(130,120,150);
        fill_rect(nx,ny+10,4,6,c); fill_rect(nx+6,ny+6,4,10,c); fill_rect(nx+12,ny+2,4,14,c); }
    last_min=m;
}
static void draw_desktop_icons(void){
    for(int i=0;i<NLAUNCH;i++){
        int col=i/ ((scr_h-TASKBAR_H-20)/92), row=i % ((scr_h-TASKBAR_H-20)/92);
        int x=16+col*96, y=16+row*92;
        if(sel_icon==i) blend_rect(x-4,y-4,88,86,RGB(255,255,255),60);
        draw_icon(launchers[i].icon,x+20,y,2,1);
        draw_text_center(x+1,y+51,80,launchers[i].name,RGB(0,0,0));
        draw_text_center(x,y+50,80,launchers[i].name,RGB(255,255,255));
    }
}
static int desktop_icon_at(int mx,int my){
    int per=(scr_h-TASKBAR_H-20)/92;
    for(int i=0;i<NLAUNCH;i++){ int col=i/per,row=i%per,x=16+col*96,y=16+row*92; if(hit(mx,my,x-4,y-4,88,86)) return i; }
    return -1;
}
static void compose(void){
    gfx_reset_clip(); gfx_ox=gfx_oy=0;
    memcpy(gfx_backbuffer(),bg,scr_w*scr_h*4);
    draw_desktop_icons();
    int top=top_win();
    for(int k=0;k<zcount;k++){ int i=zorder[k]; if(!wins[i].minimized) draw_window(i,i==top); }
    draw_taskbar();
    if(menu_open) draw_menu();
    if(ticks<toast_until){
        int tw=text_width(toast)+40, tx=(scr_w-tw)/2, ty=scr_h-TASKBAR_H-70;
        blend_rect(tx+3,ty+3,tw,40,0,90); fill_round_rect(tx,ty,tw,40,10,RGB(40,30,70));
        draw_text_center(tx,ty+12,tw,toast,RGB(255,220,150));
    }
    if(!hide_cursor) draw_pixmap(mouse_x,mouse_y,cursor_map,1);
}

/* ---------------- input routing ---------------- */
static int win_at(int x,int y){
    for(int k=zcount-1;k>=0;k--){ window_t *w=&wins[zorder[k]]; if(!w->minimized && hit(x,y,w->x,w->y,w->w,w->h)) return zorder[k]; }
    return -1;
}
static void send_mouse(int i,int ev,int x,int y,int b){
    window_t *w=&wins[i];
    if(w->used && w->mouse) w->mouse(w,ev,x-w->x-BORDER,y-w->y-TITLE_H,b);
    dirty=1;
}
static void do_shutdown(void){
    music_stop();
    store_save();
    shutdown();
    gfx_reset_clip(); fill_rect(0,0,scr_w,scr_h,RGB(0,0,0));
    draw_text_big(scr_w/2-text_width("It's now safe to turn off")*1,scr_h/2-40,"It's now safe to turn off",RGB(255,170,60),2);
    draw_text_big(scr_w/2-text_width("your computer.")*1,scr_h/2,"your computer.",RGB(255,170,60),2);
    gfx_present(); cli(); for(;;) hlt();
}
static void menu_click(int mx,int my){
    int x=menu_x(), iy=menu_y()+50;
    for(int i=0;i<NLAUNCH;i++){ if(hit(mx,my,x,iy,menu_w(),menu_item_h())){ menu_open=0; launchers[i].fn(); return; } iy+=menu_item_h(); }
    iy+=12;
    if(hit(mx,my,x,iy,menu_w(),menu_item_h())){ menu_open=0; lock_screen(); dirty=1; return; }
    iy+=menu_item_h();
    if(hit(mx,my,x,iy,menu_w(),menu_item_h())){ music_stop(); store_save(); reboot(); }
    iy+=menu_item_h();
    if(hit(mx,my,x,iy,menu_w(),menu_item_h())) do_shutdown();
}
static u32 last_click_t; static int last_click_x, last_click_y;
static void on_left_down(int mx,int my){
    int dbl = (ticks-last_click_t<450 && iabs(mx-last_click_x)<6 && iabs(my-last_click_y)<6);
    last_click_t = dbl?0:ticks; last_click_x=mx; last_click_y=my;
    dirty=1;
    if(menu_open){
        if(hit(mx,my,menu_x(),menu_y(),menu_w(),menu_h())){ menu_click(mx,my); return; }
        menu_open=0;
        if(hit(mx,my,6,scr_h-TASKBAR_H+5,112,30)) return;
    }
    if(my>=scr_h-TASKBAR_H){
        if(hit(mx,my,6,scr_h-TASKBAR_H+5,112,30)){ menu_open=1; return; }
        int bw=tb_btn_w(), x=128;
        for(int i=0;i<MAXWIN;i++){
            if(!wins[i].used) continue;
            if(hit(mx,my,x,scr_h-TASKBAR_H,bw,TASKBAR_H)){
                if(wins[i].minimized){ wins[i].minimized=0; raise(i); }
                else if(i==top_win()) wins[i].minimized=1;
                else raise(i);
                return;
            }
            x+=bw;
        }
        return;
    }
    int i=win_at(mx,my);
    if(i>=0){
        window_t *w=&wins[i];
        raise(i);
        if(my<w->y+TITLE_H){
            int bx=w->x+w->w-26;
            if(hit(mx,my,bx,w->y+5,20,20)){ wm_close(w); return; }
            if(w->resizable){ bx-=26; if(hit(mx,my,bx,w->y+5,20,20) || dbl){ wm_toggle_max(w); return; } }
            if(hit(mx,my,bx-26,w->y+5,20,20)){ w->minimized=1; return; }
            if(w->maximized) return;
            drag_win=i; drag_dx=mx-w->x; drag_dy=my-w->y; return;
        }
        if(w->resizable && !w->maximized && hit(mx,my,w->x+w->w-16,w->y+w->h-16,16,16)){
            resize_win=i; rs_w=w->w; rs_h=w->h; rs_mx=mx; rs_my=my; return;
        }
        if(hit(mx,my,w->x+BORDER,w->y+TITLE_H,win_cw(w),win_ch(w))){
            capture_win=i;
            send_mouse(i,EV_DOWN,mx,my,1);
            if(dbl && w->used) send_mouse(i,EV_DBL,mx,my,1);
        }
        return;
    }
    int ic=desktop_icon_at(mx,my);
    sel_icon=ic;
    if(ic>=0 && dbl){ sel_icon=-1; launchers[ic].fn(); }
}
static void handle_mouse(int mx,int my,int btn,int wheel,int prev){
    static int lx=-1, ly=-1;
    if(mx!=lx||my!=ly){
        lx=mx; ly=my; dirty=1;
        if(resize_win>=0){
            window_t *w=&wins[resize_win];
            w->w=clampi(rs_w+mx-rs_mx, w->min_w?w->min_w:260, scr_w-w->x);
            w->h=clampi(rs_h+my-rs_my, w->min_h?w->min_h:180, scr_h-TASKBAR_H-w->y);
        } else if(drag_win>=0){
            window_t *w=&wins[drag_win];
            w->x=clampi(mx-drag_dx,-w->w+80,scr_w-80);
            w->y=clampi(my-drag_dy,0,scr_h-TASKBAR_H-TITLE_H);
        } else if(capture_win>=0) send_mouse(capture_win,EV_MOVE,mx,my,btn);
        else { int i=win_at(mx,my); if(i>=0 && i==top_win() && my>=wins[i].y+TITLE_H){ window_t *w=&wins[i]; if(w->mouse) w->mouse(w,EV_MOVE,mx-w->x-BORDER,my-w->y-TITLE_H,0); } }
    }
    if((btn&1) && !(prev&1)) on_left_down(mx,my);
    if(!(btn&1) && (prev&1)){
        drag_win=-1; resize_win=-1;
        if(capture_win>=0){ int c=capture_win; capture_win=-1; send_mouse(c,EV_UP,mx,my,1); }
    }
    if((btn&2) && !(prev&2)){
        int i=win_at(mx,my);
        if(i>=0 && my>=wins[i].y+TITLE_H){ raise(i); send_mouse(i,EV_DOWN,mx,my,2); }
    }
    if(wheel){ int i=win_at(mx,my); if(i>=0) send_mouse(i,EV_WHEEL,mx,my,wheel); }
}

void wm_pump(void){ compose(); gfx_present(); }
void screenshot(void){
    int pics=fs_lookup("/Pictures"); if(pics<0) pics=fs_create(0,"Pictures",FT_DIR);
    hide_cursor=1; compose(); hide_cursor=0;
    u32 n=(u32)scr_w*scr_h; u32 *px=kmalloc(n*4);
    if(!px){ wm_toast("Not enough memory for a screenshot"); return; }
    memcpy(px,gfx_backbuffer(),n*4);
    char name[FS_NAME]; fs_unique_name(pics,"Screenshot",".img",name);
    int f=fs_create(pics,name,FT_IMAGE);
    if(f<0){ kfree(px); wm_toast("Couldn't save the screenshot"); return; }
    fs_nodes[f].data=(char*)px; fs_nodes[f].img_w=scr_w; fs_nodes[f].img_h=scr_h; fs_nodes[f].size=n*4; fs_touch();
    blend_rect(0,0,scr_w,scr_h,0xFFFFFF,140); gfx_present();      /* camera flash */
    snd_note(1800,0,40,60); snd_note(1200,0,60,50);
    char m[64]; strncpy0(m,"Screenshot saved: /Pictures/",sizeof m); strcat0(m,name,sizeof m); wm_toast(m);
}
char user_name[32]="Camadam";
void clock_service(void);
void lock_screen(void){
    int mx,my,b,wh, prevb=mouse_btn, done=0; u32 last=0; int lx=-1, ly=-1;
    while(kbd_get()>=0);
    while(!done){
        while(mouse_get(&mx,&my,&b,&wh)){ if((b&1) && !(prevb&1)) done=1; prevb=b; }
        int k; while((k=kbd_get())>=0) if(k=='\n'||k==' '||k==KEY_ESC||(k>=32&&k<127)) done=1;
        if(ticks-last>=500 || mouse_x!=lx || mouse_y!=ly){
            last=ticks; lx=mouse_x; ly=mouse_y;
            gfx_reset_clip(); gfx_ox=gfx_oy=0;
            memcpy(gfx_backbuffer(),bg,scr_w*scr_h*4);
            { u32 *bb=gfx_backbuffer(); for(int yy=scr_h-TASKBAR_H;yy<scr_h;yy++) memcpy(bb+yy*scr_w,bb+(scr_h-TASKBAR_H-1)*scr_w,scr_w*4); }
            blend_rect(0,0,scr_w,scr_h,RGB(10,8,20),150);
            int h,m,s2,d,mo,y; rtc_time(&h,&m,&s2); rtc_date(&d,&mo,&y); h=(h+clock_offset+24)%24;
            char t[8]; t[0]='0'+h/10; t[1]='0'+h%10; t[2]=':'; t[3]='0'+m/10; t[4]='0'+m%10; t[5]=0;
            draw_text_big(scr_w/2-text_width(t)*7/2,scr_h/5,t,RGB(255,255,255),7);
            char ds[24], n[8]; itoa10(d,n); strncpy0(ds,n,sizeof ds); strcat0(ds,".",sizeof ds); itoa10(mo,n); strcat0(ds,n,sizeof ds); strcat0(ds,".",sizeof ds); itoa10(y,n); strcat0(ds,n,sizeof ds);
            draw_text_big(scr_w/2-text_width(ds),scr_h/5+120,ds,RGB(220,210,255),2);
            int ay=scr_h*3/5;
            fill_circle(scr_w/2,ay,46,accent_light()); fill_circle(scr_w/2,ay,42,accent_colors[theme_accent_idx]);
            char ini[2]={user_name[0]?user_name[0]:'C',0}; if(ini[0]>='a'&&ini[0]<='z') ini[0]-=32;
            draw_text_big(scr_w/2-12,ay-32,ini,RGB(255,255,255),4);
            draw_text_big(scr_w/2-text_width(user_name),ay+58,user_name,RGB(255,255,255),2);
            if((ticks/700)%2==0) draw_text_center(0,ay+110,scr_w,"Click or press Enter to unlock",RGB(200,190,230));
            draw_pixmap(mouse_x,mouse_y,cursor_map,1);
            gfx_present();
        }
        net_poll(); usb_poll(); clock_service();
        hlt();
    }
    dirty=1;
}
void wm_run(void){
    theme_apply();
    music_play_notes("C5:1 E5:1 G5:1 C6:3 R:1 A5:1 C6:1 E6:4",85);
    lock_screen();
    app_about();
    wm_toast("F2/F3 mouse speed  -  F4 keyboard mouse  -  F5 mouse test");
    toast_until=ticks+6000;
    int prevb=0; u32 last_frame=0, last_tick=0;
    for(;;){
        int k;
        while((k=kbd_get())>=0){
            dirty=1;
            if(menu_open && k==KEY_ESC){ menu_open=0; continue; }
            if(k==KEY_F1){ app_about(); continue; }
            if(k==KEY_F5){ app_mousetest(); continue; }
            if(k==KEY_F12){ lock_screen(); continue; }
            if(k==KEY_PRTSC){ screenshot(); continue; }
            if(k==KEY_F4){ kbmouse=!kbmouse; if(!kbmouse && kb_hold){ kb_hold=0; mouse_inject(0); }
                wm_toast(kbmouse?"Keyboard mouse ON: arrows move, Enter click, Space drag, Tab right-click":"Keyboard mouse OFF");
                toast_until=ticks+(kbmouse?5000:1500); continue; }
            if(kbmouse){
                int step=12;
                if(k==KEY_LEFT||k==KEY_RIGHT||k==KEY_UP||k==KEY_DOWN){
                    if(ticks-kb_last<120) kb_run=imin(kb_run+1,6); else kb_run=0;
                    kb_last=ticks; step=8+kb_run*6;
                    if(k==KEY_LEFT) mouse_x=clampi(mouse_x-step,0,scr_w-1);
                    if(k==KEY_RIGHT) mouse_x=clampi(mouse_x+step,0,scr_w-1);
                    if(k==KEY_UP) mouse_y=clampi(mouse_y-step,0,scr_h-1);
                    if(k==KEY_DOWN) mouse_y=clampi(mouse_y+step,0,scr_h-1);
                    mouse_inject(kb_hold?1:0); continue;
                }
                if(k=='\n'){ mouse_inject(1); mouse_inject(0); kb_hold=0; continue; }
                if(k==' '){ kb_hold=!kb_hold; mouse_inject(kb_hold?1:0); continue; }
                if(k=='\t'){ mouse_inject(2); mouse_inject(0); continue; }
            }
            if(k==KEY_F2){ if(mouse_speed>1) mouse_speed--; speed_toast(); continue; }
            if(k==KEY_F3){ if(mouse_speed<10) mouse_speed++; speed_toast(); continue; }
            int t=top_win();
            if(t>=0 && wins[t].key) wins[t].key(&wins[t],k);
        }
        int mx,my,b,wh;
        while(mouse_get(&mx,&my,&b,&wh)){ handle_mouse(mx,my,b,wh,prevb); prevb=b; }
        net_poll();
        usb_poll();
        clock_service();
        store_tick();
        if(ticks-last_tick>=10){
            last_tick=ticks;
            for(int i=0;i<MAXWIN;i++) if(wins[i].used && wins[i].tick) wins[i].tick(&wins[i]);
            static int was_toast; if(ticks<toast_until) was_toast=1; else if(was_toast){ was_toast=0; dirty=1; }
            static u32 last_rtc; if(ticks-last_rtc>=1000){ last_rtc=ticks; int h,m,s; rtc_time(&h,&m,&s); if(m!=last_min) dirty=1; }
        }
        if(dirty && ticks-last_frame>=16){
            dirty=0; last_frame=ticks;
            compose(); gfx_present();
        }
        hlt();
    }
}
