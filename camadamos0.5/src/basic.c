/* CamadamOS BASIC - a small line-numbered BASIC interpreter.
   This file is compiled WITH the x87 FPU enabled (see Makefile), so it may use double. */
#include "kernel.h"

extern const u8 font_alpha[95][16][8];

/* ------------------------------------------------------------------ setjmp */
typedef struct { u32 ebx, esi, edi, ebp, esp, eip; } jb_t;
int b_setjmp(jb_t *j) __attribute__((returns_twice));
void b_longjmp(jb_t *j, int v) __attribute__((noreturn));
__asm__(
".text\n.globl b_setjmp\nb_setjmp:\n"
"  movl 4(%esp),%eax\n  movl %ebx,0(%eax)\n  movl %esi,4(%eax)\n  movl %edi,8(%eax)\n  movl %ebp,12(%eax)\n"
"  leal 4(%esp),%ecx\n  movl %ecx,16(%eax)\n  movl (%esp),%ecx\n  movl %ecx,20(%eax)\n  xorl %eax,%eax\n  ret\n"
".globl b_longjmp\nb_longjmp:\n  movl 4(%esp),%edx\n  movl 8(%esp),%eax\n"
"  movl 0(%edx),%ebx\n  movl 4(%edx),%esi\n  movl 8(%edx),%edi\n  movl 12(%edx),%ebp\n  movl 16(%edx),%esp\n  jmp *20(%edx)\n");

/* ------------------------------------------------------------------ x87 math */
static double f_sqrt(double x){ __asm__("fsqrt":"+t"(x)); return x; }
static double f_sin(double x){ __asm__("fsin":"+t"(x)); return x; }
static double f_cos(double x){ __asm__("fcos":"+t"(x)); return x; }
static double f_atan(double x){ double r; __asm__("fld1\n fpatan":"=t"(r):"0"(x)); return r; }
static double f_floor(double x){
    u16 cw, ncw; double r;
    __asm__ volatile("fnstcw %0":"=m"(cw)); ncw=(cw&~0x0C00)|0x0400;
    __asm__ volatile("fldcw %0"::"m"(ncw)); __asm__("frndint":"=t"(r):"0"(x)); __asm__ volatile("fldcw %0"::"m"(cw));
    return r;
}
static double f_log(double x){ double r; __asm__("fldln2\n fxch\n fyl2x":"=t"(r):"0"(x)); return r; }
static double f_exp(double x){
    /* e^x = 2^(x*log2 e) */
    double t=x*1.4426950408889634, i=f_floor(t), f=t-i, r;
    __asm__("f2xm1":"=t"(r):"0"(f)); r+=1.0;
    __asm__("fscale":"=t"(r):"0"(r),"u"(i));
    return r;
}
static double f_pow(double a,double b){
    if(b==f_floor(b) && b>=-64 && b<=64){ double r=1; int n=(int)b, neg=n<0; if(neg) n=-n; while(n--) r*=a; return neg?1/r:r; }
    if(a<=0) return 0;
    return f_exp(b*f_log(a));
}

/* ------------------------------------------------------------------ screen */
#define SW 640
#define SH 400
#define COLS 80
#define ROWS 25
#define STATUS_H 22
static const u32 qpal[16]={0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
                            0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF};

/* ------------------------------------------------------------------ state */
#define MAXL 2000
#define NVARS 200
#define STRMAX 255
enum { V_NUM, V_STR };
typedef struct { char name[16]; int str; int arr; double n; char *s; int d1, d2; double *na; char **sa; } var_t;
typedef struct { int str; double n; char *s; } val_t;
typedef struct { double *v; double lim, step; int line; const char *p; } forf_t;
typedef struct { int line; const char *p; } ret_t;
enum { ST_READY, ST_RUN, ST_INPUT, ST_SLEEP, ST_WAITKEY, ST_PLAY };

typedef struct {
    u32 *g;                 /* 640x400 canvas */
    int row, col, fg, bg;
    int lnum[MAXL]; char *ltxt[MAXL]; int nl;
    var_t vars[NVARS]; int nv;
    forf_t fors[32]; int nf;
    ret_t gos[64]; int ng;
    ret_t whiles[32]; int nw;
    int cl; const char *p;  /* current line index (-1 = immediate) and position */
    char imm[300];
    int state; u32 wake;
    char in[300]; int inlen, in_row, in_col, in_drawn;
    char last_cmd[300];
    const char *inp_p; int inp_line;   /* INPUT var list */
    int keyq[32], kh, kt;
    int brk, jumped;
    int dline; const char *dp;         /* DATA pointer */
    u32 seed;
    char fname[40];
    u32 play_f[256], play_ms[256]; int play_n, play_i;
    int cursor_on;
    window_t *win;
} basic_t;

static basic_t *B;
static jb_t errjmp;
static char errmsg[80];

/* ------------------------------------------------------------------ output */
static void g_pset(int x,int y,u32 c){ if(x>=0&&y>=0&&x<SW&&y<SH) B->g[y*SW+x]=c; }
static void draw_cell(int r,int c,char ch){
    u32 fg=qpal[B->fg&15], bg=qpal[B->bg&15];
    u32 *base=B->g+r*16*SW+c*8;
    if(ch<32||ch>126) ch='?';
    const u8 (*gl)[8]=font_alpha[ch-32];
    for(int j=0;j<16;j++) for(int i=0;i<8;i++){ int a=gl[j][i]; base[j*SW+i]= a ? mix(bg,fg,a) : bg; }
}
static int scrolled;
static void scroll_up(void){
    memmove(B->g,B->g+16*SW,(SH-16)*SW*4);
    u32 bg=qpal[B->bg&15]; for(int i=(SH-16)*SW;i<SH*SW;i++) B->g[i]=bg;
    scrolled++;
}
static void newline(void){ B->col=0; if(++B->row>=ROWS){ B->row=ROWS-1; scroll_up(); } }
static void outc(char ch){
    if(ch=='\n'){ newline(); return; }
    if(B->col>=COLS) newline();
    draw_cell(B->row,B->col,ch); B->col++;
}
static void outs(const char *s){ while(*s) outc(*s++); wm_invalidate(); }
static void cls(void){ u32 bg=qpal[B->bg&15]; for(int i=0;i<SW*SH;i++) B->g[i]=bg; B->row=B->col=0; wm_invalidate(); }

/* ------------------------------------------------------------------ numbers */
static void fmt_num(double x,char *o){
    char b[40]; int k=0;
    if(x!=x){ strncpy0(o,"NaN",8); return; }
    if(x<0){ b[k++]='-'; x=-x; }
    if(x>=1e9 || (x<1e-5 && x>0)){
        int e=0; while(x>=10){ x/=10; e++; } while(x<1){ x*=10; e--; }
        double r=x+5e-7; if(r>=10){ r/=10; e++; }
        int ip=(int)r; b[k++]='0'+ip; double fr=r-ip; b[k++]='.';
        for(int i=0;i<6;i++){ fr*=10; int d=(int)fr; b[k++]='0'+d; fr-=d; }
        while(b[k-1]=='0') k--; if(b[k-1]=='.') k--;
        b[k++]='E'; b[k++]= e<0?'-':'+'; if(e<0) e=-e; b[k++]='0'+e/10; b[k++]='0'+e%10; b[k]=0;
        strncpy0(o,b,40); return;
    }
    /* up to 7 significant digits */
    double ip=f_floor(x), fr=x-ip;
    int digits=0; double t=ip; while(t>=1){ t/=10; digits++; }
    int fd = 7-digits; if(fd<0) fd=0;
    double sc=1; for(int i=0;i<fd;i++) sc*=10;
    double rr=f_floor(fr*sc+0.5);
    if(rr>=sc){ ip+=1; rr-=sc; }
    u32 iv=(u32)(int)ip; char ib[16]; int n=0;
    do { ib[n++]='0'+iv%10; iv/=10; } while(iv);
    while(n) b[k++]=ib[--n];
    if(fd && rr>0){
        char fb[12]; u32 fv=(u32)(int)rr;
        for(int i=fd-1;i>=0;i--){ fb[i]='0'+fv%10; fv/=10; }
        int e=fd; while(e>0 && fb[e-1]=='0') e--;
        if(e){ b[k++]='.'; for(int i=0;i<e;i++) b[k++]=fb[i]; }
    }
    b[k]=0; strncpy0(o,b,40);
}
static double parse_num(const char **pp){
    const char *p=*pp; double v=0; int any=0;
    while(*p>='0'&&*p<='9'){ v=v*10+(*p-'0'); p++; any=1; }
    if(*p=='.'){ p++; double f=0.1; while(*p>='0'&&*p<='9'){ v+=(*p-'0')*f; f/=10; p++; any=1; } }
    if(any && (*p=='e'||*p=='E') && ((p[1]>='0'&&p[1]<='9')||((p[1]=='-'||p[1]=='+')&&p[2]>='0'&&p[2]<='9'))){
        p++; int neg=0; if(*p=='-'){neg=1;p++;} else if(*p=='+') p++;
        int e=0; while(*p>='0'&&*p<='9'){ e=e*10+(*p-'0'); p++; }
        while(e--) v = neg ? v/10 : v*10;
    }
    *pp=p; return v;
}
static double str_val(const char *s){
    while(*s==' ') s++; int neg=0; if(*s=='-'){neg=1;s++;} else if(*s=='+') s++;
    double v=parse_num(&s); return neg?-v:v;
}

