/* CamadamOS games: Tetris, Pong, Breakout, 2048, and the Games folder */
#include "kernel.h"

/* =========================== Tetris =========================== */
#define TW 10
#define TH 20
#define TC 26
static const u16 shapes[7][4]={ /* 4x4 bitmasks per rotation */
 {0x0F00,0x2222,0x00F0,0x4444},{0x8E00,0x6440,0x0E20,0x44C0},{0x2E00,0x4460,0x0E80,0xC440},
 {0x6600,0x6600,0x6600,0x6600},{0x6C00,0x4620,0x06C0,0x8C40},{0x4E00,0x4640,0x0E40,0x4C40},{0xC600,0x2640,0x0C60,0x4C80}};
static const u32 tcolors[8]={0,RGB(40,200,230),RGB(50,90,220),RGB(240,150,40),RGB(240,210,50),RGB(70,200,90),RGB(160,80,210),RGB(225,60,60)};
typedef struct { u8 b[TH][TW]; int cur,rot,x,y,next,hold,held,score,lines,level,state; u32 last; int flash; u32 flash_t; int best; } tet_t;
static int tbest;
static int cell(int s,int r,int i,int j){ return (shapes[s][r]>>(15-(i*4+j)))&1; }
static int fits(tet_t *t,int s,int r,int x,int y){
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) if(cell(s,r,i,j)){ int X=x+j, Y=y+i; if(X<0||X>=TW||Y>=TH) return 0; if(Y>=0 && t->b[Y][X]) return 0; }
    return 1;
}
static void spawn(tet_t *t){ t->cur=t->next; t->next=rnd(7); t->rot=0; t->x=3; t->y=-1; t->held=0; if(!fits(t,t->cur,0,3,-1)){ t->state=2; if(t->score>tbest) tbest=t->score; piano_note(120,500); } }
static void lock_piece(tet_t *t){
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) if(cell(t->cur,t->rot,i,j)){ int Y=t->y+i; if(Y>=0) t->b[Y][t->x+j]=t->cur+1; }
    int cleared=0;
    for(int y=TH-1;y>=0;y--){ int full=1; for(int x=0;x<TW;x++) if(!t->b[y][x]) full=0;
        if(full){ cleared++; for(int k=y;k>0;k--) memcpy(t->b[k],t->b[k-1],TW); memset(t->b[0],0,TW); y++; } }
    static const int pts[5]={0,100,300,500,800};
    t->score+=pts[cleared]*t->level; t->lines+=cleared;
    if(cleared){ piano_note(cleared==4?1568:1046,90); t->level=1+t->lines/10; } else piano_note(220,20);
    spawn(t);
}
static void tet_reset(tet_t *t){ memset(t,0,sizeof *t); t->next=rnd(7); t->hold=-1; t->level=1; spawn(t); t->state=0; }
static void tblock(int x,int y,u32 c,int ghost){
    if(ghost){ draw_rect(x+1,y+1,TC-2,TC-2,mix(c,0,60)); return; }
    fill_rect(x+1,y+1,TC-2,TC-2,c); hline(x+1,y+1,TC-2,mix(c,0xFFFFFF,120)); vline(x+1,y+1,TC-2,mix(c,0xFFFFFF,120));
    hline(x+1,y+TC-2,TC-2,mix(c,0,90)); vline(x+TC-2,y+1,TC-2,mix(c,0,90));
}
static void mini(int s,int x,int y){ if(s<0) return; for(int i=0;i<4;i++) for(int j=0;j<4;j++) if(cell(s,0,i,j)) { fill_rect(x+j*16,y+i*16,15,15,tcolors[s+1]); } }
static void tet_draw(window_t *w){
    tet_t *t=w->st; int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(20,18,40),RGB(10,8,24));
    int ox=12, oy=12;
    fill_rect(ox-2,oy-2,TW*TC+4,TH*TC+4,RGB(70,64,110)); fill_rect(ox,oy,TW*TC,TH*TC,RGB(14,12,28));
    for(int y=0;y<TH;y++) for(int x=0;x<TW;x++){ if(t->b[y][x]) tblock(ox+x*TC,oy+y*TC,tcolors[t->b[y][x]],0); else pset(ox+x*TC+TC/2,oy+y*TC+TC/2,RGB(40,36,70)); }
    if(t->state==1){
        int gy=t->y; while(fits(t,t->cur,t->rot,t->x,gy+1)) gy++;
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) if(cell(t->cur,t->rot,i,j)){
            if(gy+i>=0) tblock(ox+(t->x+j)*TC,oy+(gy+i)*TC,tcolors[t->cur+1],1);
            if(t->y+i>=0) tblock(ox+(t->x+j)*TC,oy+(t->y+i)*TC,tcolors[t->cur+1],0);
        }
    }
    int px=ox+TW*TC+20; char b[24];
    draw_text_bold(px,14,"NEXT",RGB(200,190,240)); mini(t->next,px,36);
    draw_text_bold(px,110,"HOLD (C)",RGB(200,190,240)); mini(t->hold,px,132);
    draw_text_bold(px,210,"SCORE",RGB(200,190,240)); itoa10(t->score,b); draw_text_big(px,228,b,RGB(255,255,255),2);
    draw_text_bold(px,270,"LINES",RGB(200,190,240)); itoa10(t->lines,b); draw_text(px,288,b,RGB(255,255,255));
    draw_text_bold(px,314,"LEVEL",RGB(200,190,240)); itoa10(t->level,b); draw_text(px,332,b,RGB(255,255,255));
    draw_text_bold(px,358,"BEST",RGB(200,190,240)); itoa10(tbest,b); draw_text(px,376,b,RGB(255,255,255));
    draw_text(px,420,"Arrows: move",RGB(140,130,180)); draw_text(px,438,"Up: rotate",RGB(140,130,180)); draw_text(px,456,"Space: drop",RGB(140,130,180)); draw_text(px,474,"P: pause",RGB(140,130,180));
    if(t->state!=1){
        fill_round_rect(ox+10,oy+200,TW*TC-20,100,10,RGB(255,255,255));
        draw_text_big(ox+TW*TC/2-(t->state==2?72:(t->state==3?48:56)),oy+214,t->state==2?"Game over":t->state==3?"Paused":"TETRIS",accent_dark(),2);
        draw_text_center(ox,oy+262,TW*TC,"Press Space to play",RGB(80,80,90));
    }
}
static void tet_key(window_t *w,int k){
    tet_t *t=w->st;
    if(t->state==0||t->state==2){ if(k==' '||k=='\n'){ int best=tbest; tet_reset(t); tbest=best; t->state=1; t->last=ticks; } return; }
    if(k=='p'||k=='P'||k==KEY_ESC){ t->state = t->state==3?1:3; return; }
    if(t->state!=1) return;
    if(k==KEY_LEFT && fits(t,t->cur,t->rot,t->x-1,t->y)) t->x--;
    else if(k==KEY_RIGHT && fits(t,t->cur,t->rot,t->x+1,t->y)) t->x++;
    else if(k==KEY_DOWN){ if(fits(t,t->cur,t->rot,t->x,t->y+1)){ t->y++; t->score++; } }
    else if(k==KEY_UP||k=='x'){ int r=(t->rot+1)%4; static const int kick[5]={0,-1,1,-2,2}; for(int i=0;i<5;i++) if(fits(t,t->cur,r,t->x+kick[i],t->y)){ t->x+=kick[i]; t->rot=r; break; } }
    else if(k==' '){ while(fits(t,t->cur,t->rot,t->x,t->y+1)){ t->y++; t->score+=2; } lock_piece(t); t->last=ticks; }
    else if((k=='c'||k=='C') && !t->held){ int h=t->hold; t->hold=t->cur; if(h<0) spawn(t); else { t->cur=h; t->rot=0; t->x=3; t->y=-1; } t->held=1; }
}
static void tet_tick(window_t *w){
    tet_t *t=w->st; if(t->state!=1) return;
    u32 speed=(u32)imax(80,800-(t->level-1)*70);
    if(ticks-t->last<speed) return;
    t->last=ticks;
    if(fits(t,t->cur,t->rot,t->x,t->y+1)) t->y++; else lock_piece(t);
    wm_invalidate();
}
void app_tetris(void){
    window_t *w=wm_open("Tetris",12+TW*TC+150,TH*TC+24,IC_TETRIS); if(!w) return;
    tet_t *t=kzalloc(sizeof *t); tet_reset(t);
    w->st=t; w->draw=tet_draw; w->key=tet_key; w->tick=tet_tick;
}

