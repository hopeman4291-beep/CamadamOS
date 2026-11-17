/* CamadamOS Minesweeper */
#include "kernel.h"

#define MW 30
#define MH 16
#define CS 24
typedef struct {
    int cols, rows, mines, level;
    u8 mine[MH][MW], open[MH][MW], flag[MH][MW], cnt[MH][MW];
    int state; /* 0 fresh, 1 playing, 2 lost, 3 won */
    u32 t0; int secs; int flags; int opened;
    int boom_x, boom_y;
} mines_t;

static const int lv_c[3]={9,16,30}, lv_r[3]={9,16,16}, lv_m[3]={10,40,99};
static int gx(mines_t *m, window_t *w){ return (win_cw(w)-m->cols*CS)/2; }
#define GY 84

static void resize(window_t *w, mines_t *m){
    int cw=imax(m->cols*CS+24, 300), ch=GY+m->rows*CS+14;
    w->w=cw+2*BORDER; w->h=ch+TITLE_H+BORDER;
    if(w->x+w->w>scr_w) w->x=imax(0,scr_w-w->w);
    if(w->y+w->h>scr_h-40) w->y=imax(0,scr_h-40-w->h);
}
static void new_game(mines_t *m, int level){
    int l=level; memset(m,0,sizeof *m);
    m->level=l; m->cols=lv_c[l]; m->rows=lv_r[l]; m->mines=lv_m[l];
}
static void place(mines_t *m, int sx, int sy){
    int n=0;
    while(n<m->mines){
        int x=rnd(m->cols), y=rnd(m->rows);
        if(m->mine[y][x] || (iabs(x-sx)<=1 && iabs(y-sy)<=1)) continue;
        m->mine[y][x]=1; n++;
    }
    for(int y=0;y<m->rows;y++) for(int x=0;x<m->cols;x++){
        int c=0; for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){
            int nx=x+dx, ny=y+dy; if(nx>=0&&ny>=0&&nx<m->cols&&ny<m->rows&&m->mine[ny][nx]) c++; }
        m->cnt[y][x]=c;
    }
}
static void reveal(mines_t *m, int x, int y){
    static s16 stk[MW*MH*2][2]; int sp=0;
    stk[sp][0]=x; stk[sp][1]=y; sp++;
    while(sp){
        sp--; int cx=stk[sp][0], cy=stk[sp][1];
        if(cx<0||cy<0||cx>=m->cols||cy>=m->rows) continue;
        if(m->open[cy][cx]||m->flag[cy][cx]) continue;
        m->open[cy][cx]=1; m->opened++;
        if(m->mine[cy][cx]){ m->state=2; m->boom_x=cx; m->boom_y=cy; return; }
        if(m->cnt[cy][cx]==0)
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) if((dx||dy) && sp<MW*MH*2-1){ stk[sp][0]=cx+dx; stk[sp][1]=cy+dy; sp++; }
    }
}
static void check_win(mines_t *m){
    if(m->state==1 && m->opened==m->cols*m->rows-m->mines){
        m->state=3;
        for(int y=0;y<m->rows;y++) for(int x=0;x<m->cols;x++) if(m->mine[y][x]) m->flag[y][x]=1;
        m->flags=m->mines;
    }
}

