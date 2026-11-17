/* CamadamOS Web: a small HTML 1-3.2 era browser in the spirit of
 * Mosaic / Internet Explorer 3. Plain HTTP over our own TCP/IP stack;
 * HTTPS pages are fetched through FrogFind, which simplifies modern sites
 * for vintage browsers. Local pages use cam://name (files in /Pages). */
#include "kernel.h"

#define TOOL_H 44
#define STATUS_H 22
#define MARGIN 14
#define MAXITEMS 12000
#define MAXLINKS 1500
#define MAXINPUTS 48
#define MAXFORMS 12
#define FROGFIND "http://frogfind.com"

enum { KI_TEXT, KI_HR, KI_INPUT, KI_BUTTON, KI_BULLET, KI_IMG };
#define MAXIMG 40
enum { ST_BOLD=1, ST_ITAL=2, ST_UND=4, ST_LINK=8, ST_PRE=16, ST_GRAY=32, ST_HEAD=64 };
typedef struct { s16 x,y,w,h; u8 kind, style, scale; s16 link, inp; u32 off; u16 n; } item_t;
typedef struct { int form; u32 name; int type; char value[160]; int size; } input_t; /* type 0 text, 1 hidden, 2 submit, 3 password */
typedef struct { u32 action; int post; } form_t;
typedef struct { char src[400]; char alt[48]; int aw, ah, w, h, state; u32 *px; } webimg_t; /* state 0 pending, 1 loaded, 2 failed */
typedef struct {
    char url[600], title[100];
    char *arena; u32 alen, acap;
    item_t *it; int nit;
    u32 links[MAXLINKS]; int nlinks;
    input_t inp[MAXINPUTS]; int ninp;
    form_t forms[MAXFORMS]; int nforms;
    int height; u32 bg, fg, linkc;
    webimg_t img[MAXIMG]; int nimg, img_seen;
    char *src; u32 srclen; int width;
} page_t;
typedef struct {
    page_t pg; int scroll;
    char addr[600]; int addr_focus, addr_fresh;
    int focus_inp; char status[160]; int hover_link;
    char hist[24][600]; int nhist;
    int loading;
} web_t;
static window_t *loading_win;