/* =========================== Pong =========================== */
#define PW 640
#define PH 400
typedef struct { int ly, ry, bx, by, vx, vy, ls, rs, state, two, keys_l, keys_r; u32 last; int mouse_y, use_mouse; } pong_t;
static void pong_serve(pong_t *p,int dir){ p->bx=PW/2*256; p->by=PH/2*256; p->vx=dir*3*256; p->vy=(rnd(5)-2)*256; }
static void pong_draw(window_t *w){
    pong_t *p=w->st;
    fill_rect(0,0,PW,PH,RGB(10,30,20));
    for(int y=0;y<PH;y+=20) fill_rect(PW/2-2,y,4,10,RGB(60,120,80));
    char b[8]; itoa10(p->ls,b); draw_text_big(PW/2-80,20,b,RGB(120,240,150),3); itoa10(p->rs,b); draw_text_big(PW/2+56,20,b,RGB(120,240,150),3);
    fill_rect(20,p->ly/256-35,10,70,RGB(230,255,230)); fill_rect(PW-30,p->ry/256-35,10,70,RGB(230,255,230));
    fill_rect(p->bx/256-6,p->by/256-6,12,12,RGB(255,255,255));
    if(p->state!=1){
        fill_round_rect(PW/2-170,PH/2-50,340,110,12,RGB(255,255,255));
        draw_text_big(PW/2-(p->state==2?(p->ls>p->rs?88:96):40),PH/2-40,p->state==2?(p->ls>p->rs?"You win!":"CPU wins"):"PONG",RGB(20,90,50),2);
        draw_text_center(PW/2-170,PH/2,340,p->two?"2 players: W/S and Up/Down":"Mouse or Up/Down to move",RGB(80,80,90));
        draw_text_center(PW/2-170,PH/2+20,340,"Space: start   2: toggle 2 players",RGB(80,80,90));
    }
}
static void pong_key(window_t *w,int k){
    pong_t *p=w->st;
    if(p->state!=1){ if(k==' '||k=='\n'){ p->ls=p->rs=0; p->state=1; pong_serve(p,1); p->last=ticks; } if(k=='2'){ p->two=!p->two; } return; }
    int step=24*256;
    if(p->two){ if(k=='w'||k=='W') p->ly-=step; if(k=='s'||k=='S') p->ly+=step; if(k==KEY_UP) p->ry-=step; if(k==KEY_DOWN) p->ry+=step; }
    else { if(k==KEY_UP||k=='w') { p->ly-=step; p->use_mouse=0; } if(k==KEY_DOWN||k=='s'){ p->ly+=step; p->use_mouse=0; } }
    p->ly=clampi(p->ly,35*256,(PH-35)*256); p->ry=clampi(p->ry,35*256,(PH-35)*256);
}
static void pong_mouse(window_t *w,int ev,int x,int y,int b){ pong_t *p=w->st; (void)x; (void)b; if(ev==EV_MOVE && !p->two){ p->ly=clampi(y,35,PH-35)*256; p->use_mouse=1; } if(ev==EV_DOWN && p->state!=1) pong_key(w,' '); }
static void pong_tick(window_t *w){
    pong_t *p=w->st; if(p->state!=1) return;
    if(ticks-p->last<16) return; p->last=ticks;
    p->bx+=p->vx; p->by+=p->vy;
    if(p->by<6*256){ p->by=6*256; p->vy=-p->vy; piano_note(600,15); }
    if(p->by>(PH-6)*256){ p->by=(PH-6)*256; p->vy=-p->vy; piano_note(600,15); }
    if(!p->two){ int target=p->by, d=target-p->ry; int sp=(3+p->rs/2)*256; if(p->vx<0) d=(PH/2*256-p->ry)/8; p->ry+=clampi(d,-sp,sp); p->ry=clampi(p->ry,35*256,(PH-35)*256); }
    int bx=p->bx/256, by=p->by/256;
    if(p->vx<0 && bx-6<=30 && bx-6>=18 && iabs(by-p->ly/256)<=40){ p->vx=-p->vx*21/20; p->vy=(by-p->ly/256)*256/6; piano_note(880,20); }
    if(p->vx>0 && bx+6>=PW-30 && bx+6<=PW-18 && iabs(by-p->ry/256)<=40){ p->vx=-p->vx*21/20; p->vy=(by-p->ry/256)*256/6; piano_note(880,20); }
    if(p->vx>12*256) p->vx=12*256; if(p->vx<-12*256) p->vx=-12*256;
    if(bx<0){ p->rs++; piano_note(200,150); pong_serve(p,1); }
    if(bx>PW){ p->ls++; piano_note(1200,150); pong_serve(p,-1); }
    if(p->ls>=7||p->rs>=7) p->state=2;
    wm_invalidate();
}
void app_pong(void){
    window_t *w=wm_open("Pong",PW,PH,IC_PONG); if(!w) return;
    pong_t *p=kzalloc(sizeof *p); p->ly=p->ry=PH/2*256; pong_serve(p,1);
    w->st=p; w->draw=pong_draw; w->key=pong_key; w->mouse=pong_mouse; w->tick=pong_tick;
}

