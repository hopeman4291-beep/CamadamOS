/* CamadamOS Chess - full rules + alpha-beta AI */
#include "kernel.h"

/* ---------- board representation ----------
   square 0..63, a8=0 ... h8=7, a1=56 ... h1=63 (row 0 = rank 8)
   piece: 0 empty, 1 P 2 N 3 B 4 R 5 Q 6 K, +8 for black */
enum { EMPTY=0, P=1, N, B, R, Q, K };
#define BLACK 8
#define COL(p) ((p)&8)
#define TYP(p) ((p)&7)
#define ROW(s) ((s)>>3)
#define FIL(s) ((s)&7)

typedef struct { u8 b[64]; u8 side; u8 castle; s8 ep; u8 half; u8 wk, bk; } pos_t;  /* castle: 1 WK 2 WQ 4 BK 8 BQ */
typedef struct { u8 from, to, promo, flags; } move_t;  /* flags: 1 capture 2 ep 4 castle 8 double */

static const int kdx[8]={1,2,2,1,-1,-2,-2,-1}, kdy[8]={2,1,-1,-2,-2,-1,1,2};
static const int ddx[8]={1,1,-1,-1,1,-1,0,0}, ddy[8]={1,-1,1,-1,0,0,1,-1};

static int onb(int f,int r){ return f>=0&&f<8&&r>=0&&r<8; }

static int attacked(const pos_t *p,int sq,int by){  /* by = 0 white / 8 black */
    int f=FIL(sq), r=ROW(sq);
    /* pawns: white pawns attack upward (row-1) */
    int pr = by ? r-1 : r+1;
    for(int d=-1;d<=1;d+=2) if(onb(f+d,pr)){ int q=p->b[pr*8+f+d]; if(q==(P|by)) return 1; }
    for(int i=0;i<8;i++){ int nf=f+kdx[i], nr=r+kdy[i]; if(onb(nf,nr) && p->b[nr*8+nf]==(N|by)) return 1; }
    for(int i=0;i<8;i++){ int nf=f+ddx[i], nr=r+ddy[i]; if(onb(nf,nr) && p->b[nr*8+nf]==(K|by)) return 1; }
    for(int i=0;i<8;i++){
        int nf=f+ddx[i], nr=r+ddy[i], diag=i<4;
        while(onb(nf,nr)){
            int q=p->b[nr*8+nf];
            if(q){ if(COL(q)==by && (TYP(q)==Q || TYP(q)==(diag?B:R))) return 1; break; }
            nf+=ddx[i]; nr+=ddy[i];
        }
    }
    return 0;
}
static int in_check(const pos_t *p,int side){ return attacked(p, side?p->bk:p->wk, side^8); }

static int add(move_t *m,int n,int from,int to,int fl,int promo){ m[n].from=from; m[n].to=to; m[n].flags=fl; m[n].promo=promo; return n+1; }

