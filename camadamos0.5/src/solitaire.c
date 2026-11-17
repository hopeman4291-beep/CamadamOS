/* CamadamOS Solitaire (Klondike, draw one) */
#include "kernel.h"

#define CW 64
#define CH 90
#define GAP 12
#define TAB_Y 118
typedef struct { u8 c[52], up[52]; int n; } pile_t;
enum { P_STOCK=0, P_WASTE=1, P_FOUND=2, P_TAB=6, P_COUNT=13 };
typedef struct {
    pile_t p[P_COUNT];
    int drag_pile, drag_idx, drag_ox, drag_oy, mx, my, dragging;
    int moves, won; u32 t0, won_t;
} sol_t;

static int is_red(int c){ return c/13<2; }
static int rank(int c){ return c%13; }
static int pile_x(int i){ if(i==P_STOCK) return GAP; if(i==P_WASTE) return GAP+(CW+GAP); if(i<P_TAB) return GAP+(i-P_FOUND+3)*(CW+GAP); return GAP+(i-P_TAB)*(CW+GAP); }
static int pile_y(int i){ return i<P_TAB?GAP:TAB_Y; }
static int fan_down=8, fan_up=22;
static void compute_fan(sol_t *s, int avail){
    int worst=0; for(int t=0;t<7;t++){ pile_t *p=&s->p[P_TAB+t]; int need=0; for(int k=0;k<p->n-1;k++) need+=p->up[k]?22:8; if(need>worst) worst=need; }
    fan_down=8; fan_up=22;
    if(worst+CH>avail-TAB_Y){ fan_up=imax(12,22*(avail-TAB_Y-CH)/imax(1,worst)); fan_down=imax(4,fan_up*8/22); }
}
static int card_y(sol_t *s,int pi,int k){
    int y=pile_y(pi); if(pi<P_TAB) return y;
    pile_t *p=&s->p[pi]; for(int j=0;j<k;j++) y+=p->up[j]?fan_up:fan_down; return y;
}

static void deal(sol_t *s){
    memset(s,0,sizeof *s);
    u8 d[52]; for(int i=0;i<52;i++) d[i]=i;
    for(int i=51;i>0;i--){ int j=rnd(i+1); u8 t=d[i]; d[i]=d[j]; d[j]=t; }
    int k=0;
    for(int t=0;t<7;t++) for(int j=0;j<=t;j++){ pile_t *p=&s->p[P_TAB+t]; p->c[p->n]=d[k++]; p->up[p->n]=(j==t); p->n++; }
    while(k<52){ pile_t *p=&s->p[P_STOCK]; p->c[p->n]=d[k++]; p->up[p->n]=0; p->n++; }
    s->drag_pile=-1; s->t0=ticks;
}