/* =========================== Breakout =========================== */
#define BW 560
#define BH 440
#define BR 6
#define BCOLS 10
typedef struct { u8 bricks[BR][BCOLS]; int px, bx, by, vx, vy, lives, score, level, state, stuck; u32 last; } brk_t;
static const u32 brow[BR]={RGB(230,60,60),RGB(240,140,40),RGB(240,210,50),RGB(70,190,90),RGB(50,140,230),RGB(150,80,210)};
static void brk_level(brk_t *b){ for(int r=0;r<BR;r++) for(int c=0;c<BCOLS;c++) b->bricks[r][c]=1+(r<b->level-1?1:0); b->stuck=1; }
static void brk_draw(window_t *w){
    brk_t *b=w->st;
    gradient_v(0,0,BW,BH,RGB(16,16,40),RGB(40,20,60));
    int bw=BW/BCOLS;
    for(int r=0;r<BR;r++) for(int c=0;c<BCOLS;c++) if(b->bricks[r][c]){
        u32 col=brow[r]; if(b->bricks[r][c]>1) col=mix(col,0xFFFFFF,90);
        fill_round_rect(c*bw+2,50+r*22+2,bw-4,18,4,col); }
    fill_round_rect(b->px-40,BH-30,80,12,6,RGB(230,230,250));
    fill_circle(b->bx/256,b->by/256,6,RGB(255,255,255));
    char s[40], n[12]; strncpy0(s,"Score ",sizeof s); itoa10(b->score,n); strcat0(s,n,sizeof s); draw_text_bold(10,14,s,RGB(255,255,255));
    strncpy0(s,"Level ",sizeof s); itoa10(b->level,n); strcat0(s,n,sizeof s); draw_text_center(0,14,BW,s,RGB(255,255,255));
    for(int i=0;i<b->lives;i++) fill_circle(BW-20-i*18,22,6,RGB(255,120,140));
    if(b->state!=1){
        fill_round_rect(BW/2-160,BH/2-40,320,90,12,RGB(255,255,255));
        draw_text_big(BW/2-(b->state==2?72:72),BH/2-30,b->state==2?"Game over":"BREAKOUT",accent_dark(),2);
        draw_text_center(BW/2-160,BH/2+14,320,"Mouse or arrows to move, click/Space",RGB(80,80,90));
    } else if(b->stuck) draw_text_center(0,BH/2,BW,"Click or press Space to launch",RGB(200,200,230));
}
static void brk_launch(brk_t *b){ if(b->stuck){ b->stuck=0; b->vx=(rnd(2)?1:-1)*3*256; b->vy=-4*256-b->level*64; } }
static void brk_start(brk_t *b){ memset(b,0,sizeof *b); b->lives=3; b->level=1; b->px=BW/2; b->state=1; brk_level(b); }
static void brk_key(window_t *w,int k){
    brk_t *b=w->st;
    if(b->state!=1){ if(k==' '||k=='\n') brk_start(b); return; }
    if(k==KEY_LEFT) b->px=imax(40,b->px-30); if(k==KEY_RIGHT) b->px=imin(BW-40,b->px+30);
    if(k==' ') brk_launch(b);
}
static void brk_mouse(window_t *w,int ev,int x,int y,int bt){ brk_t *b=w->st; (void)y; (void)bt; if(ev==EV_MOVE) b->px=clampi(x,40,BW-40); if(ev==EV_DOWN){ if(b->state!=1) brk_start(b); else brk_launch(b); } }
static void brk_tick(window_t *w){
    brk_t *b=w->st; if(b->state!=1) return;
    if(ticks-b->last<16) return; b->last=ticks;
    if(b->stuck){ b->bx=b->px*256; b->by=(BH-38)*256; wm_invalidate(); return; }
    b->bx+=b->vx; b->by+=b->vy;
    int x=b->bx/256, y=b->by/256;
    if(x<6){ b->bx=6*256; b->vx=-b->vx; } if(x>BW-6){ b->bx=(BW-6)*256; b->vx=-b->vx; }
    if(y<40){ b->by=40*256; b->vy=-b->vy; }
    if(b->vy>0 && y+6>=BH-30 && y+6<=BH-18 && iabs(x-b->px)<=46){ b->vy=-b->vy; b->vx=(x-b->px)*256/7; piano_note(700,15); }
    int bw=BW/BCOLS, r=(y-50)/22, c=x/bw;
    if(y>=50 && r>=0 && r<BR && c>=0 && c<BCOLS && b->bricks[r][c]){
        b->bricks[r][c]--; b->vy=-b->vy; b->score+=(BR-r)*10; piano_note(400+(BR-r)*120,25);
        int left=0; for(int i=0;i<BR;i++) for(int j=0;j<BCOLS;j++) left+=b->bricks[i][j];
        if(!left){ b->level++; brk_level(b); piano_note(1568,200); }
    }
    if(y>BH){ b->lives--; piano_note(150,300); if(b->lives<=0) b->state=2; else b->stuck=1; }
    wm_invalidate();
}
void app_breakout(void){
    window_t *w=wm_open("Breakout",BW,BH,IC_BREAKOUT); if(!w) return;
    brk_t *b=kzalloc(sizeof *b); brk_start(b); b->state=0;
    w->st=b; w->draw=brk_draw; w->key=brk_key; w->mouse=brk_mouse; w->tick=brk_tick;
}