/* pseudo-legal moves; caponly = captures/promotions only */
static int gen(const pos_t *p,move_t *m,int caponly){
    int n=0, us=p->side, them=us^8;
    for(int s=0;s<64;s++){
        int pc=p->b[s]; if(!pc||COL(pc)!=us) continue;
        int f=FIL(s), r=ROW(s), t=TYP(pc);
        if(t==P){
            int dir = us ? 1 : -1, start = us ? 1 : 6, last = us ? 7 : 0;
            int nr=r+dir;
            if(onb(f,nr) && !p->b[nr*8+f]){
                if(nr==last){ for(int pr=Q;pr>=N;pr--) n=add(m,n,s,nr*8+f,0,pr); }
                else if(!caponly){
                    n=add(m,n,s,nr*8+f,0,0);
                    if(r==start && !p->b[(nr+dir)*8+f]) n=add(m,n,s,(nr+dir)*8+f,8,0);
                }
            }
            for(int d=-1;d<=1;d+=2){
                int nf=f+d; if(!onb(nf,nr)) continue;
                int to=nr*8+nf, q=p->b[to];
                if(q && COL(q)==them){
                    if(nr==last){ for(int pr=Q;pr>=N;pr--) n=add(m,n,s,to,1,pr); }
                    else n=add(m,n,s,to,1,0);
                } else if(to==p->ep) n=add(m,n,s,to,3,0);
            }
        } else if(t==N || t==K){
            for(int i=0;i<8;i++){
                int nf=f+(t==N?kdx[i]:ddx[i]), nr=r+(t==N?kdy[i]:ddy[i]);
                if(!onb(nf,nr)) continue;
                int to=nr*8+nf, q=p->b[to];
                if(q){ if(COL(q)==them) n=add(m,n,s,to,1,0); }
                else if(!caponly) n=add(m,n,s,to,0,0);
            }
            if(t==K && !caponly){
                int home = us ? 4 : 60;
                if(s==home && !in_check(p,us)){
                    int ks = us ? 4 : 1, qs = us ? 8 : 2;
                    if((p->castle&ks) && !p->b[s+1] && !p->b[s+2] && p->b[s+3]==(R|us) && !attacked(p,s+1,them) && !attacked(p,s+2,them))
                        n=add(m,n,s,s+2,4,0);
                    if((p->castle&qs) && !p->b[s-1] && !p->b[s-2] && !p->b[s-3] && p->b[s-4]==(R|us) && !attacked(p,s-1,them) && !attacked(p,s-2,them))
                        n=add(m,n,s,s-2,4,0);
                }
            }
        } else {
            int i0 = t==R ? 4 : 0, i1 = t==B ? 4 : 8;
            for(int i=i0;i<i1;i++){
                int nf=f+ddx[i], nr=r+ddy[i];
                while(onb(nf,nr)){
                    int to=nr*8+nf, q=p->b[to];
                    if(q){ if(COL(q)==them) n=add(m,n,s,to,1,0); break; }
                    if(!caponly) n=add(m,n,s,to,0,0);
                    nf+=ddx[i]; nr+=ddy[i];
                }
            }
        }
    }
    return n;
}

static const u8 castle_mask[64]={
    [0]=8,[4]=12,[7]=4,[56]=2,[60]=3,[63]=1 };

/* returns 0 if illegal (leaves own king in check) */
static int make(const pos_t *p,pos_t *o,move_t m){
    *o=*p;
    int pc=o->b[m.from], us=p->side;
    o->b[m.to]=pc; o->b[m.from]=0;
    if(m.flags&2) o->b[m.to + (us?-8:8)]=0;
    if(m.promo) o->b[m.to]=m.promo|us;
    if(m.flags&4){
        if(m.to>m.from){ o->b[m.from+1]=o->b[m.from+3]; o->b[m.from+3]=0; }
        else { o->b[m.from-1]=o->b[m.from-4]; o->b[m.from-4]=0; }
    }
    if(TYP(pc)==K){ if(us) o->bk=m.to; else o->wk=m.to; }
    o->castle &= ~(castle_mask[m.from]|castle_mask[m.to]);
    o->ep = (m.flags&8) ? (m.from+m.to)/2 : -1;
    o->half = (TYP(pc)==P || (m.flags&1)) ? 0 : (p->half<255 ? p->half+1 : 255);
    o->side = us^8;
    return !in_check(o,us);
}

static int legal_moves(const pos_t *p,move_t *out){
    move_t m[256]; int n=gen(p,m,0), k=0; pos_t t;
    for(int i=0;i<n;i++) if(make(p,&t,m[i])) out[k++]=m[i];
    return k;
}

/* ---------- evaluation ---------- */
static const int val[7]={0,100,320,330,500,900,0};
static const s8 pst_p[64]={ 0,0,0,0,0,0,0,0, 50,50,50,50,50,50,50,50, 10,10,20,30,30,20,10,10, 5,5,10,25,25,10,5,5,
    0,0,0,20,20,0,0,0, 5,-5,-10,0,0,-10,-5,5, 5,10,10,-20,-20,10,10,5, 0,0,0,0,0,0,0,0 };
static const s8 pst_n[64]={ -50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,0,10,15,15,10,0,-30, -30,5,15,20,20,15,5,-30,
    -30,0,15,20,20,15,0,-30, -30,5,10,15,15,10,5,-30, -40,-20,0,5,5,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50 };