static void draw_digits(int x,int y,int v){
    fill_rect(x,y,56,30,RGB(20,10,10));
    char b[8]; if(v<0) v=0; if(v>999) v=999;
    b[0]='0'+v/100; b[1]='0'+(v/10)%10; b[2]='0'+v%10; b[3]=0;
    draw_text_big(x+4,y-1,b,RGB(255,50,40),2);
}
static void draw_face(int cx,int cy,int st){
    fill_circle(cx,cy,13,RGB(255,215,40)); draw_circle(cx,cy,13,RGB(120,90,0));
    if(st==2){ draw_line(cx-7,cy-6,cx-3,cy-2,0); draw_line(cx-3,cy-6,cx-7,cy-2,0); draw_line(cx+3,cy-6,cx+7,cy-2,0); draw_line(cx+7,cy-6,cx+3,cy-2,0);
        draw_circle(cx,cy+7,3,0); }
    else if(st==3){ fill_rect(cx-9,cy-6,7,4,0); fill_rect(cx+2,cy-6,7,4,0); hline(cx-2,cy-5,4,0);
        draw_line(cx-5,cy+4,cx,cy+7,0); draw_line(cx,cy+7,cx+5,cy+4,0); }
    else { fill_circle(cx-4,cy-4,2,0); fill_circle(cx+4,cy-4,2,0);
        draw_line(cx-5,cy+4,cx-2,cy+6,0); hline(cx-2,cy+6,5,0); draw_line(cx+2,cy+6,cx+5,cy+4,0); }
}
static const u32 numc[9]={0,RGB(30,80,230),RGB(30,140,40),RGB(220,30,30),RGB(20,20,140),RGB(130,20,20),RGB(20,140,140),RGB(0,0,0),RGB(110,110,110)};
static void m_draw(window_t *w){
    mines_t *m=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,ch,RGB(205,208,218));
    static const char *ln[3]={"Easy","Medium","Hard"};
    for(int i=0;i<3;i++) ui_button(10+i*78,6,72,24,ln[i],m->level==i);
    fill_round_rect(8,36,cw-16,42,6,RGB(185,188,200));
    draw_digits(16,42,m->mines-m->flags);
    draw_digits(cw-72,42,m->secs);
    draw_face(cw/2,57,m->state);
    int ox=gx(m,w);
    fill_rect(ox-3,GY-3,m->cols*CS+6,m->rows*CS+6,RGB(130,132,145));
    for(int y=0;y<m->rows;y++) for(int x=0;x<m->cols;x++){
        int px=ox+x*CS, py=GY+y*CS;
        int show = m->open[y][x] || (m->state==2 && m->mine[y][x] && !m->flag[y][x]);
        if(show){
            fill_rect(px,py,CS,CS,(m->state==2&&x==m->boom_x&&y==m->boom_y)?RGB(240,70,60):RGB(225,228,235));
            draw_rect(px,py,CS,CS,RGB(175,178,190));
            if(m->mine[y][x]){ fill_circle(px+12,py+12,6,RGB(20,20,20)); hline(px+4,py+12,17,0); vline(px+12,py+4,17,0); pset(px+10,py+10,0xFFFFFF); pset(px+10,py+9,0xFFFFFF); }
            else if(m->cnt[y][x]){ char s[2]={(char)('0'+m->cnt[y][x]),0}; draw_text_bold(px+8,py+4,s,numc[m->cnt[y][x]]); }
        } else {
            fill_rect(px,py,CS,CS,RGB(190,195,210));
            hline(px,py,CS,RGB(245,246,250)); vline(px,py,CS,RGB(245,246,250));
            hline(px,py+CS-1,CS,RGB(120,124,140)); vline(px+CS-1,py,CS,RGB(120,124,140));
            hline(px+1,py+CS-2,CS-1,RGB(150,154,170)); vline(px+CS-2,py+1,CS-1,RGB(150,154,170));
            if(m->flag[y][x]){
                vline(px+11,py+5,13,0); fill_rect(px+7,py+17,10,2,0);
                for(int i=0;i<7;i++){ int wd=8-iabs(i-3)*2; hline(px+11-wd,py+5+i,wd,RGB(230,30,30)); }
                if(m->state==2 && !m->mine[y][x]){ draw_line(px+4,py+4,px+19,py+19,RGB(200,0,0)); draw_line(px+19,py+4,px+4,py+19,RGB(200,0,0)); }
            }
        }
    }
    if(m->state==3){ fill_round_rect(cw/2-90,GY+m->rows*CS/2-18,180,36,8,RGB(60,170,90)); draw_text_center(cw/2-90,GY+m->rows*CS/2-8,180,"You won! :)",0xFFFFFF); }
}
static void m_mouse(window_t *w,int ev,int x,int y,int b){
    mines_t *m=w->st;
    if(ev!=EV_DOWN) return;
    for(int i=0;i<3;i++) if(b==1 && hit(x,y,10+i*78,6,72,24)){ new_game(m,i); resize(w,m); wm_set_title(w,"Minesweeper"); return; }
    if(b==1 && hit(x,y,win_cw(w)/2-14,43,28,28)){ new_game(m,m->level); return; }
    if(m->state>=2) return;
    int ox=gx(m,w);
    if(x<ox||y<GY) return;
    int cx=(x-ox)/CS, cy=(y-GY)/CS;
    if(cx>=m->cols||cy>=m->rows) return;
    if(b==2){
        if(!m->open[cy][cx]){ m->flag[cy][cx]^=1; m->flags+=m->flag[cy][cx]?1:-1; }
        return;
    }
    if(m->state==0){ place(m,cx,cy); m->state=1; m->t0=ticks; }
    if(m->open[cy][cx] && m->cnt[cy][cx]){ /* chord */
        int f=0; for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ int nx=cx+dx,ny=cy+dy; if(nx>=0&&ny>=0&&nx<m->cols&&ny<m->rows&&m->flag[ny][nx]) f++; }
        if(f==m->cnt[cy][cx]) for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ if(m->state!=1) break; reveal(m,cx+dx,cy+dy); }
    } else if(!m->flag[cy][cx]) reveal(m,cx,cy);
    check_win(m);
}
static void m_tick(window_t *w){
    mines_t *m=w->st;
    if(m->state==1){ int s=(int)((ticks-m->t0)/1000); if(s!=m->secs){ m->secs=s; wm_invalidate(); } }
}
static void m_key(window_t *w,int k){ mines_t *m=w->st; if(k=='n'||k=='N'||k==KEY_F1+100) new_game(m,m->level); }
void app_mines(void){
    window_t *w=wm_open("Minesweeper",300,300,IC_MINES); if(!w) return;
    mines_t *m=kzalloc(sizeof *m); new_game(m,0);
    w->st=m; w->draw=m_draw; w->mouse=m_mouse; w->tick=m_tick; w->key=m_key;
    resize(w,m);
}