/* ------------------------------------------------------------------ errors */
static void err(const char *m) __attribute__((noreturn));
static void err(const char *m){ strncpy0(errmsg,m,sizeof errmsg); b_longjmp(&errjmp,1); }

/* ------------------------------------------------------------------ lexer helpers */
static int up(int c){ return (c>='a'&&c<='z')?c-32:c; }
static int isal(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
static int isdig(int c){ return c>='0'&&c<='9'; }
static void ws(void){ while(*B->p==' '||*B->p=='\t') B->p++; }
/* match keyword (case-insensitive, must not be followed by a letter/digit/$) */
static int kw(const char *k){
    ws(); const char *p=B->p; int i=0;
    while(k[i]){ if(up(p[i])!=k[i]) return 0; i++; }
    if(isal(k[i-1]) && (isal(p[i])||isdig(p[i])||p[i]=='$'||p[i]=='_')) return 0;
    B->p=p+i; return 1;
}
static int peekkw(const char *k){ const char *s=B->p; int r=kw(k); B->p=s; return r; }
static int ch(int c){ ws(); if(*B->p==c){ B->p++; return 1; } return 0; }
static void need(int c){ if(!ch(c)){ char m[24]="Expected  "; m[9]=c; err(m); } }
static int at_end(void){ ws(); return *B->p==0||*B->p==':'||*B->p=='\''||peekkw("ELSE"); }

/* temp string ring */
static char tmp[48][STRMAX+1]; static int tmpi;
static char *tstr(void){ tmpi=(tmpi+1)%48; tmp[tmpi][0]=0; return tmp[tmpi]; }

/* ------------------------------------------------------------------ variables */
static int read_name(char *o){
    ws(); int n=0;
    if(!isal(*B->p)) return 0;
    while((isal(*B->p)||isdig(*B->p)||*B->p=='_') ){ if(n<14) o[n++]=up(*B->p); B->p++; }
    if(*B->p=='$'){ o[n++]='$'; B->p++; }
    o[n]=0; return 1;
}
static var_t *find_var(const char *name,int arr,int create){
    for(int i=0;i<B->nv;i++) if(B->vars[i].arr==arr && !strcmp(B->vars[i].name,name)) return &B->vars[i];
    if(!create) return 0;
    if(B->nv>=NVARS) err("Too many variables");
    var_t *v=&B->vars[B->nv++]; memset(v,0,sizeof *v);
    strncpy0(v->name,name,16); v->arr=arr; v->str = name[strlen(name)-1]=='$';
    return v;
}
static void dim_arr(var_t *v,int d1,int d2){
    if(d1<0||d2<0||(d1+1)*(d2+1)>200000) err("Bad array size");
    v->d1=d1; v->d2=d2; int n=(d1+1)*(d2+1);
    if(v->str){ v->sa=kzalloc(n*sizeof(char*)); if(!v->sa) err("Out of memory"); }
    else { v->na=kzalloc(n*sizeof(double)); if(!v->na) err("Out of memory"); }
}
static void free_vars(void){
    for(int i=0;i<B->nv;i++){
        var_t *v=&B->vars[i];
        if(v->s) kfree(v->s);
        if(v->na) kfree(v->na);
        if(v->sa){ int n=(v->d1+1)*(v->d2+1); for(int k=0;k<n;k++) if(v->sa[k]) kfree(v->sa[k]); kfree(v->sa); }
    }
    B->nv=0;
}
static double expr_num(void);
/* lvalue: returns slot pointers */
typedef struct { int str; double *n; char **s; } lval_t;
static lval_t lvalue(void){
    char name[20]; lval_t L={0,0,0};
    if(!read_name(name)) err("Syntax error");
    ws();
    if(*B->p=='('){
        B->p++;
        int i1=(int)expr_num(), i2=0; if(ch(',')) i2=(int)expr_num(); need(')');
        var_t *v=find_var(name,1,1);
        if(!v->na && !v->sa) dim_arr(v,10,(i2?10:0));
        if(i1<0||i1>v->d1||i2<0||i2>v->d2) err("Subscript out of range");
        int idx=i1*(v->d2+1)+i2;
        L.str=v->str; if(v->str) L.s=&v->sa[idx]; else L.n=&v->na[idx];
    } else {
        var_t *v=find_var(name,0,1);
        L.str=v->str; if(v->str) L.s=&v->s; else L.n=&v->n;
    }
    return L;
}
static void set_str(char **slot,const char *s){
    if(*slot) kfree(*slot);
    int n=strlen(s); *slot=kmalloc(n+1); if(!*slot) err("Out of memory"); memcpy(*slot,s,n+1);
}
static void assign(lval_t L,val_t v){
    if(L.str!=v.str) err("Type mismatch");
    if(L.str) set_str(L.s,v.s); else *L.n=v.n;
}

/* ------------------------------------------------------------------ expressions */
static val_t expr(void);
static val_t num(double n){ val_t v={0,n,0}; return v; }
static val_t sv(char *s){ val_t v={1,0,s}; return v; }
static double expr_num(void){ val_t v=expr(); if(v.str) err("Type mismatch"); return v.n; }
static char *expr_str(void){ val_t v=expr(); if(!v.str) err("Type mismatch"); return v.s; }
static double rnd01(void){ B->seed=B->seed*1103515245u+12345u; return (double)(int)((B->seed>>8)&0xFFFFFF)/16777216.0; }

static int inkey(void){ if(B->kh==B->kt) return 0; int k=B->keyq[B->kt]; B->kt=(B->kt+1)%32; return k; }

static val_t func(const char *name){
    /* name already read (uppercase, may end with $); '(' may follow */
    #define F(n) (!strcmp(name,n))
    if(F("RND")){ if(ch('(')){ expr_num(); need(')'); } return num(rnd01()); }
    if(F("TIMER")) return num((double)(int)ticks/1000.0);
    if(F("INKEY$")){ char *s=tstr(); int k=inkey();
        if(k==KEY_UP) strncpy0(s,"UP",8); else if(k==KEY_DOWN) strncpy0(s,"DOWN",8);
        else if(k==KEY_LEFT) strncpy0(s,"LEFT",8); else if(k==KEY_RIGHT) strncpy0(s,"RIGHT",8);
        else if(k=='\n'){ s[0]=13; s[1]=0; } else if(k>0 && k<256){ s[0]=k; s[1]=0; }
        return sv(s); }
    if(F("PI")) return num(3.14159265358979);
    need('(');
    val_t r;
    if(F("ABS")){ double x=expr_num(); r=num(x<0?-x:x); }
    else if(F("INT")){ r=num(f_floor(expr_num())); }
    else if(F("FIX")){ double x=expr_num(); r=num(x<0?-f_floor(-x):f_floor(x)); }
    else if(F("SGN")){ double x=expr_num(); r=num(x>0?1:x<0?-1:0); }
    else if(F("SQR")){ double x=expr_num(); if(x<0) err("Illegal function call"); r=num(f_sqrt(x)); }
    else if(F("SIN")) r=num(f_sin(expr_num()));
    else if(F("COS")) r=num(f_cos(expr_num()));
    else if(F("TAN")){ double x=expr_num(); r=num(f_sin(x)/f_cos(x)); }
    else if(F("ATN")) r=num(f_atan(expr_num()));
    else if(F("LOG")){ double x=expr_num(); if(x<=0) err("Illegal function call"); r=num(f_log(x)); }
    else if(F("EXP")) r=num(f_exp(expr_num()));
    else if(F("LEN")) r=num((int)strlen(expr_str()));
    else if(F("ASC")){ char *s=expr_str(); r=num((u8)s[0]); }
    else if(F("VAL")) r=num(str_val(expr_str()));
    else if(F("CHR$")){ char *s=tstr(); s[0]=(char)(int)expr_num(); s[1]=0; r=sv(s); }
    else if(F("STR$")){ char *s=tstr(); double x=expr_num(); if(x>=0){ s[0]=' '; fmt_num(x,s+1); } else fmt_num(x,s); r=sv(s); }
    else if(F("LEFT$")){ char *a=expr_str(); need(','); int n=(int)expr_num(); char *s=tstr(); int l=strlen(a); if(n>l) n=l; if(n<0) n=0; memcpy(s,a,n); s[n]=0; r=sv(s); }
    else if(F("RIGHT$")){ char *a=expr_str(); need(','); int n=(int)expr_num(); char *s=tstr(); int l=strlen(a); if(n>l) n=l; if(n<0) n=0; memcpy(s,a+l-n,n); s[n]=0; r=sv(s); }
    else if(F("MID$")){ char *a=expr_str(); need(','); int st=(int)expr_num()-1, n=STRMAX; if(ch(',')) n=(int)expr_num();
        char *s=tstr(); int l=strlen(a); if(st<0) st=0; if(st>l) st=l; if(n>l-st) n=l-st; if(n<0) n=0; memcpy(s,a+st,n); s[n]=0; r=sv(s); }
    else if(F("UCASE$")||F("LCASE$")){ char *a=expr_str(); char *s=tstr(); int i=0; for(;a[i];i++){ int c=a[i]; s[i]= F("UCASE$") ? up(c) : ((c>='A'&&c<='Z')?c+32:c); } s[i]=0; r=sv(s); }
    else if(F("SPACE$")){ int n=(int)expr_num(); if(n>STRMAX) n=STRMAX; char *s=tstr(); for(int i=0;i<n;i++) s[i]=' '; s[n<0?0:n]=0; r=sv(s); }
    else if(F("STRING$")){ int n=(int)expr_num(); need(','); val_t c=expr(); int cc = c.str ? c.s[0] : (int)c.n; if(n>STRMAX) n=STRMAX; char *s=tstr(); for(int i=0;i<n;i++) s[i]=cc; s[n<0?0:n]=0; r=sv(s); }
    else if(F("INSTR")){ char *a=expr_str(); need(','); char *b=expr_str(); int la=strlen(a), lb=strlen(b), pos=0;
        for(int i=0;i+lb<=la;i++) if(!memcmp(a+i,b,lb)){ pos=i+1; break; } r=num(pos); }
    else if(F("POINT")){ int x=(int)expr_num(); need(','); int y=(int)expr_num(); int c=-1;
        if(x>=0&&y>=0&&x<SW&&y<SH){ u32 px=B->g[y*SW+x]&0xFFFFFF; for(int i=0;i<16;i++) if(qpal[i]==px) c=i; } r=num(c); }
    else { char m[40]="Unknown function "; strcat0(m,name,40); err(m); }
    need(')');
    return r;
    #undef F
}
static int is_func(const char *n){
    static const char *const fn[]={"RND","TIMER","INKEY$","PI","ABS","INT","FIX","SGN","SQR","SIN","COS","TAN","ATN","LOG","EXP","LEN","ASC","VAL",
        "CHR$","STR$","LEFT$","RIGHT$","MID$","UCASE$","LCASE$","SPACE$","STRING$","INSTR","POINT",0};
    for(int i=0;fn[i];i++) if(!strcmp(fn[i],n)) return 1;
    return 0;
}
static val_t prim(void){
    ws(); char c=*B->p;
    if(c=='('){ B->p++; val_t v=expr(); need(')'); return v; }
    if(c=='"'){
        B->p++; char *s=tstr(); int n=0;
        while(*B->p && *B->p!='"'){ if(n<STRMAX) s[n++]=*B->p; B->p++; }
        s[n]=0; if(*B->p=='"') B->p++;
        return sv(s);
    }
    if(isdig(c)||c=='.'){ return num(parse_num(&B->p)); }
    if(c=='&' && (B->p[1]=='H'||B->p[1]=='h')){ B->p+=2; double v=0; for(;;){ int d=up(*B->p); if(isdig(d)) v=v*16+d-'0'; else if(d>='A'&&d<='F') v=v*16+d-'A'+10; else break; B->p++; } return num(v); }
    if(isal(c)){
        const char *save=B->p; char name[20]; read_name(name);
        if(is_func(name)) return func(name);
        B->p=save; lval_t L=lvalue();
        if(L.str){ char *s=tstr(); if(*L.s) strncpy0(s,*L.s,STRMAX+1); return sv(s); }
        return num(*L.n);
    }
    err("Syntax error");
}
static val_t power(void){
    val_t a=prim();
    while(ch('^')){ val_t b=prim(); if(a.str||b.str) err("Type mismatch"); a.n=f_pow(a.n,b.n); }
    return a;
}
static val_t unary(void){
    if(ch('-')){ val_t v=unary(); if(v.str) err("Type mismatch"); v.n=-v.n; return v; }
    if(ch('+')) return unary();
    return power();
}
static val_t term(void){
    val_t a=unary();
    for(;;){
        if(ch('*')){ double b=unary().n; a.n*=b; }
        else if(ch('/')){ double b=unary().n; if(b==0) err("Division by zero"); a.n/=b; }
        else if(ch('\\')){ double b=unary().n; int ib=(int)b; if(!ib) err("Division by zero"); a.n=(int)a.n/ib; }
        else if(kw("MOD")){ double b=unary().n; int ib=(int)b; if(!ib) err("Division by zero"); a.n=(int)a.n%ib; }
        else return a;
    }
}
static val_t sum(void){
    val_t a=term();
    for(;;){
        if(ch('+')){
            val_t b=term();
            if(a.str!=b.str) err("Type mismatch");
            if(a.str){ char *s=tstr(); strncpy0(s,a.s,STRMAX+1); strcat0(s,b.s,STRMAX+1); a.s=s; }
            else a.n+=b.n;
        } else if(ch('-')){ val_t b=term(); if(a.str||b.str) err("Type mismatch"); a.n-=b.n; }
        else return a;
    }
}
static val_t cmp(void){
    val_t a=sum();
    for(;;){
        ws(); const char *p=B->p; int op=0;
        if(p[0]=='<'&&p[1]=='>'){op=1;B->p+=2;} else if(p[0]=='<'&&p[1]=='='){op=2;B->p+=2;} else if(p[0]=='>'&&p[1]=='='){op=3;B->p+=2;}
        else if(p[0]=='<'){op=4;B->p++;} else if(p[0]=='>'){op=5;B->p++;} else if(p[0]=='='){op=6;B->p++;}
        if(!op) return a;
        val_t b=sum(); if(a.str!=b.str) err("Type mismatch");
        int c = a.str ? strcmp(a.s,b.s) : (a.n<b.n?-1:a.n>b.n?1:0);
        int r = op==1?c!=0: op==2?c<=0: op==3?c>=0: op==4?c<0: op==5?c>0: c==0;
        a=num(r?-1:0);
    }
}
static val_t notx(void){ if(kw("NOT")){ val_t v=notx(); v.n=~(int)v.n; return v; } return cmp(); }
static val_t andx(void){ val_t a=notx(); while(kw("AND")){ val_t b=notx(); a.n=(int)a.n & (int)b.n; } return a; }
static val_t expr(void){
    val_t a=andx();
    for(;;){
        if(kw("OR")){ val_t b=andx(); a.n=(int)a.n | (int)b.n; }
        else if(kw("XOR")){ val_t b=andx(); a.n=(int)a.n ^ (int)b.n; }
        else return a;
    }
}

/* ------------------------------------------------------------------ program lines */
static int find_line(int n){ int lo=0, hi=B->nl-1; while(lo<=hi){ int m=(lo+hi)/2; if(B->lnum[m]==n) return m; if(B->lnum[m]<n) lo=m+1; else hi=m-1; } return -1; }
static void del_line(int n){ int i=find_line(n); if(i<0) return; kfree(B->ltxt[i]); for(int k=i;k<B->nl-1;k++){ B->lnum[k]=B->lnum[k+1]; B->ltxt[k]=B->ltxt[k+1]; } B->nl--; }
static void put_line(int n,const char *t){
    del_line(n);
    while(*t==' ') t++;
    if(!*t) return;
    if(B->nl>=MAXL) return;
    int i=0; while(i<B->nl && B->lnum[i]<n) i++;
    for(int k=B->nl;k>i;k--){ B->lnum[k]=B->lnum[k-1]; B->ltxt[k]=B->ltxt[k-1]; }
    int l=strlen(t); B->ltxt[i]=kmalloc(l+1); memcpy(B->ltxt[i],t,l+1); B->lnum[i]=n; B->nl++;
}
static void new_prog(void){ for(int i=0;i<B->nl;i++) kfree(B->ltxt[i]); B->nl=0; }

static void goto_line(int n){
    int i=find_line(n);
    if(i<0){ char m[40]="Undefined line "; char x[12]; itoa10(n,x); strcat0(m,x,40); err(m); }
    B->cl=i; B->p=B->ltxt[i]; B->jumped=1;
}
static int cur_line_num(void){ return B->cl>=0 && B->cl<B->nl ? B->lnum[B->cl] : -1; }

/* skip to end of current statement (respecting strings) */
static void skip_stmt(void){ while(*B->p && *B->p!=':'){ if(*B->p=='"'){ B->p++; while(*B->p && *B->p!='"') B->p++; if(*B->p) B->p++; } else B->p++; } }
static void skip_line(void){ while(*B->p) B->p++; }
/* advance to the start of the next statement; returns 0 if program ended */
static int next_stmt(void){
    ws();
    if(*B->p==':'){ B->p++; return 1; }
    if(*B->p && *B->p!='\'' && !peekkw("ELSE")) err("Syntax error");
    if(B->cl<0) return 0;
    B->cl++; if(B->cl>=B->nl) return 0;
    B->p=B->ltxt[B->cl]; return 1;
}
/* scan forward for matching block end (NEXT for FOR, WEND for WHILE) */
static void scan_block(const char *open,const char *close){
    int depth=0;
    for(;;){
        skip_stmt();
        if(!next_stmt()) err(close[0]=='N'?"FOR without NEXT":"WHILE without WEND");
        ws();
        if(peekkw(open)) depth++;
        else if(peekkw(close)){ if(depth==0){ kw(close); if(close[0]=='N'){ /* skip optional var */ char nm[20]; const char *s=B->p; if(!read_name(nm)) B->p=s; } return; } depth--; }
    }
}

/* ------------------------------------------------------------------ graphics */
static u32 colarg(int c){ return qpal[c&15]; }
static void g_line(int x0,int y0,int x1,int y1,u32 c){
    int dx=x1-x0, dy=y1-y0, sx=dx<0?-1:1, sy=dy<0?-1:1; if(dx<0) dx=-dx; if(dy<0) dy=-dy;
    int e=dx-dy;
    for(int guard=0;guard<4000;guard++){ g_pset(x0,y0,c); if(x0==x1&&y0==y1) break; int e2=2*e; if(e2>-dy){ e-=dy; x0+=sx; } if(e2<dx){ e+=dx; y0+=sy; } }
}
static void g_circle(int cx,int cy,int r,u32 c,int fill){
    int x=r, y=0, e=1-r;
    while(x>=y){
        if(fill){ g_line(cx-x,cy+y,cx+x,cy+y,c); g_line(cx-x,cy-y,cx+x,cy-y,c); g_line(cx-y,cy+x,cx+y,cy+x,c); g_line(cx-y,cy-x,cx+y,cy-x,c); }
        else { g_pset(cx+x,cy+y,c); g_pset(cx-x,cy+y,c); g_pset(cx+x,cy-y,c); g_pset(cx-x,cy-y,c);
               g_pset(cx+y,cy+x,c); g_pset(cx-y,cy+x,c); g_pset(cx+y,cy-x,c); g_pset(cx-y,cy-x,c); }
        y++; if(e<0) e+=2*y+1; else { x--; e+=2*(y-x)+1; }
    }
}
static void coord(int *x,int *y){
    int par=ch('(');
    *x=(int)expr_num(); need(','); *y=(int)expr_num();
    if(par) need(')');
}

/* ------------------------------------------------------------------ PLAY (music macro language) */
static void play_parse(const char *s){
    static const int semi[7]={9,11,0,2,4,5,7};  /* A B C D E F G */
    int oct=4, len=4, tempo=120; B->play_n=0; B->play_i=0;
    while(*s && B->play_n<256){
        int c=up(*s++);
        if(c==' ') continue;
        if(c=='O'){ int v=0; while(isdig(*s)) v=v*10+(*s++-'0'); oct=clampi(v,0,7); continue; }
        if(c=='<'){ if(oct>0) oct--; continue; } if(c=='>'){ if(oct<7) oct++; continue; }
        if(c=='L'){ int v=0; while(isdig(*s)) v=v*10+(*s++-'0'); if(v>=1&&v<=64) len=v; continue; }
        if(c=='T'){ int v=0; while(isdig(*s)) v=v*10+(*s++-'0'); if(v>=32&&v<=255) tempo=v; continue; }
        if(c=='M'){ if(*s) s++; continue; }
        int note=-1;
        if(c>='A'&&c<='G'){ note=semi[c-'A']; if(*s=='#'||*s=='+'){ note++; s++; } else if(*s=='-'){ note--; s++; } }
        else if(c!='P') continue;
        int l=len; if(isdig(*s)){ int v=0; while(isdig(*s)) v=v*10+(*s++-'0'); if(v>=1&&v<=64) l=v; }
        int ms = 240000/(tempo*l);
        while(*s=='.'){ ms = ms*3/2; s++; }
        double f=0;
        if(note>=-1 && c!='P'){
            int midi = (oct+1)*12 + note;       /* C4 = 60 */
            f = 440.0 * f_pow(2.0,(midi-69)/12.0);
        }
        B->play_f[B->play_n]=(u32)(int)f; B->play_ms[B->play_n]=ms; B->play_n++;
    }
}

/* ------------------------------------------------------------------ statements */
static void do_print(void){
    int nl=1;
    for(;;){
        if(at_end()) break;
        nl=1;
        if(ch(';')){ nl=0; continue; }
        if(ch(',')){ nl=0; int t=(B->col/14+1)*14; if(t>=COLS) newline(); else while(B->col<t) outc(' '); continue; }
        if(kw("TAB(")){ int t=(int)expr_num()-1; need(')'); if(t<B->col) newline(); while(B->col<t && B->col<COLS-1) outc(' '); nl=0; continue; }
        if(kw("SPC(")){ int t=(int)expr_num(); need(')'); while(t-->0) outc(' '); nl=0; continue; }
        val_t v=expr();
        if(v.str) outs(v.s);
        else { char b[44]; fmt_num(v.n,b); if(v.n>=0) outc(' '); outs(b); outc(' '); }
    }
    if(nl) newline();
    wm_invalidate();
}
static void begin_input(void);
static int data_next(char *out){
    /* find next DATA item */
    for(;;){
        if(B->dp){
            while(*B->dp==' ') B->dp++;
            if(*B->dp && *B->dp!=':'){
                int n=0;
                if(*B->dp=='"'){ B->dp++; while(*B->dp && *B->dp!='"'){ if(n<STRMAX) out[n++]=*B->dp; B->dp++; } if(*B->dp) B->dp++; }
                else { while(*B->dp && *B->dp!=',' && *B->dp!=':'){ if(n<STRMAX) out[n++]=*B->dp; B->dp++; } while(n&&out[n-1]==' ') n--; }
                out[n]=0;
                while(*B->dp==' ') B->dp++;
                if(*B->dp==',') B->dp++;
                return 1;
            }
            B->dp=0; B->dline++;
        }
        /* scan lines from dline for DATA */
        for(;B->dline<B->nl;B->dline++){
            const char *t=B->ltxt[B->dline]; int q=0;
            for(const char *s=t;*s;s++){
                if(*s=='"') q^=1;
                if(!q && (s==t || s[-1]==':' || s[-1]==' ') && up(s[0])=='D'&&up(s[1])=='A'&&up(s[2])=='T'&&up(s[3])=='A' && !isal(s[4])){
                    /* make sure it's at statement start */
                    const char *b=s; while(b>t && b[-1]==' ') b--;
                    if(b==t || b[-1]==':'){ B->dp=s+4; break; }
                }
            }
            if(B->dp) break;
        }
        if(B->dline>=B->nl) return 0;
    }
}

static void exec_stmt(void){
    ws();
    if(!*B->p || *B->p==':') return;
    if(*B->p=='\'' ){ skip_line(); return; }
    if(*B->p=='?'){ B->p++; do_print(); return; }
    if(kw("REM")){ skip_line(); return; }
    if(kw("ELSE")){ skip_line(); return; }
    if(kw("PRINT")){ do_print(); return; }
    if(kw("CLS")){ cls(); return; }
    if(kw("LET")){ lval_t L=lvalue(); need('='); assign(L,expr()); return; }
    if(kw("GOTO")){ goto_line((int)expr_num()); return; }
    if(kw("GOSUB")){
        int n=(int)expr_num();
        if(B->ng>=64) err("Out of stack space");
        B->gos[B->ng].line=B->cl; B->gos[B->ng].p=B->p; B->ng++;
        goto_line(n); return;
    }
    if(kw("RETURN")){ if(!B->ng) err("RETURN without GOSUB"); B->ng--; B->cl=B->gos[B->ng].line; B->p=B->gos[B->ng].p; return; }
    if(kw("END")||kw("STOP")||kw("SYSTEM")){ B->cl=-1; B->p=""; B->brk=2; return; }
    if(kw("IF")){
        double c=expr_num();
        int then=kw("THEN"); int isgoto=0; if(!then){ isgoto=kw("GOTO"); if(!isgoto) err("Expected THEN"); }
        if(c!=0){
            ws(); if(isdig(*B->p)) goto_line((int)expr_num()); else B->jumped=1;
            return;   /* continue executing statements after THEN */
        }
        /* false: find ELSE on this line */
        int q=0, depth=0;
        while(*B->p){
            if(*B->p=='"') q^=1;
            if(!q){
                if(peekkw("IF")) depth++;
                if(peekkw("ELSE")){ if(depth==0){ kw("ELSE"); ws(); if(isdig(*B->p)) goto_line((int)expr_num()); else B->jumped=1; return; } depth--; }
            }
            B->p++;
        }
        return;
    }
    if(kw("FOR")){
        lval_t L=lvalue(); if(L.str) err("Type mismatch");
        need('='); double a=expr_num(); if(!kw("TO")) err("Expected TO");
        double lim=expr_num(), step=1; if(kw("STEP")) step=expr_num();
        *L.n=a;
        /* remove existing frame for same var */
        for(int i=B->nf-1;i>=0;i--) if(B->fors[i].v==L.n){ B->nf=i; break; }
        if((step>=0 && a>lim) || (step<0 && a<lim)){ scan_block("FOR","NEXT"); return; }
        if(B->nf>=32) err("Too many nested FORs");
        forf_t *f=&B->fors[B->nf++]; f->v=L.n; f->lim=lim; f->step=step; f->line=B->cl; f->p=B->p;
        return;
    }
    if(kw("NEXT")){
        if(!B->nf) err("NEXT without FOR");
        ws();
        if(isal(*B->p)){ lval_t L=lvalue(); while(B->nf && B->fors[B->nf-1].v!=L.n) B->nf--; if(!B->nf) err("NEXT without FOR"); }
        forf_t *f=&B->fors[B->nf-1];
        *f->v+=f->step;
        if((f->step>=0 && *f->v<=f->lim) || (f->step<0 && *f->v>=f->lim)){ B->cl=f->line; B->p=f->p; }
        else B->nf--;
        return;
    }
    if(kw("WHILE")){
        const char *start=B->p-5; int line=B->cl;
        double c=expr_num();
        int top = B->nw && B->whiles[B->nw-1].line==line && B->whiles[B->nw-1].p==start;
        if(c!=0){ if(!top){ if(B->nw>=32) err("Too many WHILEs"); B->whiles[B->nw].line=line; B->whiles[B->nw].p=start; B->nw++; } }
        else { if(top) B->nw--; scan_block("WHILE","WEND"); }
        return;
    }
    if(kw("WEND")){ if(!B->nw) err("WEND without WHILE"); B->cl=B->whiles[B->nw-1].line; B->p=B->whiles[B->nw-1].p; B->jumped=1; return; }
    if(kw("DIM")){
        do {
            char name[20]; if(!read_name(name)) err("Syntax error");
            need('('); int d1=(int)expr_num(), d2=0; if(ch(',')) d2=(int)expr_num(); need(')');
            var_t *v=find_var(name,1,1); if(v->na||v->sa) err("Array already dimensioned");
            dim_arr(v,d1,d2);
        } while(ch(','));
        return;
    }
    if(kw("INPUT")){
        ws();
        int q=1;
        if(*B->p=='"'){ char *s=expr_str(); outs(s); if(ch(',')) q=0; else need(';'); }
        if(q) outs("? ");
        B->inp_p=B->p; B->inp_line=B->cl;
        /* skip var list for now */
        do { lvalue(); } while(ch(','));
        begin_input(); B->state=ST_INPUT; return;
    }
    if(kw("COLOR")){ ws(); if(*B->p!=',') B->fg=(int)expr_num()&15; if(ch(',')) B->bg=(int)expr_num()&15; return; }
    if(kw("LOCATE")){ ws(); int r=B->row+1, c=B->col+1; if(*B->p!=',') r=(int)expr_num(); if(ch(',')) c=(int)expr_num();
        B->row=clampi(r-1,0,ROWS-1); B->col=clampi(c-1,0,COLS-1); return; }
    if(kw("PSET")){ int x,y; coord(&x,&y); int c=B->fg; if(ch(',')) c=(int)expr_num(); g_pset(x,y,colarg(c)); wm_invalidate(); return; }
    if(kw("LINE")){
        int x0,y0,x1,y1; coord(&x0,&y0); need('-'); coord(&x1,&y1);
        int c=B->fg, box=0;
        if(ch(',')){ ws(); if(*B->p!=',') c=(int)expr_num(); if(ch(',')){ if(kw("BF")) box=2; else if(kw("B")) box=1; } }
        u32 col=colarg(c);
        if(box==2){ int ax=imin(x0,x1), bx=imax(x0,x1); for(int y=imin(y0,y1);y<=imax(y0,y1);y++) g_line(ax,y,bx,y,col); }
        else if(box==1){ g_line(x0,y0,x1,y0,col); g_line(x1,y0,x1,y1,col); g_line(x1,y1,x0,y1,col); g_line(x0,y1,x0,y0,col); }
        else g_line(x0,y0,x1,y1,col);
        wm_invalidate(); return;
    }
    if(kw("CIRCLE")){ int x,y; coord(&x,&y); need(','); int r=(int)expr_num(); int c=B->fg, f=0; if(ch(',')){ c=(int)expr_num(); if(ch(',')) f=(int)expr_num(); }
        g_circle(x,y,r,colarg(c),f); wm_invalidate(); return; }
    if(kw("SOUND")){ double f=expr_num(); need(','); double d=expr_num(); int ms=(int)(d*55);
        if(f>=37) piano_note((u32)(int)f,(u32)ms); B->wake=ticks+ms; B->state=ST_SLEEP; return; }
    if(kw("BEEP")){ piano_note(880,150); B->wake=ticks+150; B->state=ST_SLEEP; return; }
    if(kw("PLAY")){ char *s=expr_str(); play_parse(s); if(B->play_n){ B->state=ST_PLAY; B->wake=ticks; } return; }
    if(kw("SLEEP")){ if(at_end()){ B->state=ST_WAITKEY; B->kh=B->kt=0; return; } double s=expr_num(); B->wake=ticks+(u32)(int)(s*1000); B->state=ST_SLEEP; return; }
    if(kw("RANDOMIZE")){ if(!at_end()) B->seed=(u32)(int)expr_num()*2654435761u+1; else B->seed=ticks*2654435761u; return; }
    if(kw("SWAP")){ lval_t a=lvalue(); need(','); lval_t b=lvalue(); if(a.str!=b.str) err("Type mismatch");
        if(a.str){ char *t=*a.s; *a.s=*b.s; *b.s=t; } else { double t=*a.n; *a.n=*b.n; *b.n=t; } return; }
    if(kw("DATA")){ skip_stmt(); return; }
    if(kw("READ")){
        do { lval_t L=lvalue(); char buf[STRMAX+1]; if(!data_next(buf)) err("Out of DATA");
             if(L.str) set_str(L.s,buf); else *L.n=str_val(buf); } while(ch(','));
        return;
    }
    if(kw("RESTORE")){ B->dline=0; B->dp=0; if(!at_end()){ int n=(int)expr_num(); int i=find_line(n); if(i>=0) B->dline=i; } return; }
    /* implicit LET */
    if(isal(*B->p)){ lval_t L=lvalue(); if(!ch('=')) err("Syntax error"); assign(L,expr()); return; }
    err("Syntax error");
}

/* ------------------------------------------------------------------ console input */
static void render_input(void){
    int r=B->in_row, c=B->in_col;
    /* clear previously drawn area */
    int tot=B->in_drawn+1; int rr=r, cc=c;
    for(int i=0;i<tot;i++){ if(cc>=COLS){ cc=0; rr++; } if(rr<ROWS) draw_cell(rr,cc,' '); cc++; }
    B->row=r; B->col=c; scrolled=0;
    for(int i=0;i<B->inlen;i++) outc(B->in[i]);
    if(B->col>=COLS) newline();
    B->in_row-=scrolled; if(B->in_row<0) B->in_row=0;
    B->in_drawn=B->inlen; wm_invalidate();
}
static void begin_input(void){ B->inlen=0; B->in[0]=0; B->in_row=B->row; B->in_col=B->col; B->in_drawn=0; }

static void ready(void){
    B->state=ST_READY; B->cl=-1;
    if(B->col) newline();
    outs("Ready\n"); begin_input();
}

/* run statements until time slice ends */
static void run_slice(void){
    u32 t0=ticks; volatile int count=0;
    if(b_setjmp(&errjmp)){
        __asm__ volatile("fninit");
        if(B->col) newline();
        int ln=cur_line_num();
        outs(errmsg); if(ln>=0){ char x[16]; outs(" in line "); itoa10(ln,x); outs(x); }
        outs("\n"); piano_note(220,150);
        ready(); return;
    }
    while(B->state==ST_RUN){
        if(B->brk==1){ B->brk=0; if(B->col) newline(); outs("Break"); int ln=cur_line_num(); if(ln>=0){ char x[16]; outs(" in line "); itoa10(ln,x); outs(x); } outs("\n"); ready(); return; }
        exec_stmt();
        if(B->brk==2){ B->brk=0; ready(); return; }
        if(B->state!=ST_RUN) return;
        if(B->jumped) B->jumped=0;
        else if(!next_stmt()){ ready(); return; }
        if((++count&63)==0 && ticks-t0>12) return;
    }
}
static void after_wait(void){
    /* resume after SLEEP/SOUND/PLAY/INPUT: move past the statement */
    if(b_setjmp(&errjmp)){ __asm__ volatile("fninit"); outs(errmsg); outs("\n"); ready(); return; }
    B->state=ST_RUN; B->jumped=0;
    if(!next_stmt()) ready();
}
static void finish_input(void){
    newline();
    if(b_setjmp(&errjmp)){ __asm__ volatile("fninit"); outs(errmsg); outs("\n"); ready(); return; }
    const char *s=B->in;
    B->p=B->inp_p; B->cl=B->inp_line;
    do {
        lval_t L=lvalue(); char item[STRMAX+1]; int n=0;
        while(*s==' ') s++;
        if(*s=='"'){ s++; while(*s && *s!='"'){ if(n<STRMAX) item[n++]=*s; s++; } if(*s) s++; while(*s && *s!=',') s++; }
        else { while(*s && *s!=','){ if(n<STRMAX) item[n++]=*s; s++; } if(!L.str) while(n && item[n-1]==' ') n--; }
        item[n]=0; if(*s==',') s++;
        if(L.str) set_str(L.s,item); else *L.n=str_val(item);
    } while(ch(','));
    B->state=ST_RUN;
    if(!next_stmt()){ ready(); return; }
    run_slice();
}

/* ------------------------------------------------------------------ files */
static int prog_dir(void){ int d=fs_lookup("/Programs"); if(d<0) d=fs_create(0,"Programs",FT_DIR); return d; }
static int resolve_file(const char *name,int create){
    char n[40]; strncpy0(n,name,sizeof n);
    if(n[0]=='/'){ int f=fs_lookup(n); if(f>=0 || !create) return f; }
    if(!ends_with(n,".bas") && !ends_with(n,".BAS")) strcat0(n,".bas",sizeof n);
    int d=prog_dir(); int f=fs_find(d,n);
    if(f<0 && create) f=fs_create(d,n,FT_TEXT);
    return f;
}
static void load_text(const char *t,u32 len){
    new_prog(); int auto_n=10; char line[300];
    u32 i=0;
    while(i<len){
        int n=0; while(i<len && t[i]!='\n'){ if(t[i]!='\r' && n<299) line[n++]=t[i]; i++; } i++;
        line[n]=0;
        const char *s=line; while(*s==' ') s++;
        if(!*s) continue;
        if(isdig(*s)){ int num=0; while(isdig(*s)) num=num*10+(*s++-'0'); put_line(num,s); auto_n=num+10; }
        else { put_line(auto_n,s); auto_n+=10; }
    }
}
static int load_file(int f){
    if(f<0 || fs_nodes[f].type!=FT_TEXT){ return 0; }
    load_text(fs_nodes[f].data?fs_nodes[f].data:"",fs_nodes[f].size);
    strncpy0(B->fname,fs_nodes[f].name,sizeof B->fname);
    return 1;
}
static void save_file(int f){
    u32 tot=0; for(int i=0;i<B->nl;i++) tot+=strlen(B->ltxt[i])+12;
    char *d=kmalloc(tot+1); u32 k=0;
    for(int i=0;i<B->nl;i++){ char x[12]; itoa10(B->lnum[i],x); for(char *q=x;*q;q++) d[k++]=*q; d[k++]=' '; for(char *q=B->ltxt[i];*q;q++) d[k++]=*q; d[k++]='\n'; }
    d[k]=0;
    if(fs_nodes[f].data && !fs_nodes[f].readonly) kfree(fs_nodes[f].data);
    fs_nodes[f].data=d; fs_nodes[f].size=k; fs_nodes[f].readonly=0; fs_touch();
    strncpy0(B->fname,fs_nodes[f].name,sizeof B->fname);
}

/* ------------------------------------------------------------------ immediate commands */
static void list(int a,int b){
    for(int i=0;i<B->nl;i++){
        if(B->lnum[i]<a || B->lnum[i]>b) continue;
        char x[12]; itoa10(B->lnum[i],x); outs(x); outc(' '); outs(B->ltxt[i]); newline();
    }
}
static void run_prog(int from){
    free_vars(); B->nf=B->ng=B->nw=0; B->dline=0; B->dp=0; B->kh=B->kt=0; B->brk=0;
    if(!B->nl){ ready(); return; }
    if(from>=0){ goto_line(from); } else { B->cl=0; B->p=B->ltxt[0]; }
    B->state=ST_RUN; run_slice();
}
static const char *help_txt[]={
"CamadamOS BASIC - commands:",
" RUN  LIST [a-b]  NEW  EDIT n  SAVE \"name\"  LOAD \"name\"  FILES  CLS",
"Type a line with a number to add it to the program, e.g.",
"  10 PRINT \"HELLO\"",
"Keys: INKEY$ gives \"UP\" \"DOWN\" \"LEFT\" \"RIGHT\" for arrows, CHR$(13) for Enter.",
"Statements: PRINT INPUT LET IF/THEN/ELSE GOTO GOSUB RETURN FOR/NEXT",
"  WHILE/WEND DIM READ/DATA/RESTORE END CLS COLOR LOCATE PSET LINE CIRCLE",
"  SOUND BEEP PLAY SLEEP RANDOMIZE SWAP REM",
"Functions: RND INT ABS SQR SIN COS TAN ATN LOG EXP SGN LEN LEFT$ RIGHT$",
"  MID$ STR$ VAL CHR$ ASC UCASE$ LCASE$ INSTR INKEY$ TIMER POINT",
"Esc or Ctrl+C stops a running program.  Ctrl+V pastes a program.",0};

static void immediate(const char *line){
    const char *s=line; while(*s==' ') s++;
    if(!*s){ begin_input(); return; }
    if(isdig(*s)){
        int n=0; while(isdig(*s)) n=n*10+(*s++-'0');
        if(*s==' ') s++;
        put_line(n,s); begin_input(); return;
    }
    strncpy0(B->imm,s,sizeof B->imm);
    B->p=B->imm; B->cl=-1;
    if(b_setjmp(&errjmp)){ __asm__ volatile("fninit"); if(B->col) newline(); outs(errmsg); outs("\n"); piano_note(220,150); ready(); return; }
    if(kw("RUN")){ ws(); int from=-1; if(isdig(*B->p)) from=(int)expr_num(); else if(*B->p=='"'){ char *f=expr_str(); if(!load_file(resolve_file(f,0))) err("File not found"); } run_prog(from); return; }
    if(kw("LIST")){ int a=0, b=0x7FFFFFFF; ws(); if(isdig(*B->p)){ a=b=(int)parse_num(&B->p); } if(ch('-')){ ws(); b = isdig(*B->p) ? (int)parse_num(&B->p) : 0x7FFFFFFF; } list(a,b); ready(); return; }
    if(kw("NEW")){ new_prog(); free_vars(); B->fname[0]=0; ready(); return; }
    if(kw("HELP")){ for(int i=0;help_txt[i];i++){ outs(help_txt[i]); newline(); } ready(); return; }
    if(kw("FILES")||kw("DIR")){
        int d=prog_dir(); int kids[64]; int n=fs_children(d,kids,64);
        if(!n) outs("No programs saved yet.\n");
        for(int i=0;i<n;i++){ outs("  "); outs(fs_nodes[kids[i]].name); newline(); }
        ready(); return;
    }
    if(kw("SAVE")){
        ws(); char nm[40];
        if(*B->p=='"') strncpy0(nm,expr_str(),sizeof nm); else if(B->fname[0]) strncpy0(nm,B->fname,sizeof nm); else err("Usage: SAVE \"name\"");
        int f=resolve_file(nm,1); if(f<0) err("Can't save");
        save_file(f); outs("Saved /Programs/"); outs(fs_nodes[f].name); newline(); ready(); return;
    }
    if(kw("LOAD")){
        char *nm=expr_str(); int f=resolve_file(nm,0);
        if(!load_file(f)) err("File not found");
        outs("Loaded "); outs(fs_nodes[f].name); newline(); ready(); return;
    }
    if(kw("EDIT")){
        int n=(int)expr_num(); int i=find_line(n); if(i<0) err("Undefined line");
        if(B->col) newline();
        begin_input();
        char x[12]; itoa10(n,x); strncpy0(B->in,x,sizeof B->in); strcat0(B->in," ",sizeof B->in); strcat0(B->in,B->ltxt[i],sizeof B->in);
        B->inlen=strlen(B->in); render_input(); return;
    }
    if(kw("DELETE")){ int a=(int)expr_num(), b=a; if(ch('-')) b=(int)expr_num(); for(int i=B->nl-1;i>=0;i--) if(B->lnum[i]>=a&&B->lnum[i]<=b) del_line(B->lnum[i]); ready(); return; }
    if(kw("RENUM")){ for(int i=0;i<B->nl;i++) B->lnum[i]=(i+1)*10; outs("Renumbered (GOTO targets not updated)\n"); ready(); return; }
    if(kw("BYE")||kw("EXIT")){ wm_close(B->win); return; }
    /* run statement(s) directly */
    B->p=B->imm; B->cl=-1; B->brk=0;
    B->state=ST_RUN; run_slice();
}

/* ------------------------------------------------------------------ window */
static void bs_draw(window_t *w){
    basic_t *b=w->st; B=b;
    int cw=win_cw(w);
    blit(0,0,SW,SH,b->g,SW);
    /* cursor */
    if((b->state==ST_READY||b->state==ST_INPUT) && ((ticks/400)&1)==0){
        int r=b->row, c=b->col; if(c>=COLS){ c=0; r++; }
        if(r<ROWS) fill_rect(c*8,r*16+13,8,3,qpal[b->fg&15]);
    }
    fill_rect(0,SH,cw,STATUS_H,RGB(30,30,60));
    const char *st = b->state==ST_READY?"Ready": b->state==ST_INPUT?"Waiting for input": b->state==ST_SLEEP||b->state==ST_WAITKEY?"Sleeping":
                     b->state==ST_PLAY?"Playing music":"Running - press Esc to stop";
    draw_text(8,SH+3,st,RGB(200,200,255));
    char info[64]; itoa10(b->nl,info); strcat0(info," lines",64);
    if(b->fname[0]){ strcat0(info,"  -  ",64); strcat0(info,b->fname,64); }
    draw_text(cw-text_width(info)-8,SH+3,info,RGB(160,160,210));
}
static void type_text(basic_t *b,const char *t);
static void bs_key(window_t *w,int k){
    basic_t *b=w->st; B=b;
    if(k==KEY_ESC || k==KEY_CTRL('c')){
        if(b->state==ST_RUN){ b->brk=1; return; }
        if(b->state!=ST_READY){ if(b->col) newline(); outs("Break\n"); ready(); return; }
        if(k==KEY_CTRL('c')){ clip_set(b->in,b->inlen); wm_toast("Copied line"); }
        return;
    }
    if(k==KEY_CTRL('v')){ const char *t=clip_get(); if(t && (b->state==ST_READY||b->state==ST_INPUT)) type_text(b,t); return; }
    if(b->state==ST_RUN || b->state==ST_SLEEP || b->state==ST_PLAY){
        if(k<0x200){ int n=(b->kh+1)%32; if(n!=b->kt){ b->keyq[b->kh]=k; b->kh=n; } }
        return;
    }
    if(b->state==ST_WAITKEY){ after_wait(); if(b->state==ST_RUN) run_slice(); return; }
    /* line editing */
    if(k=='\n'){
        b->in[b->inlen]=0;
        if(b->state==ST_INPUT){ finish_input(); return; }
        strncpy0(b->last_cmd,b->in,sizeof b->last_cmd);
        newline(); char line[300]; strncpy0(line,b->in,sizeof line);
        immediate(line); return;
    }
    if(k=='\b'){ if(b->inlen){ b->inlen--; render_input(); } return; }
    if(k==KEY_UP && b->state==ST_READY){ strncpy0(b->in,b->last_cmd,sizeof b->in); b->inlen=strlen(b->in); render_input(); return; }
    if(k>=32 && k<127 && b->inlen<250){ b->in[b->inlen++]=k; render_input(); }
}
static void type_text(basic_t *b,const char *t){
    for(;*t;t++){
        if(*t=='\r') continue;
        if(*t=='\t'){ bs_key(b->win,' '); continue; }
        bs_key(b->win,*t=='\n'?'\n':*t);
        if(b->state==ST_RUN) break;   /* a pasted RUN started the program */
    }
}
static void bs_tick(window_t *w){
    basic_t *b=w->st; B=b;
    if(b->state==ST_RUN) run_slice();
    else if(b->state==ST_SLEEP){ if((int)(ticks-b->wake)>=0 || b->kh!=b->kt){ after_wait(); if(b->state==ST_RUN) run_slice(); } }
    else if(b->state==ST_PLAY){
        if((int)(ticks-b->wake)>=0){
            if(b->play_i>=b->play_n){ after_wait(); if(b->state==ST_RUN) run_slice(); return; }
            u32 f=b->play_f[b->play_i], ms=b->play_ms[b->play_i]; b->play_i++;
            if(f) piano_note(f, ms>40?ms-10:ms);
            b->wake=ticks+ms;
        }
    }
    static u32 blink; if(ticks/400!=blink){ blink=ticks/400; if(b->state==ST_READY||b->state==ST_INPUT) wm_invalidate(); }
}
static void bs_close(window_t *w){
    basic_t *b=w->st; B=b; free_vars(); new_prog(); kfree(b->g); if(B==b) B=0;
}

static window_t *basic_win;
static void bs_close_hook(window_t *w){ bs_close(w); basic_win=0; }

static const char *const samples[][2]={
 {"hello.bas",
  "10 CLS\n20 COLOR 14\n30 PRINT \"Hello from CamadamOS BASIC!\"\n40 COLOR 7\n50 INPUT \"What is your name\"; N$\n"
  "60 PRINT \"Nice to meet you, \"; N$; \"!\"\n70 FOR I = 1 TO 5\n80 COLOR I + 8\n90 PRINT TAB(I * 3); \"*** \"; N$; \" ***\"\n100 NEXT I\n110 COLOR 7\n120 END\n"},
 {"guess.bas",
  "10 REM Number guessing game\n20 RANDOMIZE TIMER\n30 N = INT(RND * 100) + 1\n40 T = 0\n50 PRINT \"I am thinking of a number from 1 to 100.\"\n"
  "60 INPUT \"Your guess\"; G\n70 T = T + 1\n80 IF G < N THEN PRINT \"Higher!\": GOTO 60\n90 IF G > N THEN PRINT \"Lower!\": GOTO 60\n"
  "100 PRINT \"Correct! You got it in\"; T; \"tries.\"\n110 BEEP\n"},
 {"circles.bas",
  "10 REM Colourful circles\n20 CLS\n30 RANDOMIZE TIMER\n40 FOR I = 1 TO 200\n50 X = RND * 640: Y = RND * 400\n"
  "60 CIRCLE (X, Y), RND * 40 + 5, INT(RND * 15) + 1\n70 NEXT I\n80 COLOR 15: LOCATE 25, 1: PRINT \"Press a key\";\n90 SLEEP\n100 CLS\n"},
 {"spiral.bas",
  "10 REM Line spiral\n20 CLS\n30 FOR A = 0 TO 60 STEP 0.05\n40 R = A * 3\n50 X = 320 + R * COS(A): Y = 200 + R * SIN(A) * 0.6\n"
  "60 IF A > 0 THEN LINE (PX, PY)-(X, Y), (A * 4) MOD 14 + 2\n70 PX = X: PY = Y\n80 NEXT A\n"},
 {"music.bas",
  "10 REM PLAY uses the music macro language\n20 PRINT \"Playing Ode to Joy...\"\n"
  "30 PLAY \"T180 L4 O4 E E F G G F E D C C D E E. L8 D L2 D\"\n"
  "40 PRINT \"Now a scale with SOUND:\"\n50 REM SOUND frequency, duration (18 = 1 second)\n"
  "60 FOR I = 0 TO 7\n70 READ F\n80 SOUND F, 4\n90 NEXT I\n100 DATA 262,294,330,349,392,440,494,523\n"},
 {"bounce.bas",
  "10 REM Bouncing ball - Esc to stop\n20 CLS\n30 X = 320: Y = 200: DX = 4: DY = 3\n"
  "40 CIRCLE (X, Y), 12, 1, 1\n50 X = X + DX: Y = Y + DY\n60 IF X < 12 OR X > 627 THEN DX = -DX: SOUND 600, 1\n"
  "70 IF Y < 12 OR Y > 387 THEN DY = -DY: SOUND 400, 1\n80 CIRCLE (X, Y), 12, 14, 1\n90 SLEEP 0.02\n100 GOTO 40\n"},
 {0,0}};
void basic_install_samples(void){
    int d=fs_lookup("/Programs"); if(d<0) d=fs_create(0,"Programs",FT_DIR); if(d<0) return;
    for(int i=0;samples[i][0];i++){
        if(fs_find(d,samples[i][0])>=0) continue;
        int f=fs_create(d,samples[i][0],FT_TEXT); if(f<0) return;
        fs_nodes[f].data=(char*)samples[i][1]; fs_nodes[f].size=strlen(samples[i][1]); fs_nodes[f].readonly=1;
    }
}

void app_basic(const char *file){
    if(basic_win){
        B=basic_win->st;
        if(file){ int f=fs_lookup(file); if(B->state!=ST_READY){ B->brk=0; ready(); }
            if(load_file(f)){ if(B->col) newline(); outs("Loaded "); outs(B->fname); outs(" - type RUN\n"); ready(); } }
        wm_focus(basic_win);
        return;
    }
    window_t *w=wm_open("BASIC",SW,SH+STATUS_H,IC_BASIC); if(!w) return;
    basic_t *b=kzalloc(sizeof *b); if(!b){ wm_close(w); return; }
    b->g=kmalloc(SW*SH*4); b->fg=7; b->bg=1; b->seed=12345; b->win=w; B=b; basic_win=w;
    w->st=b; w->draw=bs_draw; w->key=bs_key; w->tick=bs_tick; w->close=bs_close_hook;
    cls();
    b->fg=15; outs("CamadamOS BASIC 1.0"); newline(); b->fg=7;
    outs("Type HELP for commands, FILES to see programs, LOAD \"guess\" to load one.\n");
    if(file){ int f=fs_lookup(file); if(load_file(f)){ outs("Loaded "); outs(b->fname); outs(" - type RUN\n"); } }
    ready();
}