static const s8 pst_b[64]={ -20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,10,10,5,0,-10, -10,5,5,10,10,5,5,-10,
    -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20 };
static const s8 pst_r[64]={ 0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5,
    -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, 0,0,0,5,5,0,0,0 };
static const s8 pst_q[64]={ -20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5,
    0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,5,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20 };
static const s8 pst_k[64]={ -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10, 20,20,0,0,0,0,20,20, 20,30,10,0,0,10,30,20 };
static const s8 pst_ke[64]={ -50,-40,-30,-20,-20,-30,-40,-50, -30,-20,-10,0,0,-10,-20,-30, -30,-10,20,30,30,20,-10,-30, -30,-10,30,40,40,30,-10,-30,
    -30,-10,30,40,40,30,-10,-30, -30,-10,20,30,30,20,-10,-30, -30,-30,0,0,0,0,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50 };
static const s8 *const pst[7]={0,pst_p,pst_n,pst_b,pst_r,pst_q,pst_k};

static int evaluate(const pos_t *p){
    int s=0, mat=0;
    for(int i=0;i<64;i++){ int q=p->b[i]; if(q && TYP(q)!=P && TYP(q)!=K) mat+=val[TYP(q)]; }
    int endgame = mat <= 1400;
    for(int i=0;i<64;i++){
        int q=p->b[i]; if(!q) continue;
        int t=TYP(q), idx = COL(q) ? ((7-ROW(i))*8+FIL(i)) : i;   /* mirror for black */
        int v = val[t] + ((t==K && endgame) ? pst_ke[idx] : pst[t][idx]);
        s += COL(q) ? -v : v;
    }
    return p->side ? -s : s;
}

/* ---------- search ---------- */
#define MATE 30000
static u32 nodes, t_start, t_limit; static int aborted;

static int mvv(const pos_t *p,move_t m){
    int s=0;
    if(m.flags&1){ int v = (m.flags&2) ? P : TYP(p->b[m.to]); s = 10000 + val[v]*10 - val[TYP(p->b[m.from])]/10; }
    if(m.promo) s += 9000 + val[m.promo];
    return s;
}
static void order(const pos_t *p,move_t *m,int n,move_t *first){
    int sc[256];
    for(int i=0;i<n;i++){
        sc[i]=mvv(p,m[i]);
        if(first && m[i].from==first->from && m[i].to==first->to && m[i].promo==first->promo) sc[i]=1000000;
    }
    for(int i=1;i<n;i++){ move_t mm=m[i]; int s=sc[i], j=i-1; while(j>=0 && sc[j]<s){ m[j+1]=m[j]; sc[j+1]=sc[j]; j--; } m[j+1]=mm; sc[j+1]=s; }
}
static void check_time(void){ if((nodes&1023)==0 && ticks-t_start>t_limit) aborted=1; }

static int quiesce(const pos_t *p,int alpha,int beta,int qd){
    nodes++; check_time(); if(aborted) return 0;
    int stand=evaluate(p);
    if(stand>=beta) return stand;
    if(stand>alpha) alpha=stand;
    if(qd>6) return stand;
    move_t m[256]; int n=gen(p,m,1); order(p,m,n,0);
    pos_t t;
    for(int i=0;i<n;i++){
        if(!make(p,&t,m[i])) continue;
        int sc=-quiesce(&t,-beta,-alpha,qd+1);
        if(aborted) return 0;
        if(sc>=beta) return sc;
        if(sc>alpha) alpha=sc;
    }
    return alpha;
}

static int search(const pos_t *p,int depth,int alpha,int beta,int ply){
    if(p->half>=100) return 0;
    int chk=in_check(p,p->side);
    if(chk) depth++;
    if(depth<=0) return quiesce(p,alpha,beta,0);
    nodes++; check_time(); if(aborted) return 0;
    move_t m[256]; int n=gen(p,m,0); order(p,m,n,0);
    pos_t t; int legal=0, best=-MATE-1;
    for(int i=0;i<n;i++){
        if(!make(p,&t,m[i])) continue;
        legal++;
        int sc=-search(&t,depth-1,-beta,-alpha,ply+1);
        if(aborted) return 0;
        if(sc>best) best=sc;
        if(sc>alpha) alpha=sc;
        if(alpha>=beta) break;
    }
    if(!legal) return chk ? -MATE+ply : 0;
    return best;
}

