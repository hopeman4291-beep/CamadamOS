/* CamadamOS Notepad: a simple text editor with selection and clipboard */
#include "kernel.h"

#define NP_CAP 65536
#define NP_BAR 40
typedef struct { char *t; int len, cur, top, file, dirty, sel, selecting; char name[FS_NAME]; } np_t;

static int line_start(np_t *n,int pos){ while(pos>0 && n->t[pos-1]!='\n') pos--; return pos; }
static int line_end(np_t *n,int pos){ while(pos<n->len && n->t[pos]!='\n') pos++; return pos; }
static int line_of(np_t *n,int pos){ int l=0; for(int i=0;i<pos;i++) if(n->t[i]=='\n') l++; return l; }
static int pos_of_line(np_t *n,int line){ int p=0; while(line>0 && p<n->len){ if(n->t[p]=='\n') line--; p++; } return p; }
static int sel_a(np_t *n){ return n->sel<0?n->cur:imin(n->sel,n->cur); }
static int sel_b(np_t *n){ return n->sel<0?n->cur:imax(n->sel,n->cur); }
static int has_sel(np_t *n){ return n->sel>=0 && n->sel!=n->cur; }
static void set_title(window_t *w,np_t *n){
    char t[64]; strncpy0(t,"Notepad - ",sizeof t); strcat0(t,n->name[0]?n->name:"Untitled",sizeof t); if(n->dirty) strcat0(t," *",sizeof t);
    wm_set_title(w,t);
}
static void np_draw(window_t *w){
    np_t *n=w->st; int cw=win_cw(w), ch=win_ch(w);
    fill_rect(0,0,cw,NP_BAR,RGB(238,236,246)); hline(0,NP_BAR-1,cw,RGB(200,196,214));
    ui_button(8,6,56,28,"New",0); ui_button(70,6,56,28,"Save",0); ui_button(132,6,84,28,"Save as",0);
    ui_button(222,6,56,28,"Copy",0); ui_button(284,6,60,28,"Paste",0);
    char info[48]; int ln=line_of(n,n->cur)+1, col=n->cur-line_start(n,n->cur)+1; char a[12];
    strncpy0(info,"Line ",sizeof info); itoa10(ln,a); strcat0(info,a,sizeof info); strcat0(info,", col ",sizeof info); itoa10(col,a); strcat0(info,a,sizeof info);
    if(cw>520) draw_text(cw-text_width(info)-12,13,info,RGB(100,100,120));
    fill_rect(0,NP_BAR,cw,ch-NP_BAR,RGB(255,255,252));
    fill_rect(0,NP_BAR,36,ch-NP_BAR,RGB(244,242,236));
    int rows=(ch-NP_BAR-8)/18, cl=line_of(n,n->cur);
    if(cl<n->top) n->top=cl; if(cl>=n->top+rows) n->top=cl-rows+1;
    int p=pos_of_line(n,n->top), sa=sel_a(n), sb=sel_b(n);
    gfx_set_clip(gfx_ox,gfx_oy+NP_BAR,cw,ch-NP_BAR);
    for(int r=0;r<rows && p<=n->len;r++){
        int y=NP_BAR+6+r*18; char num[8]; itoa10(n->top+r+1,num); draw_text(30-text_width(num),y,num,RGB(170,165,150));
        int e=line_end(n,p);
        if(has_sel(n) && sb>p && sa<=e){ int x0=imax(sa,p)-p, x1=imin(sb,e)-p; if(sb>e) x1++; fill_rect(44+x0*8,y-1,imax(2,(x1-x0)*8),18,mix(accent_light(),0xFFFFFF,150)); }
        char buf[260]; int k=imin(e-p,(cw-50)/8); if(k>259) k=259; memcpy(buf,n->t+p,k); buf[k]=0;
        for(int i=0;i<k;i++) if(buf[i]=='\t') buf[i]=' ';
        draw_text(44,y,buf,RGB(25,25,35));
        if(n->cur>=p && n->cur<=e && (ticks/500)%2==0) fill_rect(44+(n->cur-p)*8,y,2,16,accent_dark());
        if(e>=n->len) break;
        p=e+1;
    }
    gfx_set_clip(gfx_ox,gfx_oy,cw,ch);
}
static void delete_sel(np_t *n){
    if(!has_sel(n)){ n->sel=-1; return; }
    int a=sel_a(n), b=sel_b(n);
    memmove(n->t+a,n->t+b,n->len-b); n->len-=b-a; n->t[n->len]=0; n->cur=a; n->sel=-1; n->dirty=1;
}
static void insert(np_t *n,const char *s,int k){
    delete_sel(n);
    if(n->len+k>=NP_CAP) k=NP_CAP-1-n->len;
    if(k<=0) return;
    memmove(n->t+n->cur+k,n->t+n->cur,n->len-n->cur); memcpy(n->t+n->cur,s,k); n->len+=k; n->cur+=k; n->t[n->len]=0; n->dirty=1;
}
static void copy(np_t *n){
    if(has_sel(n)){ clip_set(n->t+sel_a(n),sel_b(n)-sel_a(n)); wm_toast("Copied"); }
    else { int a=line_start(n,n->cur), b=line_end(n,n->cur); clip_set(n->t+a,b-a); wm_toast("Copied this line"); }
}
static void paste(np_t *n){
    const char *c=clip_get(); int k=(int)strlen(c);
    char *tmp=kmalloc(k+1); if(!tmp) return; int m=0;
    for(int i=0;i<k;i++) if(c[i]!='\r') tmp[m++]=c[i];
    insert(n,tmp,m); kfree(tmp);
}
static void save(window_t *w,np_t *n,int as_new){
    int dir;
    int is_bas=ends_with(n->name,".bas");
    dir=fs_lookup(is_bas?"/Programs":"/Documents"); if(dir<0) dir=fs_create(0,is_bas?"Programs":"Documents",FT_DIR);
    int f=n->file;
    if(as_new || f<0 || !fs_nodes[f].used){
        char nm[FS_NAME]; fs_unique_name(dir,"Note",".txt",nm);
        f=fs_create(dir,nm,FT_TEXT); if(f<0){ wm_message("Notepad","Could not save: the disk is full."); return; }
        n->file=f; strncpy0(n->name,nm,FS_NAME);
    }
    char *d=kmalloc(n->len+1); if(!d) return; memcpy(d,n->t,n->len); d[n->len]=0;
    if(!fs_nodes[f].readonly) kfree(fs_nodes[f].data);
    fs_nodes[f].data=d; fs_nodes[f].size=n->len; fs_nodes[f].readonly=0; fs_touch();
    n->dirty=0; set_title(w,n);
    char p[96], m[160]; fs_path(f,p,sizeof p); strncpy0(m,"Saved as ",sizeof m); strcat0(m,p,sizeof m); wm_toast(m);
}
static int pos_at(window_t *w,np_t *n,int x,int y){
    (void)w; int line=n->top+(y-NP_BAR-6)/18; if(line<0) line=0;
    int p=pos_of_line(n,line); int e=line_end(n,p);
    int col=imax(0,(x-44+4)/8); return imin(p+col,e);
}
static void np_mouse(window_t *w,int ev,int x,int y,int b){
    np_t *n=w->st;
    if(ev==EV_WHEEL){ n->top=imax(0,n->top+b*3); return; }
    if(ev==EV_MOVE){ if(n->selecting && b){ n->cur=pos_at(w,n,x,y); } return; }
    if(ev==EV_UP){ n->selecting=0; if(n->sel==n->cur) n->sel=-1; return; }
    if(ev==EV_DBL && y>NP_BAR){ /* select word */
        int p=n->cur, a=p, e=p; while(a>0 && n->t[a-1]!=' ' && n->t[a-1]!='\n') a--; while(e<n->len && n->t[e]!=' ' && n->t[e]!='\n') e++;
        n->sel=a; n->cur=e; n->selecting=0; return;
    }
    if(ev!=EV_DOWN) return;
    if(hit(x,y,8,6,56,28)){ n->len=0; n->cur=0; n->t[0]=0; n->file=-1; n->name[0]=0; n->dirty=0; n->top=0; n->sel=-1; set_title(w,n); return; }
    if(hit(x,y,70,6,56,28)){ save(w,n,0); return; }
    if(hit(x,y,132,6,84,28)){ save(w,n,1); return; }
    if(hit(x,y,222,6,56,28)){ copy(n); return; }
    if(hit(x,y,284,6,60,28)){ paste(n); set_title(w,n); return; }
    if(y>NP_BAR){ n->cur=pos_at(w,n,x,y); n->sel=n->cur; n->selecting=1; }
}
static void np_key(window_t *w,int k){
    np_t *n=w->st;
    if(k==KEY_CTRL('a')){ n->sel=0; n->cur=n->len; return; }
    if(k==KEY_CTRL('c')){ copy(n); return; }
    if(k==KEY_CTRL('x')){ if(has_sel(n)){ copy(n); delete_sel(n); set_title(w,n); } return; }
    if(k==KEY_CTRL('v')){ paste(n); set_title(w,n); return; }
    if(k==KEY_CTRL('s')){ save(w,n,0); return; }
    if(k==KEY_LEFT){ if(n->cur>0) n->cur--; n->sel=-1; }
    else if(k==KEY_RIGHT){ if(n->cur<n->len) n->cur++; n->sel=-1; }
    else if(k==KEY_UP||k==KEY_DOWN){
        n->sel=-1;
        int s=line_start(n,n->cur), col=n->cur-s;
        if(k==KEY_UP){ if(s==0) return; int ps=line_start(n,s-1); n->cur=imin(ps+col,s-1); }
        else { int e=line_end(n,n->cur); if(e>=n->len) return; int ne=line_end(n,e+1); n->cur=imin(e+1+col,ne); }
    }
    else if(k==KEY_HOME){ n->cur=line_start(n,n->cur); n->sel=-1; }
    else if(k==KEY_END){ n->cur=line_end(n,n->cur); n->sel=-1; }
    else if(k=='\b'){ if(has_sel(n)) delete_sel(n); else if(n->cur>0){ memmove(n->t+n->cur-1,n->t+n->cur,n->len-n->cur); n->len--; n->cur--; n->t[n->len]=0; n->dirty=1; } }
    else if(k==KEY_DEL){ if(has_sel(n)) delete_sel(n); else if(n->cur<n->len){ memmove(n->t+n->cur,n->t+n->cur+1,n->len-n->cur-1); n->len--; n->t[n->len]=0; n->dirty=1; } }
    else if(k=='\t') insert(n,"    ",4);
    else if(k=='\n'||(k>=32&&k<127)){ char c=(char)k; insert(n,&c,1); }
    else return;
    set_title(w,n);
}
static void np_tick(window_t *w){ static u32 last; (void)w; if(ticks-last>500){ last=ticks; wm_invalidate(); } }
static void np_close(window_t *w){ np_t *n=w->st; kfree(n->t); }
void app_notepad(int file){
    window_t *w=wm_open("Notepad",640,440,IC_NOTEPAD); if(!w) return;
    np_t *n=kzalloc(sizeof *n); n->t=kmalloc(NP_CAP+1); n->file=-1; n->sel=-1;
    if(!n->t){ kfree(n); wm_close(w); return; }
    n->t[0]=0;
    if(file>=0 && fs_nodes[file].data){
        u32 sz=imin((int)fs_nodes[file].size,NP_CAP-1); memcpy(n->t,fs_nodes[file].data,sz); n->len=sz; n->t[sz]=0;
        n->file=file; strncpy0(n->name,fs_nodes[file].name,FS_NAME);
    }
    w->st=n; w->draw=np_draw; w->mouse=np_mouse; w->key=np_key; w->tick=np_tick; w->close=np_close;
    w->resizable=1; w->min_w=380; w->min_h=200;
    set_title(w,n);
}