/* =========================== 2048 =========================== */
typedef struct { int g[4][4], score, state; } g2048_t;
static int best2048;
static void add_tile(g2048_t *g){ int e[16], n=0; for(int i=0;i<16;i++) if(!g->g[i/4][i%4]) e[n++]=i; if(n){ int k=e[rnd(n)]; g->g[k/4][k%4]=rnd(10)?2:4; } }
static int slide(g2048_t *g,int dir){ /* 0 left 1 right 2 up 3 down */
    int moved=0;
    for(int line=0;line<4;line++){
        int v[4], k=0;
        for(int i=0;i<4;i++){ int r,c; if(dir<2){ r=line; c=dir==0?i:3-i; } else { c=line; r=dir==2?i:3-i; } if(g->g[r][c]) v[k++]=g->g[r][c]; }
        int out[4]={0,0,0,0}, o=0;
        for(int i=0;i<k;i++){ if(i+1<k && v[i]==v[i+1]){ out[o++]=v[i]*2; g->score+=v[i]*2; if(v[i]*2==2048 && g->state==0) g->state=2; i++; } else out[o++]=v[i]; }
        for(int i=0;i<4;i++){ int r,c; if(dir<2){ r=line; c=dir==0?i:3-i; } else { c=line; r=dir==2?i:3-i; } if(g->g[r][c]!=out[i]) moved=1; g->g[r][c]=out[i]; }
    }
    return moved;
}
static int can_move(g2048_t *g){ for(int r=0;r<4;r++) for(int c=0;c<4;c++){ if(!g->g[r][c]) return 1; if(c<3&&g->g[r][c]==g->g[r][c+1]) return 1; if(r<3&&g->g[r][c]==g->g[r+1][c]) return 1; } return 0; }
static u32 tile_col(int v){
    switch(v){ case 2: return RGB(238,228,218); case 4: return RGB(237,224,200); case 8: return RGB(242,177,121); case 16: return RGB(245,149,99);
    case 32: return RGB(246,124,95); case 64: return RGB(246,94,59); case 128: return RGB(237,207,114); case 256: return RGB(237,204,97);
    case 512: return RGB(237,200,80); case 1024: return RGB(237,197,63); case 2048: return RGB(237,194,46); }
    return RGB(60,58,50);
}
static void g_draw(window_t *w){
    g2048_t *g=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(250,248,239));
    draw_text_big(16,12,"2048",RGB(119,110,101),3);
    char b[24]; itoa10(g->score,b); fill_round_rect(cw-220,12,96,46,6,RGB(187,173,160)); draw_text_center(cw-220,16,96,"SCORE",RGB(238,228,218)); draw_text_center(cw-220,34,96,b,RGB(255,255,255));
    itoa10(best2048,b); fill_round_rect(cw-116,12,100,46,6,RGB(187,173,160)); draw_text_center(cw-116,16,100,"BEST",RGB(238,228,218)); draw_text_center(cw-116,34,100,b,RGB(255,255,255));
    int ox=16, oy=72, s=(cw-32-5*10)/4;
    fill_round_rect(ox,oy,cw-32,cw-32,8,RGB(187,173,160));
    for(int r=0;r<4;r++) for(int c=0;c<4;c++){
        int x=ox+10+c*(s+10), y=oy+10+r*(s+10), v=g->g[r][c];
        fill_round_rect(x,y,s,s,6,v?tile_col(v):RGB(205,193,180));
        if(v){ itoa10(v,b); int sc=v<100?3:v<1000?2:2; if(v>=1000) sc=1+ (s>=96); u32 tc=v<=4?RGB(119,110,101):RGB(249,246,242);
            draw_text_big(x+s/2-text_width(b)*sc/2,y+s/2-8*sc,b,tc,sc); }
    }
    if(g->state){ blend_rect(ox,oy,cw-32,cw-32,0xFFFFFF,150);
        draw_text_big(cw/2-(g->state==1?72:64),oy+(cw-32)/2-30,g->state==1?"Game over":"You win!",RGB(119,110,101),2);
        draw_text_center(0,oy+(cw-32)/2+10,cw,g->state==1?"Press Space for a new game":"Keep going with the arrows, or Space",RGB(119,110,101)); }
    draw_text_center(0,ch-22,cw,"Use the arrow keys to slide the tiles.",RGB(150,140,130));
}
static void g_key(window_t *w,int k){
    g2048_t *g=w->st;
    if(k==' '){ memset(g,0,sizeof *g); add_tile(g); add_tile(g); return; }
    int d=-1; if(k==KEY_LEFT) d=0; if(k==KEY_RIGHT) d=1; if(k==KEY_UP) d=2; if(k==KEY_DOWN) d=3;
    if(d<0 || g->state==1) return;
    if(g->state==2) g->state=3;
    if(slide(g,d)){ add_tile(g); piano_note(500,15); }
    if(g->score>best2048) best2048=g->score;
    if(!can_move(g)) g->state=1;
}
void app_2048(void){
    window_t *w=wm_open("2048",420,500,IC_2048); if(!w) return;
    g2048_t *g=kzalloc(sizeof *g); add_tile(g); add_tile(g);
    w->st=g; w->draw=g_draw; w->key=g_key;
}