/* root: iterative deepening up to maxd; noise adds randomness (easy) */
static move_t think(const pos_t *p,int maxd,int noise,u32 limit){
    move_t m[256]; int n=legal_moves(p,m);
    move_t best=m[0], prev={0,0,0,0}; int haveprev=0;
    t_start=ticks; t_limit=limit; nodes=0; aborted=0;
    for(int d=1; d<=maxd; d++){
        order(p,m,n,haveprev?&prev:0);
        int alpha=-MATE-1, beta=MATE+1; move_t cur=m[0]; int bestsc=-MATE-1;
        pos_t t;
        for(int i=0;i<n;i++){
            make(p,&t,m[i]);
            int sc=-search(&t,d-1,-beta,-alpha,1);
            if(aborted) break;
            if(noise) sc += (int)rnd(noise*2+1) - noise;
            if(sc>bestsc){ bestsc=sc; cur=m[i]; }
            if(sc>alpha) alpha=sc;
        }
        if(aborted && d>1) break;   /* keep previous depth's result */
        best=cur; prev=cur; haveprev=1;
        if(bestsc>MATE-100) break;   /* found a mate */
        if(aborted) break;
    }
    return best;
}

/* ---------- SAN notation ---------- */
static void sq_name(int s,char *o){ o[0]='a'+FIL(s); o[1]='8'-ROW(s); o[2]=0; }
static void san(const pos_t *p,move_t mv,char *o){
    static const char L[]=" PNBRQK";
    int pc=TYP(p->b[mv.from]); int k=0; char sq[3];
    if(mv.flags&4){ strncpy0(o, mv.to>mv.from ? "O-O" : "O-O-O", 8); k=strlen(o); }
    else {
        if(pc!=P){
            o[k++]=L[pc];
            move_t lm[256]; int n=legal_moves(p,lm), samef=0, samer=0, other=0;
            for(int i=0;i<n;i++) if(lm[i].to==mv.to && lm[i].from!=mv.from && TYP(p->b[lm[i].from])==pc){
                other=1; if(FIL(lm[i].from)==FIL(mv.from)) samef=1; if(ROW(lm[i].from)==ROW(mv.from)) samer=1; }
            if(other){ if(!samef) o[k++]='a'+FIL(mv.from); else if(!samer) o[k++]='8'-ROW(mv.from); else { sq_name(mv.from,sq); o[k++]=sq[0]; o[k++]=sq[1]; } }
        } else if(mv.flags&1) o[k++]='a'+FIL(mv.from);
        if(mv.flags&1) o[k++]='x';
        sq_name(mv.to,sq); o[k++]=sq[0]; o[k++]=sq[1];
        if(mv.promo){ o[k++]='='; o[k++]=L[mv.promo]; }
    }
    pos_t t; make(p,&t,mv);
    if(in_check(&t,t.side)){ move_t lm[256]; o[k++] = legal_moves(&t,lm) ? '+' : '#'; }
    o[k]=0;
}

