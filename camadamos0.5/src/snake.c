/* CamadamOS Snake */
#include "kernel.h"

#define GW 30
#define GH 22
#define CELL 18
#define HUD 36
typedef struct { s8 x[GW*GH], y[GW*GH]; int len, dx, dy, ndx, ndy, fx, fy, score, state; u32 last; int speed; } snake_t;
static int best;
static void place_food(snake_t *s){
    for(;;){ int x=rnd(GW), y=rnd(GH), ok=1; for(int i=0;i<s->len;i++) if(s->x[i]==x&&s->y[i]==y) ok=0; if(ok){ s->fx=x; s->fy=y; return; } }
}
static void reset(snake_t *s){
    memset(s,0,sizeof *s); s->len=4;
    for(int i=0;i<4;i++){ s->x[i]=10-i; s->y[i]=GH/2; }
    s->dx=s->ndx=1; s->speed=130; place_food(s);
}
static void sn_draw(window_t *w){
    snake_t *s=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,HUD,RGB(30,40,30));
    char b[48], n[12]; itoa10(s->score,n); strncpy0(b,"Score ",sizeof b); strcat0(b,n,sizeof b);
    draw_text_bold(12,10,b,RGB(200,255,200));
    itoa10(best,n); strncpy0(b,"Best ",sizeof b); strcat0(b,n,sizeof b); draw_text(cw-text_width(b)-12,10,b,RGB(160,200,160));
    int ox=0, oy=HUD;
    for(int y=0;y<GH;y++) for(int x=0;x<GW;x++) fill_rect(ox+x*CELL,oy+y*CELL,CELL,CELL,((x+y)&1)?RGB(150,200,90):RGB(160,210,100));
    fill_circle(ox+s->fx*CELL+CELL/2,oy+s->fy*CELL+CELL/2,CELL/2-2,RGB(220,40,40));
    pset(ox+s->fx*CELL+CELL/2+1,oy+s->fy*CELL+2,RGB(60,140,40)); fill_rect(ox+s->fx*CELL+CELL/2,oy+s->fy*CELL+1,2,4,RGB(90,60,20));
    for(int i=s->len-1;i>=0;i--){
        u32 c=i==0?RGB(40,70,200):mix(RGB(60,100,230),RGB(110,160,255),(i*40)%255);
        fill_round_rect(ox+s->x[i]*CELL+1,oy+s->y[i]*CELL+1,CELL-2,CELL-2,5,c);
    }
    int hx=ox+s->x[0]*CELL, hy=oy+s->y[0]*CELL;
    int ex1,ey1,ex2,ey2;
    if(s->dx){ ex1=ex2=hx+(s->dx>0?12:5); ey1=hy+5; ey2=hy+12; } else { ey1=ey2=hy+(s->dy>0?12:5); ex1=hx+5; ex2=hx+12; }
    fill_circle(ex1,ey1,2,0xFFFFFF); fill_circle(ex2,ey2,2,0xFFFFFF);
    if(s->state!=1){
        fill_round_rect(cw/2-150,ch/2-40,300,80,12,RGB(255,255,255));
        draw_text_big(cw/2-(s->state==2?72:40),ch/2-30,s->state==2?"Game over":"Snake",RGB(40,110,40),2);
        draw_text_center(cw/2-150,ch/2+12,300,"Arrow keys to steer - Space to start",RGB(80,80,90));
    }
}
static void sn_key(window_t *w,int k){
    snake_t *s=w->st;
    if(s->state!=1){ if(k==' '||k=='\n'){ reset(s); s->state=1; s->last=ticks; } return; }
    if(k==KEY_LEFT && s->dx!=1){ s->ndx=-1; s->ndy=0; }
    if(k==KEY_RIGHT && s->dx!=-1){ s->ndx=1; s->ndy=0; }
    if(k==KEY_UP && s->dy!=1){ s->ndx=0; s->ndy=-1; }
    if(k==KEY_DOWN && s->dy!=-1){ s->ndx=0; s->ndy=1; }
}
static void sn_tick(window_t *w){
    snake_t *s=w->st;
    if(s->state!=1 || ticks-s->last<(u32)s->speed) return;
    s->last=ticks; s->dx=s->ndx; s->dy=s->ndy;
    int nx=s->x[0]+s->dx, ny=s->y[0]+s->dy;
    int dead = nx<0||ny<0||nx>=GW||ny>=GH;
    for(int i=0;i<s->len-1 && !dead;i++) if(s->x[i]==nx&&s->y[i]==ny) dead=1;
    if(dead){ s->state=2; if(s->score>best) best=s->score; piano_note(150,400); wm_invalidate(); return; }
    int grow=(nx==s->fx&&ny==s->fy);
    if(grow && s->len<GW*GH) s->len++;
    for(int i=s->len-1;i>0;i--){ s->x[i]=s->x[i-1]; s->y[i]=s->y[i-1]; }
    s->x[0]=nx; s->y[0]=ny;
    if(grow){ s->score+=10; if(s->speed>60) s->speed-=4; place_food(s); piano_note(1200,40); }
    wm_invalidate();
}
void app_snake(void){
    window_t *w=wm_open("Snake",GW*CELL,HUD+GH*CELL,IC_SNAKE); if(!w) return;
    snake_t *s=kzalloc(sizeof *s); reset(s); s->state=0;
    w->st=s; w->draw=sn_draw; w->key=sn_key; w->tick=sn_tick;
}