/* =========================== Games folder =========================== */
typedef struct { const char *name; int icon; void (*fn)(void); } game_t;
static void l_m(void){ app_mines(); } static void l_s(void){ app_solitaire(); } static void l_sn(void){ app_snake(); }
static const game_t games[]={{"Minesweeper",IC_MINES,l_m},{"Solitaire",IC_SOLITAIRE,l_s},{"Snake",IC_SNAKE,l_sn},
    {"Tetris",IC_TETRIS,app_tetris},{"Pong",IC_PONG,app_pong},{"Breakout",IC_BREAKOUT,app_breakout},{"2048",IC_2048,app_2048},{"Chess",IC_CHESS,app_chess}};
#define NG ((int)(sizeof games/sizeof games[0]))
static void gf_draw(window_t *w){
    int cw=win_cw(w), ch=win_ch(w); (void)ch;
    fill_rect(0,0,cw,ch,RGB(250,249,253));
    for(int i=0;i<NG;i++){ int x=16+(i%4)*110, y=14+(i/4)*104; draw_icon(games[i].icon,x+25,y,2,1); draw_text_center(x,y+50,100,games[i].name,RGB(30,30,45)); }
}
static void gf_mouse(window_t *w,int ev,int x,int y,int b){ (void)w; (void)b; if(ev!=EV_DBL && ev!=EV_DOWN) return;
    for(int i=0;i<NG;i++){ int gx=16+(i%4)*110, gy=14+(i/4)*104; if(hit(x,y,gx,gy,100,80) && ev==EV_DBL){ games[i].fn(); return; } } }
void app_games(void){
    window_t *w=wm_open("Games",4*110+22,2*104+20,IC_TETRIS); if(!w) return;
    w->draw=gf_draw; w->mouse=gf_mouse;
}