/* ---------- pieces art (silhouettes; outline generated) ---------- */
static const char *const art[7][16]={ {0},
 { "................","................","................","......xxxx......",".....xxxxxx.....",".....xxxxxx.....","......xxxx......",".....xxxxxx.....",
   "......xxxx......","......xxxx......",".....xxxxxx.....","....xxxxxxxx....","...oooooooooo...","...xxxxxxxxxx...","...xxxxxxxxxx...","................" },
 { "................","................","......x.x.......",".....xxxxxx.....","....xxxoxxxx....","...xxxxxxxxxx...","..xxxxxxxxxxx...","..xxo..xxxxxx...",
   "......xxxxxxx...",".....xxxxxxx....","....xxxxxxxx....","....xxxxxxxx....","...oooooooooo...","..xxxxxxxxxxxx..","..xxxxxxxxxxxx..","................" },
 { "................",".......xx.......","......xxxx......",".....xxxxoxx....",".....xxxoxxx....",".....xxoxxxx....","......xxxx......",".......xx.......",
   "......xxxx......","......xxxx......",".....xxxxxx.....","....xxxxxxxx....","...oooooooooo...","..xxxxxxxxxxxx..","..xxxxxxxxxxxx..","................" },
 { "................","................","...xx..xx..xx...","...xxxxxxxxxx...","...xxxxxxxxxx...","....oooooooo....",".....xxxxxx.....",".....xxxxxx.....",
   ".....xxxxxx.....",".....xxxxxx.....","....xxxxxxxx....","...xxxxxxxxxx...","..oooooooooooo..","..xxxxxxxxxxxx..","..xxxxxxxxxxxx..","................" },
 { "................",".x....x..x....x.","..x..xx..xx..x..","..xx.xx..xx.xx..","..xxxxxxxxxxxx..","...xxxxxxxxxx...","...xxxxxxxxxx...","....oooooooo....",
   ".....xxxxxx.....",".....xxxxxx.....","....xxxxxxxx....","....xxxxxxxx....","...oooooooooo...","..xxxxxxxxxxxx..","..xxxxxxxxxxxx..","................" },
 { "................",".......xx.......","......xxxx......",".......xx.......","..xxx..xx..xxx..",".xxxxx.xx.xxxxx.",".xxxxxxxxxxxxxx.",".xxxxxxxxxxxxxx.",
   "..xxxxxxxxxxxx..","...oooooooooo...","....xxxxxxxx....","....xxxxxxxx....","...oooooooooo...","..xxxxxxxxxxxx..","..xxxxxxxxxxxx..","................" } };

static int filled(const char *const *a,int x,int y){ if(x<0||y<0||x>15||y>15) return 0; char c=a[y][x]; return c=='x'||c=='o'; }
static void draw_piece(int x,int y,int pc,int sc){
    const char *const *a=art[TYP(pc)];
    int black=COL(pc);
    u32 fill = black ? RGB(52,52,62) : RGB(250,247,238);
    u32 shade= black ? RGB(36,36,44) : RGB(222,215,200);
    u32 line = black ? RGB(10,10,14) : RGB(40,36,34);
    u32 det  = black ? RGB(150,150,165) : RGB(70,64,60);
    for(int r=0;r<16;r++) for(int c=0;c<16;c++){
        char ch=a[r][c]; u32 col; int draw=1;
        if(ch=='x') col = c>=10 ? shade : fill;
        else if(ch=='o') col = det;
        else if(filled(a,c-1,r)||filled(a,c+1,r)||filled(a,c,r-1)||filled(a,c,r+1)) col=line;
        else draw=0;
        if(draw) fill_rect(x+c*sc,y+r*sc,sc,sc,col);
    }
}

/* ---------- app state ---------- */
#define SQ 60
#define BX 26
#define BY 12
#define PX (BX+8*SQ+18)
#define MAXH 600
typedef struct {
    pos_t pos; pos_t hist[MAXH]; int nh;        /* hist[i] = position before ply i */
    char sanl[MAXH][8];
    int sel; move_t legal[256]; int nlegal;
    int lastf, lastt;
    int level;          /* 0 easy 1 medium 2 hard */
    int human;          /* 0 = human plays white */
    int flip;
    int state;          /* 0 play, 1 ai pending, 2 over */
    u32 think_at;
    char result[48];
    int promo_pending; move_t promo_move;
    int scroll;
} chess_t;

static void refresh(chess_t *c){ c->nlegal=legal_moves(&c->pos,c->legal); }

static int same_pos(const pos_t *a,const pos_t *b){ return !memcmp(a->b,b->b,64) && a->side==b->side && a->castle==b->castle && a->ep==b->ep; }