/* ---------------- text helpers ---------------- */
const char *translit(u32 c){
    static char one[2];
    if(c<128){ one[0]=(char)c; one[1]=0; return one; }
    switch(c){
    case 0xE7: return "c"; case 0xC7: return "C"; case 0x11F: return "g"; case 0x11E: return "G";
    case 0x131: return "i"; case 0x130: return "I"; case 0xF6: return "o"; case 0xD6: return "O";
    case 0x15F: return "s"; case 0x15E: return "S"; case 0xFC: return "u"; case 0xDC: return "U";
    case 0xA0: return " "; case 0xA9: return "(c)"; case 0xAE: return "(R)"; case 0xB0: return "o";
    case 0xAB: return "<<"; case 0xBB: return ">>"; case 0xB7: return "."; case 0xD7: return "x";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: return "\"";
    case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "..."; case 0x2022: return "*"; case 0x20AC: return "EUR"; case 0x20BA: return "TL";
    case 0x2122: return "TM"; case 0x2190: return "<-"; case 0x2192: return "->";
    }
    if(c>=0xC0 && c<=0xFF){
        static const char *lat="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        one[0]=lat[c-0xC0]; one[1]=0; return one;
    }
    return "?";
}
u32 utf8(const char *s,u32 n,u32 *i){
    u8 c=(u8)s[*i];
    if(c<0x80){ (*i)++; return c; }
    int len = (c>=0xF0)?4:(c>=0xE0)?3:(c>=0xC0)?2:0;
    if(!len || *i+len>n){ (*i)++; return c; }
    u32 v = c & (0x3F>>(len-1));
    for(int k=1;k<len;k++){ u8 d=(u8)s[*i+k]; if((d&0xC0)!=0x80){ (*i)++; return c; } v=(v<<6)|(d&0x3F); }
    *i+=len; return v;
}
static int ieq(const char *a,const char *b){ while(*a&&*b){ char x=*a,y=*b; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return 0; a++; b++; } return !*a && !*b; }
static int istarts(const char *s,u32 n,const char *p){ u32 k=0; while(p[k]){ if(k>=n) return 0; char x=s[k]; if(x>='A'&&x<='Z') x+=32; if(x!=p[k]) return 0; k++; } return 1; }
/* decode one entity at s[*i]=='&'; returns ASCII text */
static const char *entity(const char *s,u32 n,u32 *i){
    u32 j=*i+1; char nm[12]; int k=0;
    while(j<n && k<11 && s[j]!=';' && s[j]!=' ' && s[j]!='<' && s[j]!='&'){ nm[k++]=s[j++]; }
    nm[k]=0;
    if(j>=n || s[j]!=';'){ (*i)++; return "&"; }
    *i=j+1;
    if(nm[0]=='#'){
        u32 v=0;
        if(nm[1]=='x'||nm[1]=='X'){ for(int t=2;nm[t];t++){ char c=nm[t]; v=v*16+((c>='0'&&c<='9')?c-'0':((c|32)-'a'+10)); } }
        else for(int t=1;nm[t];t++) v=v*10+(nm[t]-'0');
        return translit(v);
    }
    static const char *tab[][2]={{"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},{"nbsp"," "},{"copy","(c)"},
        {"reg","(R)"},{"mdash","-"},{"ndash","-"},{"hellip","..."},{"laquo","<<"},{"raquo",">>"},{"middot","."},{"bull","*"},
        {"lsquo","'"},{"rsquo","'"},{"ldquo","\""},{"rdquo","\""},{"times","x"},{"euro","EUR"},{"deg","o"},{"trade","TM"},
        {"ccedil","c"},{"Ccedil","C"},{"ouml","o"},{"Ouml","O"},{"uuml","u"},{"Uuml","U"},{"eacute","e"},{"egrave","e"},{"aacute","a"},{"agrave","a"},{0,0}};
    for(int t=0;tab[t][0];t++) if(!strcmp(tab[t][0],nm)) return tab[t][1];
    return "?";
}
static u32 parse_color(const char *v,u32 def){
    if(!v||!*v) return def;
    if(*v=='#') v++;
    static const struct { const char *n; u32 c; } named[]={{"white",0xFFFFFF},{"black",0},{"navy",0x000080},{"blue",0x0000FF},
        {"red",0xFF0000},{"green",0x008000},{"yellow",0xFFFF00},{"gray",0x808080},{"grey",0x808080},{"silver",0xC0C0C0},
        {"maroon",0x800000},{"purple",0x800080},{"teal",0x008080},{"aqua",0x00FFFF},{"lime",0x00FF00},{"olive",0x808000},{0,0}};
    for(int i=0;named[i].n;i++) if(ieq(named[i].n,v)) return named[i].c;
    u32 c=0; int k=0;
    for(;k<6 && v[k];k++){ char ch=v[k]; int d=(ch>='0'&&ch<='9')?ch-'0':((ch|32)>='a'&&(ch|32)<='f')?(ch|32)-'a'+10:-1; if(d<0) return def; c=c*16+d; }
    return k==6?c:def;
}

/* ---------------- URL helpers ---------------- */
static void url_encode(const char *s,char *o,int n){
    const char *h="0123456789ABCDEF"; int k=0;
    for(;*s && k<n-4;s++){
        u8 c=(u8)*s;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') o[k++]=c;
        else if(c==' ') o[k++]='+';
        else { o[k++]='%'; o[k++]=h[c>>4]; o[k++]=h[c&15]; }
    }
    o[k]=0;
}
static void url_resolve(const char *base,const char *href,char *out,int n){
    while(*href==' ') href++;
    if(!strncmp(href,"http://",7)||!strncmp(href,"https://",8)||!strncmp(href,"cam:",4)||!strncmp(href,"file:",5)){ strncpy0(out,href,n); return; }
    if(href[0]=='/' && href[1]=='/'){ strncpy0(out,"http:",n); strcat0(out,href,n); return; }
    char b[600]; strncpy0(b,base,sizeof b);
    char *hash=b; while(*hash && *hash!='#') hash++; *hash=0;
    if(href[0]=='#'||!href[0]){ strncpy0(out,b,n); return; }
    char *scheme_end=b; while(*scheme_end && strncmp(scheme_end,"://",3)) scheme_end++;
    char *host_end = *scheme_end ? scheme_end+3 : b; while(*host_end && *host_end!='/') host_end++;
    if(href[0]=='/'){ *host_end=0; strncpy0(out,b,n); strcat0(out,href,n); return; }
    if(href[0]=='?'){ char *q=b; while(*q && *q!='?') q++; *q=0; strncpy0(out,b,n); strcat0(out,href,n); return; }
    char *q=b; while(*q && *q!='?') q++; *q=0;
    char *slash=0; for(char *p=host_end;*p;p++) if(*p=='/') slash=p;
    if(slash) slash[1]=0; else strcat0(b,"/",sizeof b);
    while(!strncmp(href,"../",3)){ href+=3; char *l=b+strlen(b)-1; if(l>host_end){ *l=0; while(l>host_end && *l!='/') *l--=0; } }
    while(!strncmp(href,"./",2)) href+=2;
    strncpy0(out,b,n); strcat0(out,href,n);
}

/* ---------------- layout engine ---------------- */
static page_t *P;
static int L_x, L_y, L_lh, L_left, L_right, L_line0, L_space, L_style, L_scale, L_link, L_center, L_pre, L_gap;
static int L_list[8], L_listn, L_listtype[8], L_indent, L_form, L_cells, L_skip_button;
static u32 arena_put(const char *s,int n){
    if(P->alen+n+1>P->acap) return 0;
    u32 o=P->alen; memcpy(P->arena+o,s,n); P->arena[o+n]=0; P->alen+=n+1; return o;
}
static item_t *new_item(int kind,int x,int y,int w,int h){
    if(P->nit>=MAXITEMS) return 0;
    item_t *t=&P->it[P->nit++]; memset(t,0,sizeof *t);
    t->kind=kind; t->x=x; t->y=y; t->w=w; t->h=h; t->style=L_style; t->scale=L_scale; t->link=L_link; t->inp=-1;
    return t;
}
static void newline(void){
    if(L_center && L_x>L_left){
        int shift=(L_right-L_x)/2;
        for(int i=L_line0;i<P->nit;i++) P->it[i].x+=shift;
    }
    /* align items on the line to the baseline of the tallest */
    for(int i=L_line0;i<P->nit;i++) if(P->it[i].kind!=KI_IMG && P->it[i].scale==1 && L_lh>20) P->it[i].y+=L_lh-22;
    L_y+=L_lh; L_x=L_left; L_lh=18; L_line0=P->nit; L_space=0; L_gap=0;
}
static void para(void){ if(L_x>L_left) newline(); if(!L_gap && L_y>8){ L_y+=10; L_gap=1; } }
static void add_word(const char *s,int n){
    if(n<=0) return;
    int cw=8*L_scale, w=n*cw;
    if(L_space && L_x>L_left) L_x+=cw;
    if(L_x+w>L_right && L_x>L_left) newline();
    item_t *t=new_item(KI_TEXT,L_x,L_y,w,16*L_scale); if(!t) return;
    t->off=arena_put(s,n); t->n=n;
    L_x+=w; if(16*L_scale+2>L_lh) L_lh=16*L_scale+4;
    L_space=0; L_gap=0;
}
static void text_run(const char *s,u32 n){
    char word[256]; int wn=0;
    for(u32 i=0;i<n;){
        const char *rep; char c=s[i];
        if(c=='&') rep=entity(s,n,&i);
        else if((u8)c>=0x80){ u32 cp=utf8(s,n,&i); rep=translit(cp); }
        else { rep=0; i++; }
        if(!rep){
            if(L_pre){
                if(c=='\n'){ add_word(word,wn); wn=0; L_x=L_x; newline(); continue; }
                if(c=='\r') continue;
                if(c=='\t'){ add_word(word,wn); wn=0; int col=(L_x-L_left)/8; L_x=L_left+((col/8)+1)*64; continue; }
                if(c==' '){ add_word(word,wn); wn=0; L_x+=8; continue; }
            } else if(c==' '||c=='\n'||c=='\r'||c=='\t'){ add_word(word,wn); wn=0; L_space=1; continue; }
            if(wn<255) word[wn++]=c;
        } else {
            for(;*rep;rep++){ if(*rep==' ' && !L_pre){ add_word(word,wn); wn=0; L_space=1; } else if(wn<255) word[wn++]=*rep; }
        }
        if(wn>=(L_right-L_left)/8-1){ add_word(word,wn); wn=0; }
    }
    add_word(word,wn);
}
/* attributes of the tag being processed */
static char A_name[8][16], A_val[8][300]; static int A_n;
static const char *attr(const char *n){ for(int i=0;i<A_n;i++) if(!strcmp(A_name[i],n)) return A_val[i]; return 0; }
static void get_attr_text(const char *v,char *o,int on){ /* decode entities in attribute values */
    int k=0; u32 n=strlen(v);
    for(u32 i=0;i<n && k<on-1;){ const char *r; if(v[i]=='&') r=entity(v,n,&i); else if((u8)v[i]>=0x80){ u32 cp=utf8(v,n,&i); r=translit(cp); } else { o[k++]=v[i++]; continue; } while(*r && k<on-1) o[k++]=*r++; }
    o[k]=0;
}
static void start_block_align(void){ const char *a=attr("align"); if(a && ieq(a,"center")) L_center|=2; }
static void end_block_align(void){ if(L_center&2){ if(L_x>L_left) newline(); L_center&=~2; } }
static void tag(const char *name,int closing){
    #define T(x) (!strcmp(name,x))
    if(T("br")){ if(L_x==L_left) L_y+=L_lh; else newline(); return; }
    if(T("p")||T("div")){ para(); if(closing) end_block_align(); else start_block_align(); return; }
    if(name[0]=='h' && name[1]>='1' && name[1]<='6' && !name[2]){
        if(!closing){ para(); start_block_align(); L_style|=ST_BOLD|ST_HEAD; if(name[1]=='1') L_scale=2; }
        else { if(L_x>L_left) newline(); L_style&=~(ST_BOLD|ST_HEAD); L_scale=1; end_block_align(); L_gap=0; para(); }
        return;
    }
    if(T("hr")){ para(); new_item(KI_HR,L_left,L_y+4,L_right-L_left,2); L_y+=12; L_gap=1; return; }
    if(T("ul")||T("ol")||T("menu")||T("dir")){
        if(!closing){ if(L_listn<8){ L_listtype[L_listn]=T("ol"); L_list[L_listn]=0; L_listn++; } if(L_listn==1) para(); else if(L_x>L_left) newline(); L_indent+=28; L_left=MARGIN+L_indent; L_x=L_left; }
        else { if(L_listn>0) L_listn--; L_indent=imax(0,L_indent-28); if(L_x>L_left) newline(); L_left=MARGIN+L_indent; L_x=L_left; if(!L_listn) para(); }
        return;
    }
    if(T("li") && !closing){
        if(L_x>L_left) newline();
        if(L_listn && L_listtype[L_listn-1]){ char b[8]; itoa10(++L_list[L_listn-1],b); strcat0(b,".",8); int w=(int)strlen(b)*8; item_t *t=new_item(KI_TEXT,L_left-w-6,L_y,w,16); if(t){ t->style=0; t->off=arena_put(b,(int)strlen(b)); t->n=(u16)strlen(b); } }
        else { item_t *t=new_item(KI_BULLET,L_left-14,L_y+5,6,6); if(t) t->style=0; }
        L_gap=0; return;
    }
    if(T("dl")){ para(); return; }
    if(T("dt")&&!closing){ if(L_x>L_left) newline(); L_left=MARGIN+L_indent; L_x=L_left; return; }
    if(T("dd")&&!closing){ if(L_x>L_left) newline(); L_left=MARGIN+L_indent+32; L_x=L_left; return; }
    if(T("blockquote")){ para(); L_indent+=closing?-32:32; if(L_indent<0) L_indent=0; L_left=MARGIN+L_indent; L_right=L_right+(closing?32:-32); L_x=L_left; return; }
    if(T("pre")||T("listing")||T("xmp")){ para(); L_pre=!closing; if(closing) L_style&=~ST_PRE; else L_style|=ST_PRE; return; }
    if(T("center")){ if(L_x>L_left) newline(); if(closing) L_center&=~1; else L_center|=1; return; }
    if(T("b")||T("strong")){ if(closing) L_style&=~ST_BOLD; else L_style|=ST_BOLD; return; }
    if(T("i")||T("em")||T("cite")||T("var")||T("address")){ if(closing) L_style&=~ST_ITAL; else L_style|=ST_ITAL; if(T("address")) para(); return; }
    if(T("u")){ if(closing) L_style&=~ST_UND; else L_style|=ST_UND; return; }
    if(T("tt")||T("code")||T("kbd")||T("samp")){ if(closing) L_style&=~ST_PRE; else L_style|=ST_PRE; return; }
    if(T("a")){
        if(closing){ L_link=-1; L_style&=~ST_LINK; return; }
        const char *h=attr("href");
        if(h && P->nlinks<MAXLINKS){ char d[300]; get_attr_text(h,d,sizeof d); P->links[P->nlinks]=arena_put(d,(int)strlen(d)); L_link=P->nlinks++; L_style|=ST_LINK; }
        return;
    }
    if(T("img")){
        const char *a=attr("alt"); char d[120]="";
        if(a) get_attr_text(a,d,sizeof d);
        const char *src=attr("src");
        int idx=-1;
        if(src && *src && P->img_seen<MAXIMG){
            idx=P->img_seen++;
            if(idx>=P->nimg){
                webimg_t *im=&P->img[idx]; memset(im,0,sizeof *im);
                get_attr_text(src,im->src,sizeof im->src); strncpy0(im->alt,d,sizeof im->alt);
                const char *ws=attr("width"), *hs=attr("height");
                if(ws){ for(;*ws>='0'&&*ws<='9';ws++) im->aw=im->aw*10+(*ws-'0'); }
                if(hs){ for(;*hs>='0'&&*hs<='9';hs++) im->ah=im->ah*10+(*hs-'0'); }
                P->nimg=idx+1;
            }
        }
        webimg_t *im = idx>=0 ? &P->img[idx] : 0;
        int bw=0, bh=0;
        if(im && im->state==1){ bw=im->w; bh=im->h; }
        else if(im && im->state==0 && im->aw>0 && im->ah>0){ bw=im->aw; bh=im->ah; }
        if(bw>0 && bh>0){
            int maxw=L_right-L_left; if(bw>maxw){ bh=bh*maxw/bw; bw=maxw; }
            if(L_space && L_x>L_left) L_x+=8;
            if(L_x+bw>L_right && L_x>L_left) newline();
            item_t *t=new_item(KI_IMG,L_x,L_y,bw,bh); if(!t) return;
            t->inp=idx; L_x+=bw; if(bh+4>L_lh) L_lh=bh+4; L_space=0; L_gap=0;
            return;
        }
        int saved=L_style; L_style|=ST_GRAY; if(L_link>=0) L_style|=ST_LINK;
        char b[140]; strncpy0(b,"[",sizeof b); strcat0(b,d[0]?d:"image",sizeof b); strcat0(b,"]",sizeof b);
        if(d[0] || L_link>=0 || (im && im->state==0)){ L_space=1; text_run(b,(u32)strlen(b)); L_space=1; }
        L_style=saved; return;
    }
    if(T("tr")){ if(L_x>L_left) newline(); L_cells=0; return; }
    if((T("td")||T("th")) && !closing){ if(L_cells++>0){ L_x+=16; } if(T("th")) L_style|=ST_BOLD; return; }
    if(T("th") && closing){ L_style&=~ST_BOLD; return; }
    if(T("table")){ para(); return; }
    if(T("form")){
        if(closing){ L_form=-1; para(); return; }
        if(P->nforms<MAXFORMS){ const char *a=attr("action"); char d[300]; get_attr_text(a?a:"",d,sizeof d);
            P->forms[P->nforms].action=arena_put(d,(int)strlen(d)); const char *m=attr("method"); P->forms[P->nforms].post=m&&ieq(m,"post"); L_form=P->nforms++; }
        return;
    }
    if((T("input")||T("button")||T("textarea")||T("select")) && !closing){
        const char *ty=attr("type"); const char *nm=attr("name"); const char *va=attr("value");
        int type=0;
        if(T("button")){ type=2; L_skip_button=1; }
        else if(T("textarea")||T("select")) type=0;
        else if(ty){ if(ieq(ty,"hidden")) type=1; else if(ieq(ty,"submit")||ieq(ty,"image")||ieq(ty,"button")) type=2;
            else if(ieq(ty,"password")) type=3; else if(ieq(ty,"checkbox")||ieq(ty,"radio")||ieq(ty,"reset")||ieq(ty,"file")) return; }
        if(P->ninp>=MAXINPUTS) return;
        input_t *in=&P->inp[P->ninp]; memset(in,0,sizeof *in);
        in->form=L_form; in->type=type;
        char d[300]; get_attr_text(nm?nm:"",d,sizeof d); in->name=arena_put(d,(int)strlen(d));
        if(va) get_attr_text(va,in->value,sizeof in->value);
        if(type==1){ P->ninp++; return; }
        if(type==2){
            const char *lab=in->value[0]?in->value:"Submit";
            int w=text_width(lab)+20;
            if(L_x+w+8>L_right && L_x>L_left) newline();
            if(L_space||L_x>L_left) L_x+=6;
            item_t *t=new_item(KI_BUTTON,L_x,L_y-2,w,22); if(!t) return;
            t->inp=P->ninp; t->link=-1; L_x+=w+6; if(L_lh<26) L_lh=26;
        } else {
            const char *sz=attr("size"); int s=20; if(sz){ s=0; for(;*sz>='0'&&*sz<='9';sz++) s=s*10+(*sz-'0'); }
            s=clampi(s,6,48); in->size=s;
            int w=s*8+10;
            if(L_x+w>L_right && L_x>L_left) newline();
            if(L_x>L_left) L_x+=6;
            if(w>L_right-L_x) w=L_right-L_x;
            item_t *t=new_item(KI_INPUT,L_x,L_y-2,w,22); if(!t) return;
            t->inp=P->ninp; t->link=-1; L_x+=w+4; if(L_lh<26) L_lh=26;
        }
        P->ninp++; L_space=0; return;
    }
    if(T("button") && closing){ L_skip_button=0; return; }
    if(T("body") && !closing){ const char *bg=attr("bgcolor"), *tx=attr("text"), *lk=attr("link");
        P->bg=parse_color(bg,P->bg); P->fg=parse_color(tx,P->fg); P->linkc=parse_color(lk,P->linkc);
        /* keep text readable if a page sets clashing colours */
        int lb=((P->bg>>16)&255)+((P->bg>>8)&255)+(P->bg&255), lf=((P->fg>>16)&255)+((P->fg>>8)&255)+(P->fg&255);
        if(iabs(lb-lf)<200){ P->fg = lb>380?0:0xFFFFFF; }
        int ll=((P->linkc>>16)&255)+((P->linkc>>8)&255)+(P->linkc&255);
        if(iabs(lb-ll)<150) P->linkc = lb>380?RGB(0,0,200):RGB(130,180,255);
        return; }
    #undef T
}
static void layout(page_t *pg,const char *h,u32 n,int width){
    P=pg; pg->nit=0; pg->nlinks=0; pg->ninp=0; pg->nforms=0; pg->alen=1; pg->title[0]=0; pg->img_seen=0;
    pg->bg=RGB(255,255,255); pg->fg=RGB(20,20,30); pg->linkc=RGB(20,60,200);
    L_left=MARGIN; L_right=width-MARGIN; L_x=L_left; L_y=10; L_lh=18; L_line0=0; L_space=0; L_style=0; L_scale=1; L_link=-1;
    L_center=0; L_pre=0; L_gap=1; L_listn=0; L_indent=0; L_form=-1; L_cells=0; L_skip_button=0;
    u32 i=0;
    while(i<n){
        if(h[i]=='<'){
            if(i+3<n && h[i+1]=='!' && h[i+2]=='-' && h[i+3]=='-'){ u32 j=i+4; while(j+2<n && !(h[j]=='-'&&h[j+1]=='-'&&h[j+2]=='>')) j++; i=j+3; continue; }
            if(i+1<n && (h[i+1]=='!'||h[i+1]=='?')){ while(i<n && h[i]!='>') i++; i++; continue; }
            u32 j=i+1; int closing=0;
            if(j<n && h[j]=='/'){ closing=1; j++; }
            char name[16]; int k=0;
            while(j<n && h[j]!='>' && h[j]!=' ' && h[j]!='\n' && h[j]!='\t' && h[j]!='\r' && h[j]!='/'){ if(k<15){ char c=h[j]; if(c>='A'&&c<='Z') c+=32; name[k++]=c; } j++; }
            name[k]=0;
            if(!k || !((name[0]>='a'&&name[0]<='z'))){ text_run(h+i,1); i++; continue; }
            A_n=0;
            while(j<n && h[j]!='>'){
                while(j<n && (h[j]==' '||h[j]=='\n'||h[j]=='\t'||h[j]=='\r'||h[j]=='/')) j++;
                if(j>=n||h[j]=='>') break;
                char an[16]; int ak=0;
                while(j<n && h[j]!='=' && h[j]!='>' && h[j]!=' ' && h[j]!='\n' && h[j]!='\t'){ if(ak<15){ char c=h[j]; if(c>='A'&&c<='Z') c+=32; an[ak++]=c; } j++; }
                an[ak]=0;
                char *dst = A_n<8 ? A_val[A_n] : 0; int vk=0;
                while(j<n && (h[j]==' '||h[j]=='\n')) j++;
                if(j<n && h[j]=='='){
                    j++; while(j<n && (h[j]==' '||h[j]=='\n')) j++;
                    char q=0; if(j<n && (h[j]=='"'||h[j]=='\'')) q=h[j++];
                    while(j<n && (q ? h[j]!=q : (h[j]!=' '&&h[j]!='>'&&h[j]!='\n'))){ if(dst && vk<299) dst[vk++]=h[j]; j++; }
                    if(q && j<n) j++;
                }
                if(dst && ak){ dst[vk]=0; strncpy0(A_name[A_n],an,16); A_n++; }
            }
            i=j+1;
            if(!closing && (!strcmp(name,"script")||!strcmp(name,"style")||!strcmp(name,"title")||!strcmp(name,"noscript")||!strcmp(name,"head") )){
                if(!strcmp(name,"head")) continue;
                char end[16]="</"; strcat0(end,name,16);
                u32 s0=i; while(i<n && !istarts(h+i,n-i,end)) i++;
                if(!strcmp(name,"title")){
                    char t[200]; int tk=0;
                    for(u32 q=s0;q<i && tk<199;){ const char *r; if(h[q]=='&') r=entity(h,i,&q); else if((u8)h[q]>=0x80){ u32 cp=utf8(h,i,&q); r=translit(cp);} else { char c=h[q++]; if(c=='\n'||c=='\r'||c=='\t') c=' '; t[tk++]=c; continue; } while(*r && tk<199) t[tk++]=*r++; }
                    t[tk]=0; char *ts=t; while(*ts==' ') ts++; strncpy0(pg->title,ts,sizeof pg->title);
                }
                while(i<n && h[i]!='>') i++;
                i++; continue;
            }
            tag(name,closing);
            continue;
        }
        u32 j=i; while(j<n && h[j]!='<') j++;
        if(!L_skip_button) text_run(h+i,j-i);
        else if(L_skip_button && P->ninp>0){ /* use button inner text as its label */
            input_t *in=&P->inp[P->ninp-1]; if(!in->value[0]){ int k=0; for(u32 q=i;q<j && k<40;q++){ char c=h[q]; if(c=='\n'||c=='\r'||c=='\t') c=' '; if(c==' '&&(k==0)) continue; in->value[k++]=c; } in->value[k]=0;
                for(int t=pg->nit-1;t>=0;t--) if(pg->it[t].kind==KI_BUTTON && pg->it[t].inp==P->ninp-1){ pg->it[t].w=text_width(in->value[0]?in->value:"Submit")+20; break; } } }
        i=j;
    }
    if(L_x>L_left) newline();
    pg->height=L_y+20;
}

/* ---------------- loading ---------------- */
static void set_status(web_t *b,const char *s){ strncpy0(b->status,s,sizeof b->status); }
static void load_status(const char *s){
    if(!loading_win) return;
    web_t *b=loading_win->st; set_status(b,s); wm_invalidate(); wm_pump();
}
static void free_page(page_t *pg){
    kfree(pg->arena); kfree(pg->it); kfree(pg->src); pg->arena=0; pg->it=0; pg->src=0; pg->nit=0;
    for(int i=0;i<pg->nimg;i++){ kfree(pg->img[i].px); pg->img[i].px=0; }
    pg->nimg=0;
}
static void show_html(window_t *w,web_t *b,const char *html,u32 n){
    page_t *pg=&b->pg;
    free_page(pg);
    pg->acap=n+64*1024+16; pg->arena=kmalloc(pg->acap);
    pg->it=kmalloc(sizeof(item_t)*MAXITEMS);
    pg->src=kmalloc(n+1);
    if(!pg->arena||!pg->it||!pg->src){ free_page(pg); set_status(b,"Not enough memory to show this page."); return; }
    memcpy(pg->src,html,n); pg->src[n]=0; pg->srclen=n; pg->width=win_cw(w)-12;
    layout(pg,pg->src,n,pg->width);
    b->scroll=0; b->focus_inp=-1; b->hover_link=-1;
    char t[48]; strncpy0(t,"Web - ",sizeof t); strcat0(t,pg->title[0]?pg->title:pg->url,sizeof t); wm_set_title(w,t);
}
static void error_page(window_t *w,web_t *b,const char *what,const char *detail){
    static char h[2048];
    strncpy0(h,"<html><head><title>Can't open page</title></head><body><h1>Can't open this page</h1><p><b>",sizeof h);
    strcat0(h,what,sizeof h); strcat0(h,"</b></p><p>",sizeof h); strcat0(h,detail,sizeof h);
    strcat0(h,"</p><hr><p>Things to try:</p><ul><li>Check that an Ethernet cable is plugged in (Wi-Fi isn't supported).</li>"
        "<li>Open <b>Settings</b> and look at the <b>Network</b> page.</li>"
        "<li>Search with <a href=\"" FROGFIND "/\">FrogFind</a>, which also opens modern HTTPS sites.</li>"
        "<li>Go back to the <a href=\"cam://home\">start page</a>.</li></ul></body></html>",sizeof h);
    show_html(w,b,h,(u32)strlen(h));
}
static void relayout(web_t *b){ page_t *pg=&b->pg; if(pg->src) layout(pg,pg->src,pg->srclen,pg->width); }
static void name_from_url(const char *url,char *out){
    const char *p=url, *last=url; while(*p && *p!='?' && *p!='#'){ if(*p=='/') last=p+1; p++; }
    int k=0; for(const char *q=last;q<p && k<FS_NAME-1;q++){ char c=*q; if(c=='%'||c=='/'||c=='\\'||c==':'||c<32||c>126) c='_'; out[k++]=c; }
    out[k]=0; if(!k) strncpy0(out,"download",FS_NAME);
}
static void unique_in(int dir,char *name){
    if(fs_find(dir,name)<0) return;
    char base[FS_NAME], ext[FS_NAME]=""; strncpy0(base,name,FS_NAME);
    char *dot=0; for(char *q=base;*q;q++) if(*q=='.') dot=q;
    if(dot){ strncpy0(ext,dot,FS_NAME); *dot=0; }
    if(strlen(base)>20) base[20]=0;
    fs_unique_name(dir,base,ext,name);
}
static int img_fetch(const char *url,u8 **data,u32 *len,u32 **px,int *w,int *h){
    *data=0; *px=0;
    if(!strncmp(url,"file:",5)){
        const char *path=url+5; while(path[0]=='/'&&path[1]=='/') path++;
        int f=fs_lookup(path); if(f<0||!fs_nodes[f].data) return 0;
        if(fs_nodes[f].type==FT_IMAGE){ u32 n=fs_nodes[f].img_w*fs_nodes[f].img_h; *px=kmalloc(n*4); if(!*px) return 0; memcpy(*px,fs_nodes[f].data,n*4); *w=fs_nodes[f].img_w; *h=fs_nodes[f].img_h; return 1; }
        *px=image_decode((u8*)fs_nodes[f].data,fs_nodes[f].size,w,h); return *px!=0;
    }
    char u[700]; strncpy0(u,url,sizeof u);
    if(!strncmp(u,"https://",8)){ char e[600]; url_encode(url,e,sizeof e); strncpy0(u,FROGFIND "/image.php?i=",sizeof u); strcat0(u,e,sizeof u); }
    char *body; u32 n; char loc[400]; char ct[48];
    int st=http_get(u,&body,&n,loc,sizeof loc,ct,sizeof ct,0);
    if((st==301||st==302||st==303||st==307) && loc[0]){ kfree(body); char r[700]; url_resolve(u,loc,r,sizeof r);
        if(!strncmp(r,"https://",8)){ char e[600]; url_encode(r,e,sizeof e); strncpy0(r,FROGFIND "/image.php?i=",sizeof r); strcat0(r,e,sizeof r); }
        st=http_get(r,&body,&n,loc,sizeof loc,ct,sizeof ct,0); }
    if(st!=200||!body){ kfree(body); return 0; }
    *len=n; *px=image_decode((u8*)body,n,w,h); kfree(body);
    return *px!=0;
}
static void load_images(window_t *w,web_t *b){
    page_t *pg=&b->pg; u32 t0=ticks;
    int total=0; for(int i=0;i<pg->nimg;i++) if(!pg->img[i].state) total++;
    int k=0;
    for(int i=0;i<pg->nimg;i++){
        webimg_t *im=&pg->img[i]; if(im->state) continue;
        k++;
        if(ticks-t0>60000){ im->state=2; continue; }
        int c=kbd_get(); if(c==KEY_ESC){ for(int j=i;j<pg->nimg;j++) if(!pg->img[j].state) pg->img[j].state=2; break; }
        char msg[64], a[8]; strncpy0(msg,"Loading image ",sizeof msg); itoa10(k,a); strcat0(msg,a,sizeof msg); strcat0(msg," of ",sizeof msg); itoa10(total,a); strcat0(msg,a,sizeof msg); strcat0(msg,"... (Esc to skip)",sizeof msg);
        set_status(b,msg); wm_invalidate(); wm_pump();
        char url[700]; url_resolve(pg->url,im->src,url,sizeof url);
        u8 *data; u32 len; u32 *px; int iw=0, ih=0;
        if(!img_fetch(url,&data,&len,&px,&iw,&ih)){ im->state=2; continue; }
        int tw=iw, th=ih;
        if(im->aw>0 && im->ah>0){ tw=im->aw; th=im->ah; }
        else if(im->aw>0){ th=ih*im->aw/iw; tw=im->aw; }
        else if(im->ah>0){ tw=iw*im->ah/ih; th=im->ah; }
        int maxw=pg->width-2*MARGIN; if(tw>maxw){ th=th*maxw/tw; tw=maxw; }
        if(th>1600){ tw=tw*1600/th; th=1600; }
        if(tw<1) tw=1; if(th<1) th=1;
        im->px=image_scale(px,iw,ih,tw,th); im->w=tw; im->h=th; im->state=1;
        relayout(b); wm_invalidate(); wm_pump();
    }
    set_status(b,"Done");
    (void)w;
}
static void save_download(window_t *w,web_t *b,const char *url,char *body,u32 blen,const char *ctype){
    int dl=fs_lookup("/Downloads"); if(dl<0) dl=fs_create(0,"Downloads",FT_DIR);
    char name[FS_NAME]; name_from_url(url,name); unique_in(dl,name);
    int iw,ih; u32 *px=image_decode((u8*)body,blen,&iw,&ih);
    int f;
    if(px){ f=fs_create(dl,name,FT_IMAGE); if(f>=0){ fs_nodes[f].data=(char*)px; fs_nodes[f].img_w=iw; fs_nodes[f].img_h=ih; fs_nodes[f].size=iw*ih*4; fs_touch(); } else kfree(px); }
    else {
        int type = (!strncmp(ctype,"text/",5)||ends_with(name,".txt")) ? FT_TEXT : (ends_with(name,".htm")||ends_with(name,".html")) ? FT_PAGE : FT_BIN;
        f=fs_create(dl,name,type);
        if(f>=0){ char *c=kmalloc(blen+1); if(c){ memcpy(c,body,blen); c[blen]=0; fs_nodes[f].data=c; fs_nodes[f].size=blen; fs_touch(); } }
    }
    static char h[1600]; char kb[16]; itoa10((int)((blen+1023)/1024),kb);
    strncpy0(h,"<html><head><title>Download complete</title></head><body><h1>Download complete</h1><p>Saved <b>",sizeof h);
    strcat0(h,name,sizeof h); strcat0(h,"</b> (",sizeof h); strcat0(h,kb,sizeof h); strcat0(h," KB) to the <b>Downloads</b> folder.</p>",sizeof h);
    if(f<0) strncpy0(h,"<html><body><h1>Download failed</h1><p>The disk is full or the name is taken.</p>",sizeof h);
    if(px && f>=0){ strcat0(h,"<p><img src=\"file:///Downloads/",sizeof h); strcat0(h,name,sizeof h); strcat0(h,"\"></p>",sizeof h); }
    strcat0(h,"<p>Open the <a href=\"app:files\">Files</a> app and choose <b>Downloads</b> to find it. Remember: files are kept in memory and disappear when you restart.</p></body></html>",sizeof h);
    show_html(w,b,h,(u32)strlen(h));
    if(px && f>=0) load_images(w,b);
}
static void push_hist(web_t *b,const char *u){
    if(b->nhist && !strcmp(b->hist[b->nhist-1],u)) return;
    if(b->nhist==24){ for(int i=1;i<24;i++) strncpy0(b->hist[i-1],b->hist[i],600); b->nhist--; }
    strncpy0(b->hist[b->nhist++],u,600);
}
static void go(window_t *w,web_t *b,const char *in,int add_hist){
    char url[600]; char tmp[600];
    while(*in==' ') in++;
    /* turn what was typed into a URL */
    if(!strncmp(in,"app:",4)){ const char *a=in+4;
        if(!strcmp(a,"mines")) app_mines(); else if(!strcmp(a,"solitaire")) app_solitaire(); else if(!strcmp(a,"paint")) app_paint(-1);
        else if(!strcmp(a,"music")) app_music(-1); else if(!strcmp(a,"piano")) app_piano(); else if(!strcmp(a,"snake")) app_snake();
        else if(!strcmp(a,"files")) app_files(fs_lookup("/Downloads"));
        return; }
    if(!strncmp(in,"cam:",4)||!strncmp(in,"http://",7)||!strncmp(in,"https://",8)||!strncmp(in,"file:",5)) strncpy0(url,in,sizeof url);
    else {
        int has_dot=0, has_space=0; for(const char *p=in;*p;p++){ if(*p=='.') has_dot=1; if(*p==' ') has_space=1; }
        if(has_dot && !has_space){ strncpy0(url,"http://",sizeof url); strcat0(url,in,sizeof url); }
        else { url_encode(in,tmp,sizeof tmp); strncpy0(url,FROGFIND "/?q=",sizeof url); strcat0(url,tmp,sizeof url); }
    }
    if(!strncmp(url,"https://",8)){ /* fetch HTTPS sites through FrogFind's reader */
        url_encode(url,tmp,sizeof tmp); strncpy0(url,FROGFIND "/read.php?a=",sizeof url); strcat0(url,tmp,sizeof url);
    }
    if(add_hist && b->pg.url[0]) push_hist(b,b->pg.url);
    strncpy0(b->pg.url,url,sizeof b->pg.url); strncpy0(b->addr,url,sizeof b->addr); b->addr_focus=0;
    if(!strncmp(url,"cam:",4)){
        const char *nm=url+4; while(*nm=='/') nm++;
        char fn[64]; strncpy0(fn,*nm?nm:"home",sizeof fn); if(!ends_with(fn,".htm")) strcat0(fn,".htm",sizeof fn);
        int pages=fs_lookup("/Pages"); int f=pages>=0?fs_find(pages,fn):-1;
        if(f<0||!fs_nodes[f].data){ error_page(w,b,"Page not found",url); return; }
        show_html(w,b,fs_nodes[f].data,fs_nodes[f].size); set_status(b,"Done");
        if(b->pg.nimg) load_images(w,b);
        return;
    }
    if(!strncmp(url,"file:",5)){
        const char *path=url+5; while(path[0]=='/'&&path[1]=='/') path++;
        int f=fs_lookup(path);
        if(f<0||!fs_nodes[f].data){ error_page(w,b,"File not found",path); return; }
        if(fs_nodes[f].type==FT_IMAGE){ char h[300]; strncpy0(h,"<html><body><center><img src=\"",sizeof h); strcat0(h,url,sizeof h); strcat0(h,"\"></center></body></html>",sizeof h); show_html(w,b,h,(u32)strlen(h)); load_images(w,b); return; }
        if(fs_nodes[f].type==FT_PAGE){ show_html(w,b,fs_nodes[f].data,fs_nodes[f].size); if(b->pg.nimg) load_images(w,b); }
        else { u32 n=fs_nodes[f].size; char *h=kmalloc(n*2+64); if(!h) return; strncpy0(h,"<pre>",16); u32 k=5;
            for(u32 i=0;i<n;i++){ char c=fs_nodes[f].data[i]; if(c=='<'){ memcpy(h+k,"&lt;",4); k+=4; } else if(c=='&'){ memcpy(h+k,"&amp;",5); k+=5; } else h[k++]=c; }
            memcpy(h+k,"</pre>",7); show_html(w,b,h,k+6); kfree(h); }
        set_status(b,"Done"); return;
    }
    b->loading=1; loading_win=w;
    char *body=0; u32 blen=0; char loc[600]; char ctype[64]; int st=0;
    for(int redirects=0;redirects<6;redirects++){
        st=http_get(url,&body,&blen,loc,sizeof loc,ctype,sizeof ctype,load_status);
        if(st==HTTP_ERR_HTTPS){ url_encode(url,tmp,sizeof tmp); strncpy0(url,FROGFIND "/read.php?a=",sizeof url); strcat0(url,tmp,sizeof url); continue; }
        if((st==301||st==302||st==303||st==307||st==308) && loc[0]){
            kfree(body); body=0;
            url_resolve(url,loc,tmp,sizeof tmp);
            if(!strncmp(tmp,"https://",8)){ char e[600]; url_encode(tmp,e,sizeof e); strncpy0(url,FROGFIND "/read.php?a=",sizeof url); strcat0(url,e,sizeof url); }
            else strncpy0(url,tmp,sizeof url);
            continue;
        }
        break;
    }
    b->loading=0; loading_win=0;
    strncpy0(b->pg.url,url,sizeof b->pg.url); strncpy0(b->addr,url,sizeof b->addr);
    if(st<0){ error_page(w,b,http_err_str(st),url); set_status(b,"Error"); return; }
    if(!body){ error_page(w,b,"The site sent an empty answer.",url); return; }
    if(st==200 && ctype[0] && strncmp(ctype,"text/",5) && strncmp(ctype,"application/xhtml",17)){
        save_download(w,b,url,body,blen,ctype); kfree(body); return;
    }
    /* plain text? */
    int is_html=0; for(u32 i=0;i<blen && i<2048;i++) if(body[i]=='<'){ is_html=1; break; }
    if(is_html) show_html(w,b,body,blen);
    else { char *h=kmalloc(blen*2+32); if(h){ strncpy0(h,"<pre>",8); u32 k=5; for(u32 i=0;i<blen;i++){ char c=body[i]; if(c=='<'){ memcpy(h+k,"&lt;",4); k+=4; } else if(c=='&'){ memcpy(h+k,"&amp;",5); k+=5; } else h[k++]=c; } memcpy(h+k,"</pre>",7); show_html(w,b,h,k+6); kfree(h); } }
    kfree(body);
    if(b->pg.nimg) load_images(w,b);
    char s[64]; strncpy0(s,"Done",sizeof s); if(st!=200){ char n[8]; itoa10(st,n); strcat0(s," (HTTP ",sizeof s); strcat0(s,n,sizeof s); strcat0(s,")",sizeof s); }
    set_status(b,s);
}
static void submit(window_t *w,web_t *b,int form){
    page_t *pg=&b->pg; char q[600]; q[0]=0; char e[400];
    for(int i=0;i<pg->ninp;i++){
        input_t *in=&pg->inp[i]; if(in->form!=form || !pg->arena[in->name]) continue;
        if(in->type==2) continue;
        if(q[0]) strcat0(q,"&",sizeof q);
        url_encode(pg->arena+in->name,e,sizeof e); strcat0(q,e,sizeof q); strcat0(q,"=",sizeof q);
        url_encode(in->value,e,sizeof e); strcat0(q,e,sizeof q);
    }
    char base[600]; const char *act = form>=0 ? pg->arena+pg->forms[form].action : "";
    url_resolve(pg->url,act,base,sizeof base);
    char *qm=base; while(*qm && *qm!='?') qm++; *qm=0;
    strcat0(base,"?",sizeof base); strcat0(base,q,sizeof base);
    go(w,b,base,1);
}

/* ---------------- drawing ---------------- */
static int content_y(void){ return TOOL_H; }
static void relayout(web_t *b);
static void web_draw(window_t *w){
    web_t *b=w->st; page_t *pg=&b->pg; int cw=win_cw(w), ch=win_ch(w);
    if(pg->src && pg->width!=cw-12){ pg->width=cw-12; relayout(b); b->scroll=imin(b->scroll,imax(0,pg->height-(ch-TOOL_H-STATUS_H))); }
    int vy=content_y(), vh=ch-TOOL_H-STATUS_H;
    fill_rect(0,vy,cw,vh,pg->bg);
    gfx_set_clip(gfx_ox,gfx_oy+vy,cw-12,vh);
    for(int i=0;i<pg->nit;i++){
        item_t *t=&pg->it[i]; int y=vy+t->y-b->scroll;
        if(y+t->h<vy || y>vy+vh) continue;
        u32 col = (t->style&ST_LINK) ? (t->link==b->hover_link?RGB(220,40,120):pg->linkc) : (t->style&ST_GRAY)?mix(pg->fg,pg->bg,110):(t->style&ST_ITAL)?mix(pg->fg,RGB(150,40,40),90):(t->style&ST_PRE)?mix(pg->fg,RGB(0,100,40),80):pg->fg;
        if(t->style&ST_HEAD && !(t->style&ST_LINK)) col=mix(pg->fg,accent_dark(),150);
        switch(t->kind){
        case KI_TEXT:{
            char s[260]; int n=imin(t->n,259); memcpy(s,pg->arena+t->off,n); s[n]=0;
            if(t->scale>1) draw_text_big(t->x,y,s,col,t->scale);
            else if(t->style&ST_BOLD) draw_text_bold(t->x,y,s,col); else draw_text(t->x,y,s,col);
            if(t->style&(ST_LINK|ST_UND)){
                hline(t->x,y+14*t->scale+1,t->w,col);
                /* underline the space before the next word of the same link */
                if(i+1<pg->nit && pg->it[i+1].link==t->link && t->link>=0 && pg->it[i+1].y==t->y && pg->it[i+1].x>t->x) hline(t->x+t->w,y+15,pg->it[i+1].x-t->x-t->w,col);
            }
            break;}
        case KI_HR: hline(t->x,y,t->w,mix(pg->bg,0,90)); hline(t->x,y+1,t->w,mix(pg->bg,0xFFFFFF,120)); break;
        case KI_BULLET: fill_circle(t->x+3,y+3,3,pg->fg); break;
        case KI_INPUT:{
            input_t *in=&pg->inp[t->inp];
            fill_rect(t->x,y,t->w,t->h,RGB(255,255,255)); draw_rect(t->x,y,t->w,t->h,b->focus_inp==t->inp?accent_dark():RGB(130,130,145));
            char v[170]; strncpy0(v,in->value,sizeof v); if(in->type==3) for(char *p=v;*p;p++) *p='*';
            int maxc=(t->w-10)/8; int L=(int)strlen(v); const char *shown=v+(L>maxc?L-maxc:0);
            draw_text(t->x+5,y+3,shown,RGB(20,20,30));
            if(b->focus_inp==t->inp && (ticks/500)%2==0) vline(t->x+5+(int)strlen(shown)*8,y+3,16,RGB(20,20,30));
            break;}
        case KI_IMG:{
            webimg_t *im=&pg->img[t->inp];
            if(im->state==1 && im->px && im->w==t->w) blit_key(t->x,y,t->w,t->h,im->px,im->w);
            else { fill_rect(t->x,y,t->w,t->h,mix(pg->bg,RGB(128,128,140),40)); draw_rect(t->x,y,t->w,t->h,mix(pg->bg,0,100));
                   if(t->w>40 && t->h>20) draw_text(t->x+4,y+4,im->alt[0]?im->alt:"image",mix(pg->fg,pg->bg,100)); }
            if(t->link>=0 && t->link==b->hover_link) draw_rect(t->x,y,t->w,t->h,RGB(220,40,120));
            break;}
        case KI_BUTTON:{ input_t *in=&pg->inp[t->inp]; ui_button(t->x,y,t->w,t->h,in->value[0]?in->value:"Submit",0); break; }
        }
    }
    gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
    /* scrollbar */
    if(pg->height>vh){
        int th=imax(30,vh*vh/pg->height), ty=vy+(vh-th)*b->scroll/imax(1,pg->height-vh);
        fill_rect(cw-12,vy,12,vh,RGB(235,235,240)); fill_round_rect(cw-10,ty,8,th,4,RGB(170,165,190));
    }
    /* toolbar */
    gradient_v(0,0,cw,TOOL_H,RGB(240,238,246),RGB(222,220,232)); hline(0,TOOL_H-1,cw,RGB(190,186,204));
    ui_button(6,8,34,28,"<",0); ui_button(44,8,34,28,"R",0); ui_button(82,8,50,28,"Home",0); ui_button(136,8,50,28,"Save",0);
    int ax=194, aw=cw-ax-54;
    fill_round_rect(ax,8,aw,28,6,RGB(255,255,255)); draw_rect(ax,8,aw,28,b->addr_focus?accent_dark():RGB(180,176,196));
    gfx_set_clip(gfx_ox+ax+4,gfx_oy+8,aw-8,28);
    int L=(int)strlen(b->addr), maxc=(aw-16)/8; const char *shown=b->addr+(b->addr_focus && L>maxc?L-maxc:0);
    if(b->addr_focus && b->addr_fresh) fill_rect(ax+6,13,imin(aw-12,(int)strlen(shown)*8),18,RGB(200,190,240));
    draw_text(ax+8,14,shown,RGB(30,30,45));
    if(b->addr_focus && !b->addr_fresh && (ticks/500)%2==0) vline(ax+8+(int)strlen(shown)*8,13,18,RGB(30,30,45));
    gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
    ui_button(cw-48,8,42,28,"Go",0);
    /* spinning "globe" while loading */
    /* status bar */
    fill_rect(0,ch-STATUS_H,cw,STATUS_H,RGB(236,234,242)); hline(0,ch-STATUS_H,cw,RGB(200,196,214));
    const char *st=b->status;
    if(b->hover_link>=0 && b->hover_link<pg->nlinks && !b->loading) st=pg->arena+pg->links[b->hover_link];
    gfx_set_clip(gfx_ox,gfx_oy+ch-STATUS_H,cw-130,STATUS_H); draw_text(8,ch-STATUS_H+3,st,RGB(60,60,80)); gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
    char ns[40]; if(!net.present) strncpy0(ns,"No network card",sizeof ns); else if(!net.link) strncpy0(ns,"Cable unplugged",sizeof ns); else if(net.dhcp!=3) strncpy0(ns,"Connecting...",sizeof ns); else strncpy0(ns,"Online",sizeof ns);
    fill_circle(cw-text_width(ns)-22,ch-STATUS_H+11,4,net.dhcp==3?RGB(60,180,80):RGB(220,140,40));
    draw_text(cw-text_width(ns)-12,ch-STATUS_H+3,ns,RGB(60,60,80));
}

/* ---------------- input ---------------- */
static item_t *item_at(web_t *b,int x,int y){
    page_t *pg=&b->pg; int py=y-content_y()+b->scroll;
    for(int i=0;i<pg->nit;i++){ item_t *t=&pg->it[i]; if(hit(x,py,t->x-2,t->y-2,t->w+4,t->h+4) && (t->link>=0||t->kind==KI_INPUT||t->kind==KI_BUTTON)) return t; }
    return 0;
}
static void web_mouse(window_t *w,int ev,int x,int y,int bt){
    web_t *b=w->st; page_t *pg=&b->pg; int cw=win_cw(w), ch=win_ch(w), vh=ch-TOOL_H-STATUS_H;
    if(b->loading) return;
    if(ev==EV_WHEEL){ b->scroll=clampi(b->scroll+bt*48,0,imax(0,pg->height-vh)); return; }
    if(ev==EV_MOVE){
        int old=b->hover_link; b->hover_link=-1;
        if(y>=content_y() && y<ch-STATUS_H){ item_t *t=item_at(b,x,y); if(t && t->link>=0) b->hover_link=t->link; }
        if(old!=b->hover_link) wm_invalidate();
        return;
    }
    if(ev!=EV_DOWN) return;
    if(y<TOOL_H){
        b->focus_inp=-1;
        if(hit(x,y,6,8,34,28)){ if(b->nhist){ b->nhist--; char u[600]; strncpy0(u,b->hist[b->nhist],600); go(w,b,u,0); } return; }
        if(hit(x,y,44,8,34,28)){ char u[600]; strncpy0(u,pg->url,600); go(w,b,u,0); return; }
        if(hit(x,y,82,8,50,28)){ go(w,b,"cam://home",1); return; }
        if(hit(x,y,cw-48,8,42,28)){ char u[600]; strncpy0(u,b->addr,600); go(w,b,u,1); return; }
        if(hit(x,y,136,8,50,28)){
            int dl=fs_lookup("/Downloads"); if(dl<0) dl=fs_create(0,"Downloads",FT_DIR);
            if(!pg->src){ return; }
            char name[FS_NAME]; strncpy0(name,pg->title[0]?pg->title:"page",24);
            for(char *q=name;*q;q++) if(*q=='/'||*q==':'||*q=='\\') *q='_';
            strcat0(name,".htm",FS_NAME); unique_in(dl,name);
            int f=fs_create(dl,name,FT_PAGE);
            if(f>=0){ char *c=kmalloc(pg->srclen+1); if(c){ memcpy(c,pg->src,pg->srclen+1); fs_nodes[f].data=c; fs_nodes[f].size=pg->srclen; fs_touch(); }
                char m[96]; strncpy0(m,"Saved page to /Downloads/",sizeof m); strcat0(m,name,sizeof m); wm_toast(m); }
            return;
        }
        if(hit(x,y,194,8,cw-194-54,28)){ b->addr_focus=1; b->addr_fresh=1; return; }
        return;
    }
    b->addr_focus=0;
    if(x>=cw-12 && pg->height>vh){ b->scroll=clampi((y-content_y())*pg->height/vh - vh/2,0,pg->height-vh); return; }
    item_t *t=item_at(b,x,y);
    b->focus_inp=-1;
    if(!t) return;
    if(t->kind==KI_INPUT){ b->focus_inp=t->inp; return; }
    if(t->kind==KI_BUTTON){ submit(w,b,pg->inp[t->inp].form); return; }
    if(t->link>=0){ char u[600]; url_resolve(pg->url,pg->arena+pg->links[t->link],u,sizeof u); go(w,b,u,1); }
}
static void edit(char *s,int n,int k){
    int L=(int)strlen(s);
    if(k=='\b'){ if(L) s[L-1]=0; }
    else if(k>=32 && k<127 && L<n-1){ s[L]=(char)k; s[L+1]=0; }
}
static void copy_page_text(web_t *b){
    page_t *pg=&b->pg; u32 cap=pg->alen+pg->nit*2+16; char *o=kmalloc(cap); if(!o) return; u32 k=0; int lasty=-1;
    for(int i=0;i<pg->nit && k+300<cap;i++){ item_t *t=&pg->it[i]; if(t->kind!=KI_TEXT) continue;
        if(lasty>=0){ o[k++] = t->y!=lasty ? '\n' : ' '; } lasty=t->y; memcpy(o+k,pg->arena+t->off,t->n); k+=t->n; }
    clip_set(o,(int)k); kfree(o); wm_toast("Copied the page text");
}
static void web_key(window_t *w,int k){
    web_t *b=w->st; page_t *pg=&b->pg; int vh=win_ch(w)-TOOL_H-STATUS_H;
    if(b->loading) return;
    if(k==KEY_CTRL('l')){ b->addr_focus=1; b->addr_fresh=1; return; }
    if(k==KEY_CTRL('c')){ if(b->addr_focus) { clip_set(b->addr,-1); wm_toast("Copied the address"); } else if(b->focus_inp>=0) clip_set(pg->inp[b->focus_inp].value,-1); else copy_page_text(b); return; }
    if(k==KEY_CTRL('v')){
        char *dst=b->addr_focus?b->addr:(b->focus_inp>=0?pg->inp[b->focus_inp].value:0); int cap=b->addr_focus?(int)sizeof b->addr:160;
        if(!dst) return; if(b->addr_focus && b->addr_fresh){ dst[0]=0; b->addr_fresh=0; }
        int L=(int)strlen(dst); for(const char *c=clip_get();*c && L<cap-1;c++) if(*c>=32 && *c<127) dst[L++]=*c; dst[L]=0; return; }
    if(b->addr_focus){
        if(k=='\n'){ char u[600]; strncpy0(u,b->addr,600); go(w,b,u,1); return; }
        if(k==KEY_ESC){ b->addr_focus=0; strncpy0(b->addr,pg->url,sizeof b->addr); return; }
        if(b->addr_fresh && (k=='\b'||(k>=32&&k<127))){ b->addr[0]=0; b->addr_fresh=0; if(k=='\b') return; }
        b->addr_fresh=0; edit(b->addr,sizeof b->addr,k); return;
    }
    if(b->focus_inp>=0){
        input_t *in=&pg->inp[b->focus_inp];
        if(k=='\n'){ submit(w,b,in->form); return; }
        if(k==KEY_ESC){ b->focus_inp=-1; return; }
        edit(in->value,sizeof in->value,k); return;
    }
    int mx=imax(0,pg->height-vh);
    if(k==KEY_DOWN) b->scroll=imin(mx,b->scroll+40);
    else if(k==KEY_UP) b->scroll=imax(0,b->scroll-40);
    else if(k==' ') b->scroll=imin(mx,b->scroll+vh-40);
    else if(k==KEY_HOME) b->scroll=0;
    else if(k==KEY_END) b->scroll=mx;
    else if(k=='\b'){ if(b->nhist){ b->nhist--; char u[600]; strncpy0(u,b->hist[b->nhist],600); go(w,b,u,0); } }
    else if(k>=32 && k<127){ b->addr_focus=1; b->addr_fresh=0; b->addr[0]=(char)k; b->addr[1]=0; }
}
static void web_tick(window_t *w){ web_t *b=w->st; static u32 last; if((b->focus_inp>=0||b->addr_focus) && ticks-last>500){ last=ticks; wm_invalidate(); } }
static void web_close(window_t *w){ web_t *b=w->st; free_page(&b->pg); }
void app_web(const char *url){
    window_t *w=wm_open("Web",imin(760,scr_w-60),imin(600,scr_h-90),IC_BROWSER); if(!w) return;
    web_t *b=kzalloc(sizeof *b); b->focus_inp=-1; b->hover_link=-1;
    w->st=b; w->draw=web_draw; w->mouse=web_mouse; w->key=web_key; w->tick=web_tick; w->close=web_close;
    w->resizable=1; w->min_w=420; w->min_h=240;
    go(w,b,url?url:"cam://home",0);
}
