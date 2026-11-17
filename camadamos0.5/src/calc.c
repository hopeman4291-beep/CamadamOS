/* CamadamOS Calculator (fixed-point, 6 decimal places) */
#include "kernel.h"

#define SCALE 1000000LL
typedef struct { s64 acc, cur; int op, entering, decimals, error; char disp[32]; char hist[48]; } calc_t;
static const char *keys[5][4]={{"C","+/-","%","/"},{"7","8","9","x"},{"4","5","6","-"},{"1","2","3","+"},{"0",".","<","="}};
#define KW 70
#define KH 48
#define TOP 96

static void fmt(s64 v,char *o){
    int neg=v<0; u64 u=neg?(u64)(-v):(u64)v;
    u64 ip=u/SCALE, fp=u%SCALE;
    char t[32]; int k=0;
    if(ip==0) t[k++]='0'; else { char r[24]; int m=0; while(ip){ r[m++]='0'+(int)(ip%10); ip/=10; } while(m) t[k++]=r[--m]; }
    if(fp){ t[k++]='.'; char f[7]; for(int i=5;i>=0;i--){ f[i]='0'+(int)(fp%10); fp/=10; } int e=6; while(e>0 && f[e-1]=='0') e--; for(int i=0;i<e;i++) t[k++]=f[i]; }
    t[k]=0;
    if(neg){ o[0]='-'; strncpy0(o+1,t,31); } else strncpy0(o,t,32);
}
static void show(calc_t *c,s64 v){ fmt(v,c->disp); }
static s64 mul(s64 a,s64 b){
    int neg=(a<0)!=(b<0); u64 ua=a<0?-(u64)a:(u64)a, ub=b<0?-(u64)b:(u64)b;
    u64 ia=ua/SCALE, fa=ua%SCALE, ib=ub/SCALE, fb=ub%SCALE;
    u64 r=ia*ib*SCALE + ia*fb + fa*ib + fa*fb/SCALE;
    return neg?-(s64)r:(s64)r;
}
static s64 divide(calc_t *c,s64 a,s64 b){
    if(b==0){ c->error=1; return 0; }
    int neg=(a<0)!=(b<0); u64 ua=a<0?-(u64)a:(u64)a, ub=b<0?-(u64)b:(u64)b;
    u64 q=ua/ub, r=ua%ub, res=q*SCALE, place=SCALE/10;
    for(int i=0;i<6;i++){ r*=10; res+=(r/ub)*place; r%=ub; place/=10; }
    return neg?-(s64)res:(s64)res;
}
static s64 apply(calc_t *c,s64 a,s64 b,int op){
    switch(op){ case '+': return a+b; case '-': return a-b; case 'x': return mul(a,b); case '/': return divide(c,a,b); }
    return b;
}
static void press(calc_t *c,const char *k){
    if(c->error && k[0]!='C'){ return; }
    char ch=k[0];
    if(ch>='0'&&ch<='9'){
        if(!c->entering){ c->cur=0; c->entering=1; c->decimals=0; }
        int d=ch-'0';
        if(c->decimals){ if(c->decimals<=6){ s64 p=SCALE; for(int i=0;i<c->decimals;i++) p/=10; c->cur+= (c->cur<0?-1:1)*d*p; c->decimals++; } }
        else if(c->cur < 100000000000LL*SCALE/1000) c->cur=c->cur*10+(c->cur<0?-d:d)*SCALE;
        show(c,c->cur);
        if(c->decimals==2 && c->disp[strlen(c->disp)-1]!='0' && d==0) strcat0(c->disp,".0",32);
        return;
    }
    if(ch=='.'){ if(!c->entering){ c->cur=0; c->entering=1; } if(!c->decimals){ c->decimals=1; show(c,c->cur); strcat0(c->disp,".",32); } return; }
    if(ch=='C'){ memset(c,0,sizeof *c); strncpy0(c->disp,"0",32); return; }
    if(ch=='<'){ if(c->entering){ c->cur=0; c->decimals=0; show(c,0); } return; }
    if(!strcmp(k,"+/-")){ c->cur=-c->cur; show(c,c->cur); return; }
    if(ch=='%'){ c->cur = c->op ? mul(c->acc, c->cur/100) : c->cur/100; show(c,c->cur); c->entering=1; return; }
    /* operators and = */
    s64 v=c->cur;
    if(c->op){
        c->acc=apply(c,c->acc,v,c->op);
    } else c->acc=v;
    if(c->error){ strncpy0(c->disp,"Error",32); strncpy0(c->hist,"Can't divide by zero",48); return; }
    char a[32]; fmt(c->acc,a);
    if(ch=='='){ c->op=0; c->cur=c->acc; strncpy0(c->hist,"",48); }
    else { c->op=ch; strncpy0(c->hist,a,48); char o[4]={' ',ch,0,0}; strcat0(c->hist,o,48); c->cur=c->acc; }
    show(c,c->acc); c->entering=0;
}
static void c_draw(window_t *w){
    calc_t *c=w->st; int cw=win_cw(w), ch=win_ch(w);
    gradient_v(0,0,cw,ch,RGB(40,36,56),RGB(24,20,36));
    fill_round_rect(10,10,cw-20,76,8,RGB(210,225,200));
    draw_text(22,18,c->hist,RGB(70,90,70));
    int s=3; while(s>1 && text_width(c->disp)*s>cw-50) s--;
    draw_text_big(cw-26-text_width(c->disp)*s,84-16*s-6,c->disp,RGB(20,40,20),s);
    for(int r=0;r<5;r++) for(int k=0;k<4;k++){
        int x=10+k*(KW+6), y=TOP+r*(KH+6); const char *l=keys[r][k];
        int op=(k==3), fn=(r==0);
        u32 col= op?accent_light():fn?RGB(90,86,110):RGB(66,62,84);
        if(r==4&&k==3) col=RGB(255,170,50);
        fill_round_rect(x,y,KW,KH,8,col);
        draw_text_big(x+KW/2-text_width(l),y+KH/2-16,l,RGB(255,255,255),2);
    }
}
static void c_mouse(window_t *w,int ev,int x,int y,int b){
    calc_t *c=w->st; (void)b;
    if(ev!=EV_DOWN) return;
    for(int r=0;r<5;r++) for(int k=0;k<4;k++) if(hit(x,y,10+k*(KW+6),TOP+r*(KH+6),KW,KH)){ press(c,keys[r][k]); return; }
}
static void c_key(window_t *w,int k){
    calc_t *c=w->st; char s[2]={(char)k,0};
    if(k==KEY_CTRL('c')){ clip_set(c->disp,-1); wm_toast("Copied"); return; }
    if(k==KEY_CTRL('v')){ press(c,"C"); for(const char *p=clip_get();*p;p++){ char t[2]={*p,0}; if((*p>='0'&&*p<='9')||*p=='.') press(c,t); else if(*p=='-' && p==clip_get()) continue; else if(*p!=',' && *p!=' ') break; } return; }
    if(k>='0'&&k<='9') press(c,s);
    else if(k=='+'||k=='-'||k=='/'||k=='.'||k=='%') press(c,s);
    else if(k=='*'||k=='x') press(c,"x");
    else if(k=='\n'||k=='=') press(c,"=");
    else if(k=='\b') press(c,"<");
    else if(k==KEY_ESC||k=='c'||k=='C') press(c,"C");
}
void app_calc(void){
    window_t *w=wm_open("Calculator",4*KW+3*6+20,TOP+5*(KH+6)+6,IC_CALC); if(!w) return;
    calc_t *c=kzalloc(sizeof *c); strncpy0(c->disp,"0",32);
    w->st=c; w->draw=c_draw; w->mouse=c_mouse; w->key=c_key;
}