static void check_end(chess_t *c){
    refresh(c);
    const char *who = c->pos.side ? "White" : "Black";
    if(c->nlegal==0){
        if(in_check(&c->pos,c->pos.side)){ strncpy0(c->result,"Checkmate - ",48); strcat0(c->result,who,48); strcat0(c->result," wins",48); }
        else strncpy0(c->result,"Stalemate - draw",48);
        c->state=2; return;
    }
    if(c->pos.half>=100){ strncpy0(c->result,"Draw - 50 move rule",48); c->state=2; return; }
    int reps=0; for(int i=0;i<c->nh;i++) if(same_pos(&c->hist[i],&c->pos)) reps++;
    if(reps>=2){ strncpy0(c->result,"Draw - repetition",48); c->state=2; return; }
    int minor=0, other=0;
    for(int i=0;i<64;i++){ int t=TYP(c->pos.b[i]); if(t==N||t==B) minor++; else if(t==P||t==R||t==Q) other++; }
    if(!other && minor<=1){ strncpy0(c->result,"Draw - not enough pieces",48); c->state=2; return; }
}

static void do_move(chess_t *c,move_t m){
    if(c->nh>=MAXH) return;
    san(&c->pos,m,c->sanl[c->nh]);
    c->hist[c->nh++]=c->pos;
    int cap=m.flags&1;
    pos_t t; make(&c->pos,&t,m); c->pos=t;
    c->lastf=m.from; c->lastt=m.to; c->sel=-1;
    int chk=in_check(&c->pos,c->pos.side);
    c->state=0; check_end(c);
    if(c->state==2) piano_note(c->result[0]=='C'?523:392,250);
    else if(chk) piano_note(880,90);
    else piano_note(cap?330:660,cap?70:40);
    int rows=(c->nh+1)/2; if(rows>16) c->scroll=rows-16; else c->scroll=0;
    if(c->state!=2 && (c->pos.side ? 1 : 0) != c->human){ c->state=1; c->think_at=ticks+120; }
    wm_invalidate();
}

static void new_game(chess_t *c){
    static const u8 back[8]={R,N,B,Q,K,B,N,R};
    memset(&c->pos,0,sizeof c->pos);
    for(int f=0;f<8;f++){ c->pos.b[f]=back[f]|BLACK; c->pos.b[8+f]=P|BLACK; c->pos.b[48+f]=P; c->pos.b[56+f]=back[f]; }
    c->pos.castle=15; c->pos.ep=-1; c->pos.wk=60; c->pos.bk=4; c->pos.side=0;
    c->nh=0; c->sel=-1; c->lastf=c->lastt=-1; c->state=0; c->result[0]=0; c->promo_pending=0; c->scroll=0;
    c->flip=c->human;
    refresh(c);
    if(c->human){ c->state=1; c->think_at=ticks+300; }
    wm_invalidate();
}

static int sq_xy(chess_t *c,int s,int *x,int *y){
    int f=FIL(s), r=ROW(s); if(c->flip){ f=7-f; r=7-r; }
    *x=BX+f*SQ; *y=BY+r*SQ; return 0;
}
static int xy_sq(chess_t *c,int x,int y){
    if(x<BX||y<BY||x>=BX+8*SQ||y>=BY+8*SQ) return -1;
    int f=(x-BX)/SQ, r=(y-BY)/SQ; if(c->flip){ f=7-f; r=7-r; }
    return r*8+f;
}