/* ---------- drawing ---------- */
static void suit_shape(int suit,int cx,int cy,int r,u32 col){
    if(suit==0){ /* heart */
        fill_circle(cx-r/2,cy-r/4,r/2+1,col); fill_circle(cx+r/2,cy-r/4,r/2+1,col);
        for(int i=0;i<=r+r/4;i++){ int w=(r+1)*(r+r/4-i)/(r+r/4); hline(cx-w,cy-r/4+i,2*w+1,col); }
    } else if(suit==1){ /* diamond */
        for(int dy=-r;dy<=r;dy++){ int w=(r-iabs(dy))*3/4; hline(cx-w,cy+dy,2*w+1,col); }
    } else if(suit==2){ /* club */
        fill_circle(cx,cy-r/2,r/2,col); fill_circle(cx-r/2-1,cy+r/6,r/2,col); fill_circle(cx+r/2+1,cy+r/6,r/2,col);
        for(int i=0;i<r*2/3+1;i++){ int w=1+i/2; hline(cx-w,cy+i,2*w+1,col); }
    } else { /* spade */
        for(int i=0;i<=r+r/4;i++){ int w=(r+1)*i/(r+r/4); hline(cx-w,cy-r+i,2*w+1,col); }
        fill_circle(cx-r/2,cy+r/4,r/2+1,col); fill_circle(cx+r/2,cy+r/4,r/2+1,col);
        for(int i=0;i<r*2/3+1;i++){ int w=1+i/2; hline(cx-w,cy+r/4+i,2*w+1,col); }
    }
}
static const char *rank_s[13]={"A","2","3","4","5","6","7","8","9","10","J","Q","K"};
static void draw_back(int x,int y){
    fill_round_rect(x,y,CW,CH,6,RGB(60,40,110));
    fill_round_rect(x+4,y+4,CW-8,CH-8,4,RGB(110,60,200));
    for(int j=y+8;j<y+CH-8;j+=6) for(int i=x+8;i<x+CW-8;i+=6) pset(i+((j/6)&1)*3,j,RGB(160,120,240));
    fill_circle(x+CW/2,y+CH/2,13,RGB(255,190,60)); fill_circle(x+CW/2,y+CH/2,9,RGB(110,60,200));
    draw_text_bold(x+CW/2-4,y+CH/2-8,"C",RGB(255,190,60));
}
static void draw_card(int c,int up,int x,int y){
    if(!up){ draw_back(x,y); return; }
    fill_round_rect(x,y,CW,CH,6,RGB(120,120,130));
    fill_round_rect(x+1,y+1,CW-2,CH-2,5,RGB(255,255,252));
    u32 col=is_red(c)?RGB(210,30,40):RGB(25,25,30);
    int r=rank(c), su=c/13;
    draw_text_bold(x+5,y+3,rank_s[r],col);
    suit_shape(su,x+9,y+27,5,col);
    if(r>=10){
        fill_rect(x+16,y+20,CW-30,CH-40,su<2?RGB(255,235,235):RGB(235,238,250));
        draw_rect(x+16,y+20,CW-30,CH-40,col);
        draw_text_big(x+CW/2-8,y+CH/2-20,rank_s[r],col,2);
        suit_shape(su,x+CW/2+2,y+CH/2+18,5,col);
    } else suit_shape(su,x+CW/2+4,y+CH/2+6,r==0?14:11,col);
    draw_text_bold(x+CW-5-text_width(rank_s[r]),y+CH-19,rank_s[r],col);
}
static void draw_slot(int x,int y,const char *lbl){
    fill_round_rect(x,y,CW,CH,6,RGB(30,110,55));
    draw_rect(x+3,y+3,CW-6,CH-6,RGB(70,160,95));
    if(lbl) draw_text_center(x,y+CH/2-8,CW,lbl,RGB(90,180,115));
}
static void s_draw(window_t *w){
    sol_t *s=w->st; int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(40,140,70),RGB(20,95,50));
    compute_fan(s,ch);
    for(int i=0;i<P_COUNT;i++){
        pile_t *p=&s->p[i]; int x=pile_x(i);
        int lim = (s->dragging && s->drag_pile==i) ? s->drag_idx : p->n;
        if(i==P_STOCK){ if(p->n) draw_back(x,pile_y(i)); else draw_slot(x,pile_y(i),"o"); continue; }
        if(i==P_WASTE||(i>=P_FOUND&&i<P_TAB)){
            if(lim>0) draw_card(p->c[lim-1],1,x,pile_y(i));
            else draw_slot(x,pile_y(i), i==P_WASTE?0:"A");
            continue;
        }
        if(lim==0) draw_slot(x,pile_y(i),"K");
        for(int k=0;k<lim;k++) draw_card(p->c[k],p->up[k],x,card_y(s,i,k));
    }
    /* new game button and status in the empty top slot */
    int bx=pile_x(P_WASTE)+CW+GAP;
    ui_button(bx,GAP+6,CW,26,"New",0);
    char b[24]; itoa10(s->moves,b); strcat0(b," moves",sizeof b);
    draw_text_center(bx-10,GAP+44,CW+20,b,RGB(220,255,225));
    if(s->dragging){
        pile_t *p=&s->p[s->drag_pile];
        for(int k=s->drag_idx;k<p->n;k++){
            int x=s->mx-s->drag_ox, y=s->my-s->drag_oy+(k-s->drag_idx)*fan_up;
            blend_rect(x+4,y+4,CW,CH,0,70);
            draw_card(p->c[k],1,x,y);
        }
    }
    if(s->won){
        int t=(int)(ticks-s->won_t);
        for(int i=0;i<13;i++){ int x=(i*41+t/6)%(cw-CW), y=(int)((i*53+t/4)%(ch-CH)); draw_card((i*17)%52,1,x,y); }
        fill_round_rect(cw/2-140,ch/2-30,280,60,10,RGB(255,250,235));
        draw_text_big(cw/2-112,ch/2-16,"You won!",RGB(110,60,200),2);
        draw_text(cw/2+30,ch/2-6,"Click New",RGB(80,80,90));
    }
}