static const char *const lvl_names[3]={"Easy","Medium","Hard"};
#define BTN_W 100
static void ch_draw(window_t *w){
    chess_t *c=w->st; int cw=win_cw(w), chh=win_ch(w);
    fill_rect(0,0,cw,chh,RGB(46,42,40));
    /* board */
    int kingsq = in_check(&c->pos,c->pos.side) ? (c->pos.side?c->pos.bk:c->pos.wk) : -1;
    for(int s=0;s<64;s++){
        int x,y; sq_xy(c,s,&x,&y);
        int light=((FIL(s)+ROW(s))&1)==0;
        u32 col = light ? RGB(240,217,181) : RGB(181,136,99);
        if(s==c->lastf||s==c->lastt) col=mix(col,RGB(230,220,60),110);
        if(s==c->sel) col=mix(col,RGB(90,160,90),150);
        fill_rect(x,y,SQ,SQ,col);
        if(s==kingsq){ for(int r=26;r>4;r-=3) fill_circle(x+SQ/2,y+SQ/2,r,mix(col,RGB(230,30,30),255-r*8)); }
        if(c->pos.b[s]) draw_piece(x+6,y+6,c->pos.b[s],3);
    }
    if(c->sel>=0) for(int i=0;i<c->nlegal;i++) if(c->legal[i].from==c->sel){
        int x,y; sq_xy(c,c->legal[i].to,&x,&y);
        if(c->pos.b[c->legal[i].to]){ for(int r=0;r<4;r++) draw_circle(x+SQ/2,y+SQ/2,27-r,RGB(70,120,70)); }
        else fill_circle(x+SQ/2,y+SQ/2,8,RGB(80,130,80));
    }
    draw_rect(BX-1,BY-1,8*SQ+2,8*SQ+2,RGB(20,18,16));
    for(int i=0;i<8;i++){
        char s[2]={0,0};
        s[0] = c->flip ? '1'+i : '8'-i; draw_text(BX-14,BY+i*SQ+SQ/2-8,s,RGB(200,190,170));
        s[0] = c->flip ? 'h'-i : 'a'+i; draw_text(BX+i*SQ+SQ/2-4,BY+8*SQ+2,s,RGB(200,190,170));
    }
    /* promotion chooser */
    if(c->promo_pending){
        blend_rect(BX,BY,8*SQ,8*SQ,0,140);
        int bx=BX+4*SQ-2*SQ-10, by=BY+4*SQ-50;
        fill_round_rect(bx-10,by-30,4*SQ+40,SQ+50,10,RGB(250,248,240));
        draw_text_center(bx-10,by-24,4*SQ+40,"Promote to:",RGB(40,40,50));
        static const int pp[4]={Q,R,B,N};
        for(int i=0;i<4;i++){ fill_round_rect(bx+i*(SQ+5),by,SQ,SQ,6,RGB(225,215,195)); draw_piece(bx+i*(SQ+5)+6,by+6,pp[i]|c->pos.side,3); }
    }
    /* panel */
    int px=PX, pw=cw-PX-14;
    draw_text_big(px,8,"Chess",RGB(250,240,220),2);
    const char *st; char buf[48];
    if(c->state==2) st=c->result;
    else if(c->state==1){ strncpy0(buf,"Thinking (",48); strcat0(buf,lvl_names[c->level],48); strcat0(buf,")...",48); st=buf; }
    else { st = c->pos.side ? "Black to move" : "White to move"; if(kingsq>=0){ strncpy0(buf,st,48); strcat0(buf," - check!",48); st=buf; } }
    fill_round_rect(px,48,pw,28,6,c->state==2?RGB(200,150,40):RGB(70,64,60));
    draw_text_center(px,54,pw,st,RGB(255,250,240));
    draw_text(px,86,"Computer level:",RGB(200,190,170));
    for(int i=0;i<3;i++){ int bw=(pw-8)/3; ui_button(px+i*(bw+4),106,bw,26,lvl_names[i],c->level==i); }
    ui_button(px,140,(pw-4)/2,26,"New game",0);
    ui_button(px+(pw-4)/2+4,140,(pw-4)/2,26,"Undo",0);
    ui_button(px,172,pw,26,c->human?"Play as White":"Play as Black",0);
    /* moves list */
    fill_round_rect(px,206,pw,chh-218,6,RGB(250,247,240));
    int rows=(c->nh+1)/2;
    for(int r=c->scroll, y=212; r<rows && y<chh-30; r++, y+=17){
        char n[8]; itoa10(r+1,n); strcat0(n,".",8);
        draw_text(px+6,y,n,RGB(130,120,110));
        draw_text_bold(px+44,y,c->sanl[r*2],RGB(30,30,40));
        if(r*2+1<c->nh) draw_text_bold(px+44+(pw-50)/2,y,c->sanl[r*2+1],RGB(30,30,40));
    }
    if(!c->nh) draw_text(px+8,214,"Moves appear here",RGB(150,140,130));
}

static void start_ai_if_needed(chess_t *c){
    if(c->state!=2 && (c->pos.side?1:0)!=c->human){ c->state=1; c->think_at=ticks+120; }
}

static void ch_mouse(window_t *w,int ev,int x,int y,int b){
    chess_t *c=w->st; (void)b;
    if(ev==EV_WHEEL){ int rows=(c->nh+1)/2; c->scroll=clampi(c->scroll+b*2,0,imax(0,rows-16)); wm_invalidate(); return; }
    if(ev!=EV_DOWN) return;
    int cw=win_cw(w), px=PX, pw=cw-PX-14;
    if(c->promo_pending){
        int bx=BX+4*SQ-2*SQ-10, by=BY+4*SQ-50; static const int pp[4]={Q,R,B,N};
        for(int i=0;i<4;i++) if(hit(x,y,bx+i*(SQ+5),by,SQ,SQ)){ move_t m=c->promo_move; m.promo=pp[i]; c->promo_pending=0; do_move(c,m); return; }
        c->promo_pending=0; c->sel=-1; wm_invalidate(); return;
    }
    for(int i=0;i<3;i++){ int bw=(pw-8)/3; if(hit(x,y,px+i*(bw+4),106,bw,26)){ c->level=i; wm_invalidate(); return; } }
    if(hit(x,y,px,140,(pw-4)/2,26)){ new_game(c); return; }
    if(hit(x,y,px+(pw-4)/2+4,140,(pw-4)/2,26)){
        if(c->state==1) return;
        /* undo back to the human's previous turn */
        int n; for(n=c->nh-1;n>=0;n--) if((c->hist[n].side?1:0)==c->human) break;
        if(n<0) return;
        c->pos=c->hist[n]; c->nh=n;
        c->sel=-1; c->lastf=c->lastt=-1; c->state=0; c->result[0]=0; refresh(c);
        int rows=(c->nh+1)/2; c->scroll = rows>16 ? rows-16 : 0;
        start_ai_if_needed(c);
        wm_invalidate(); return;
    }
    if(hit(x,y,px,172,pw,26)){ c->human^=1; new_game(c); return; }
    if(c->state!=0) return;
    int s=xy_sq(c,x,y); if(s<0) return;
    if(c->sel>=0){
        for(int i=0;i<c->nlegal;i++) if(c->legal[i].from==c->sel && c->legal[i].to==s){
            if(c->legal[i].promo){ c->promo_pending=1; c->promo_move=c->legal[i]; wm_invalidate(); return; }
            do_move(c,c->legal[i]); return;
        }
    }
    int pc=c->pos.b[s];
    c->sel = (pc && COL(pc)==c->pos.side && s!=c->sel) ? s : -1;
    wm_invalidate();
}

static void ch_tick(window_t *w){
    chess_t *c=w->st;
    if(c->state!=1 || (int)(ticks-c->think_at)<0) return;
    static const int depth[3]={2,4,6}; static const int noise[3]={60,10,0}; static const u32 lim[3]={1000,2500,5000};
    move_t m=think(&c->pos,depth[c->level],noise[c->level],lim[c->level]);
    do_move(c,m);
}
static void ch_key(window_t *w,int k){
    chess_t *c=w->st;
    if(k=='n'||k=='N') new_game(c);
    if(k==KEY_ESC){ c->sel=-1; c->promo_pending=0; wm_invalidate(); }
}

void app_chess(void){
    window_t *w=wm_open("Chess",PX+240,8*SQ+34,IC_CHESS); if(!w) return;
    chess_t *c=kzalloc(sizeof *c); c->level=1; c->human=0;
    w->st=c; w->draw=ch_draw; w->mouse=ch_mouse; w->tick=ch_tick; w->key=ch_key;
    new_game(c);
}

/* perft for self-test (terminal: chess perft N) */
static u32 perft(const pos_t *p,int d){
    if(!d) return 1;
    move_t m[256]; int n=gen(p,m,0); u32 s=0; pos_t t;
    for(int i=0;i<n;i++) if(make(p,&t,m[i])) s+=perft(&t,d-1);
    return s;
}
u32 chess_perft(int d){
    chess_t *c=kzalloc(sizeof *c); new_game(c); u32 r=perft(&c->pos,d); kfree(c); return r;
}