/* ---------- rules ---------- */
static int can_found(sol_t *s,int c,int f){
    pile_t *p=&s->p[f];
    if(!p->n) return rank(c)==0;
    int t=p->c[p->n-1]; return t/13==c/13 && rank(c)==rank(t)+1;
}
static int can_tab(sol_t *s,int c,int t){
    pile_t *p=&s->p[t];
    if(!p->n) return rank(c)==12;
    int top=p->c[p->n-1]; return p->up[p->n-1] && is_red(top)!=is_red(c) && rank(c)==rank(top)-1;
}
static void flip_top(sol_t *s,int pi){ pile_t *p=&s->p[pi]; if(pi>=P_TAB && p->n && !p->up[p->n-1]) p->up[p->n-1]=1; }
static void move(sol_t *s,int from,int idx,int to){
    pile_t *a=&s->p[from], *b=&s->p[to];
    for(int k=idx;k<a->n;k++){ b->c[b->n]=a->c[k]; b->up[b->n]=1; b->n++; }
    a->n=idx; flip_top(s,from); s->moves++;
    int done=0; for(int f=0;f<4;f++) done+=s->p[P_FOUND+f].n;
    if(done==52 && !s->won){ s->won=1; s->won_t=ticks; }
}
static int auto_found(sol_t *s,int from){
    pile_t *a=&s->p[from]; if(!a->n || !a->up[a->n-1]) return 0;
    int c=a->c[a->n-1];
    for(int f=0;f<4;f++) if(can_found(s,c,P_FOUND+f)){ move(s,from,a->n-1,P_FOUND+f); return 1; }
    return 0;
}
static void s_mouse(window_t *w,int ev,int x,int y,int b){
    sol_t *s=w->st; (void)b;
    if(ev==EV_DOWN){
        if(b!=1) return;
        int bx=pile_x(P_WASTE)+CW+GAP;
        if(hit(x,y,bx,GAP+6,CW,26)){ deal(s); return; }
        if(s->won) return;
        if(hit(x,y,pile_x(P_STOCK),GAP,CW,CH)){
            pile_t *st=&s->p[P_STOCK], *wa=&s->p[P_WASTE];
            if(st->n){ wa->c[wa->n]=st->c[--st->n]; wa->up[wa->n++]=1; }
            else { while(wa->n){ st->c[st->n]=wa->c[--wa->n]; st->up[st->n++]=0; } }
            s->moves++; return;
        }
        for(int i=P_WASTE;i<P_COUNT;i++){
            pile_t *p=&s->p[i]; if(!p->n) continue;
            int px=pile_x(i);
            int start = i<P_TAB ? p->n-1 : 0;
            for(int k=p->n-1;k>=start;k--){
                int cy=card_y(s,i,k);
                int hgt = (i>=P_TAB && k<p->n-1) ? (p->up[k]?fan_up:fan_down) : CH;
                if(hit(x,y,px,cy,CW,hgt)){
                    if(!p->up[k]) return;
                    s->dragging=1; s->drag_pile=i; s->drag_idx=k; s->drag_ox=x-px; s->drag_oy=y-cy; s->mx=x; s->my=y;
                    return;
                }
            }
        }
    } else if(ev==EV_DBL){
        if(s->dragging && s->drag_idx==s->p[s->drag_pile].n-1){ int from=s->drag_pile; s->dragging=0; auto_found(s,from); }
    } else if(ev==EV_MOVE){
        if(s->dragging){ s->mx=x; s->my=y; }
    } else if(ev==EV_UP){
        if(!s->dragging) return;
        s->dragging=0;
        pile_t *a=&s->p[s->drag_pile]; int c=a->c[s->drag_idx];
        int dx=x-s->drag_ox+CW/2, dy=y-s->drag_oy+CH/2;  /* centre of dragged card */
        int best=-1, bd=1<<30;
        for(int i=P_FOUND;i<P_COUNT;i++){
            if(i==s->drag_pile) continue;
            int px=pile_x(i)+CW/2, py=card_y(s,i,imax(0,s->p[i].n-1))+CH/2;
            int d=(px-dx)*(px-dx)+(py-dy)*(py-dy);
            if(iabs(px-dx)<CW && iabs(py-dy)<CH+20 && d<bd){ bd=d; best=i; }
        }
        if(best<0) return;
        if(best<P_TAB){ if(s->drag_idx==a->n-1 && can_found(s,c,best)) move(s,s->drag_pile,s->drag_idx,best); }
        else if(can_tab(s,c,best)) move(s,s->drag_pile,s->drag_idx,best);
    }
}
static void s_tick(window_t *w){ sol_t *s=w->st; if(s->won) wm_invalidate(); }
void app_solitaire(void){
    window_t *w=wm_open("Solitaire",GAP+7*(CW+GAP),imin(560,scr_h-40-TITLE_H-BORDER-10),IC_SOLITAIRE); if(!w) return;
    sol_t *s=kzalloc(sizeof *s); deal(s);
    w->st=s; w->draw=s_draw; w->mouse=s_mouse; w->tick=s_tick;
}
