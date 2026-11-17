/* CamadamOS Terminal: a small command shell */
#include "kernel.h"

#define TL 400
#define TC 100
typedef struct { char lines[TL][TC]; int n; char in[TC]; int cwd; int scroll; char hist[16][TC]; int nh, hpos; int color; } term_t;
static const u32 tcols[4][2]={{RGB(12,16,12),RGB(90,240,120)},{RGB(18,12,6),RGB(255,190,70)},{RGB(14,14,22),RGB(225,225,235)},{RGB(6,20,30),RGB(100,220,255)}};
static window_t *tw_cur;

static int cols(window_t *w){ return imin(TC-1,(win_cw(w)-16)/8); }
static void newline_t(term_t *t){
    if(t->n==TL){ memmove(t->lines[0],t->lines[1],(TL-1)*TC); t->n--; }
    t->lines[t->n][0]=0; t->n++;
}
static void tputs(window_t *w,const char *s){
    term_t *t=w->st; int c=cols(w);
    if(!t->n) newline_t(t);
    for(;*s;s++){
        if(*s=='\n'){ newline_t(t); continue; }
        char *l=t->lines[t->n-1]; int L=(int)strlen(l);
        if(L>=c){ newline_t(t); l=t->lines[t->n-1]; L=0; }
        l[L]=*s; l[L+1]=0;
    }
    t->scroll=0;
}
static void tprintln(window_t *w,const char *s){ tputs(w,s); tputs(w,"\n"); }
static void flush_screen(void){ wm_invalidate(); wm_pump(); }

static int resolve(term_t *t,const char *p){
    if(!*p) return t->cwd;
    int cur = p[0]=='/' ? 0 : t->cwd;
    char part[FS_NAME];
    while(*p){
        while(*p=='/') p++;
        if(!*p) break;
        int k=0; while(*p && *p!='/' && k<FS_NAME-1) part[k++]=*p++; part[k]=0;
        if(!strcmp(part,".")) continue;
        if(!strcmp(part,"..")){ cur=fs_nodes[cur].parent; continue; }
        cur=fs_find(cur,part); if(cur<0) return -1;
    }
    return cur;
}
/* split "a/b/name" into the folder node and the final name */
static int split_path(term_t *t,const char *path,char *name){
    const char *slash=strrchr0(path,'/');
    if(!slash){ strncpy0(name,path,FS_NAME); return t->cwd; }
    char dir[128]; int n=(int)(slash-path); if(n>=127) n=127; memcpy(dir,path,n); dir[n]=0;
    strncpy0(name,slash+1,FS_NAME);
    int d = n==0 ? 0 : resolve(t,dir);
    return (d>=0 && fs_nodes[d].type==FT_DIR) ? d : -1;
}
static void prompt(window_t *w){
    term_t *t=w->st; char p[80]; fs_path(t->cwd,p,sizeof p);
    char s[100]; strncpy0(s,"camadam:",sizeof s); strcat0(s,p,sizeof s); strcat0(s,"> ",sizeof s); tputs(w,s);
}
static void cmd_ls(window_t *w,term_t *t,const char *a){
    int d=resolve(t,a); if(d<0||fs_nodes[d].type!=FT_DIR){ tprintln(w,"No such folder."); return; }
    int kids[FS_MAX]; int n=fs_children(d,kids,FS_MAX);
    for(int i=0;i<n;i++){
        fsnode_t *f=&fs_nodes[kids[i]]; char line[80];
        if(f->type==FT_DIR){ strncpy0(line,"  [DIR]  ",sizeof line); }
        else { char sz[16]; u32 s=f->size; if(f->type==FT_SONG) s=(u32)strlen(songs[f->song].notes); itoa10((int)s,sz); strncpy0(line,"  ",sizeof line); for(int k=(int)strlen(sz);k<7;k++) strcat0(line," ",sizeof line); strcat0(line,sz,sizeof line); strcat0(line,"  ",sizeof line); }
        strcat0(line,f->name,sizeof line); tprintln(w,line);
    }
    if(!n) tprintln(w,"  (empty)");
}
static void cmd_cat(window_t *w,term_t *t,const char *a){
    int f=resolve(t,a); if(f<0||fs_nodes[f].type==FT_DIR){ tprintln(w,"No such file."); return; }
    fsnode_t *n=&fs_nodes[f];
    if(n->type==FT_IMAGE){ tprintln(w,"That's a picture. Try: open <name>"); return; }
    if(n->type==FT_SONG){ tprintln(w,songs[n->song].notes); return; }
    char buf[2]; buf[1]=0; for(u32 i=0;i<n->size && i<20000;i++){ buf[0]=n->data[i]; if(buf[0]=='\r') continue; if(buf[0]=='\t') buf[0]=' '; tputs(w,buf); }
    tputs(w,"\n");
}
static void cmd_net(window_t *w){
    char s[100], ip[20];
    strncpy0(s,"Card:    ",sizeof s); strcat0(s,net.chip,sizeof s); tprintln(w,s);
    if(!net.present) return;
    const char *h="0123456789ABCDEF"; strncpy0(s,"MAC:     ",sizeof s);
    for(int i=0;i<6;i++){ char b[4]={h[net.mac[i]>>4],h[net.mac[i]&15],i<5?':':0,0}; strcat0(s,b,sizeof s); } tprintln(w,s);
    strncpy0(s,"Cable:   ",sizeof s); strcat0(s,net.link?"connected":"not connected",sizeof s); tprintln(w,s);
    if(net.dhcp!=3){ tprintln(w,"Address: waiting for DHCP..."); return; }
    ip_to_str(net.ip,ip); strncpy0(s,"Address: ",sizeof s); strcat0(s,ip,sizeof s); tprintln(w,s);
    ip_to_str(net.gw,ip); strncpy0(s,"Router:  ",sizeof s); strcat0(s,ip,sizeof s); tprintln(w,s);
    ip_to_str(net.dns,ip); strncpy0(s,"DNS:     ",sizeof s); strcat0(s,ip,sizeof s); tprintln(w,s);
}
static void cmd_ping(window_t *w,const char *a){
    if(!*a){ tprintln(w,"Usage: ping <host>"); return; }
    u32 ip; tprintln(w,"Resolving..."); flush_screen();
    if(!dns_resolve(a,&ip)){ tprintln(w,"Couldn't find that host."); return; }
    char s[80], ips[20]; ip_to_str(ip,ips);
    for(int i=0;i<4;i++){
        int ms=net_ping(ip,2000);
        if(ms<0){ strncpy0(s,"Request to ",sizeof s); strcat0(s,ips,sizeof s); strcat0(s," timed out",sizeof s); }
        else { char m[12]; itoa10(ms,m); strncpy0(s,"Reply from ",sizeof s); strcat0(s,ips,sizeof s); strcat0(s,": time=",sizeof s); strcat0(s,m,sizeof s); strcat0(s," ms",sizeof s); }
        tprintln(w,s); flush_screen();
        if(i<3) sleep_ms(500);
    }
}
static void t_status(const char *m){ (void)m; if(tw_cur) flush_screen(); }
static void cmd_get(window_t *w,const char *a){
    if(!*a){ tprintln(w,"Usage: get http://example.com/"); return; }
    char url[300]; if(strncmp(a,"http",4)){ strncpy0(url,"http://",sizeof url); strcat0(url,a,sizeof url); } else strncpy0(url,a,sizeof url);
    char *body; u32 n; char loc[300];
    tw_cur=w; int st=http_get(url,&body,&n,loc,sizeof loc,0,0,t_status); tw_cur=0;
    if(st<0){ tprintln(w,http_err_str(st)); return; }
    char s[80], b[12]; itoa10(st,b); strncpy0(s,"HTTP ",sizeof s); strcat0(s,b,sizeof s); strcat0(s,", ",sizeof s); itoa10((int)n,b); strcat0(s,b,sizeof s); strcat0(s," bytes",sizeof s); tprintln(w,s);
    if(loc[0]){ strncpy0(s,"Location: ",sizeof s); strcat0(s,loc,sizeof s); tprintln(w,s); }
    int lines=0; char c[2]={0,0};
    for(u32 i=0;i<n && lines<25;i++){ c[0]=body[i]; if(c[0]=='\r') continue; if(c[0]=='\n') lines++; if(c[0]=='\t') c[0]=' '; if((u8)c[0]<32 && c[0]!='\n') continue; tputs(w,c); }
    tputs(w,"\n"); kfree(body);
}
static void run(window_t *w,char *line){
    term_t *t=w->st;
    while(*line==' ') line++;
    char *arg=line; while(*arg && *arg!=' ') arg++;
    if(*arg){ *arg++=0; while(*arg==' ') arg++; }
    char *c=line;
    if(!*c) return;
    #define IS(x) (!strcmp(c,x))
    if(IS("help")){
        tprintln(w,"Files:   ls  cd  pwd  cat  mkdir  rm  touch  open");
        tprintln(w,"System:  date  time  uptime  mem  ver  usb  sound  disks  save  clear  reboot");
        tprintln(w,"Network: net  ping <host>  host <name>  get <url>  web [url]");
        tprintln(w,"Fun:     play <1-7>  stop  beep  camadam  apps  screenshot");
        tprintln(w,"Keys:    Ctrl+C copy all text, Ctrl+V paste, Ctrl+L clear");
        tprintln(w,"Apps:    mines solitaire paint files music piano notepad");
        tprintln(w,"         calc snake settings  (type a name to open it)");
    }
    else if(IS("clear")||IS("cls")){ t->n=0; }
    else if(IS("ls")||IS("dir")) cmd_ls(w,t,arg);
    else if(IS("cd")){ int d=resolve(t,*arg?arg:"/"); if(d<0||fs_nodes[d].type!=FT_DIR) tprintln(w,"No such folder."); else t->cwd=d; }
    else if(IS("pwd")){ char p[96]; fs_path(t->cwd,p,sizeof p); tprintln(w,p); }
    else if(IS("cat")||IS("type")) cmd_cat(w,t,arg);
    else if(IS("mkdir")||IS("touch")){
        if(!*arg){ tprintln(w,IS("mkdir")?"Usage: mkdir <name>":"Usage: touch <name.txt>"); }
        else { char nm[FS_NAME]; int d=split_path(t,arg,nm);
            if(d<0||!nm[0]) tprintln(w,"No such folder.");
            else if(fs_create(d,nm,IS("mkdir")?FT_DIR:FT_TEXT)<0) tprintln(w,"Couldn't create it (exists already?)."); } }
    else if(IS("rm")||IS("del")){ int f=resolve(t,arg); if(f<=0) tprintln(w,"No such file."); else { fs_delete(f); if(!fs_nodes[t->cwd].used) t->cwd=0; } }
    else if(IS("open")){ int f=resolve(t,arg); if(f<0) tprintln(w,"No such file."); else open_file(f); }
    else if(IS("echo")) tprintln(w,arg);
    else if(IS("date")||IS("time")){ int h,m,s,d,mo,y; rtc_time(&h,&m,&s); rtc_date(&d,&mo,&y); h=(h+clock_offset+24)%24;
        char b[40], n[8]; itoa10(d,n); strncpy0(b,n,sizeof b); strcat0(b,".",sizeof b); itoa10(mo,n); strcat0(b,n,sizeof b); strcat0(b,".",sizeof b); itoa10(y,n); strcat0(b,n,sizeof b);
        strcat0(b,"  ",sizeof b); n[0]='0'+h/10; n[1]='0'+h%10; n[2]=':'; n[3]='0'+m/10; n[4]='0'+m%10; n[5]=':'; n[6]='0'+s/10; n[7]=0; strcat0(b,n,sizeof b); char z[2]={(char)('0'+s%10),0}; strcat0(b,z,sizeof b); tprintln(w,b); }
    else if(IS("uptime")){ char b[40], n[12]; u32 s=ticks/1000; itoa10((int)(s/3600),n); strncpy0(b,n,sizeof b); strcat0(b," h ",sizeof b); itoa10((int)(s/60%60),n); strcat0(b,n,sizeof b); strcat0(b," min ",sizeof b); itoa10((int)(s%60),n); strcat0(b,n,sizeof b); strcat0(b," s",sizeof b); tprintln(w,b); }
    else if(IS("mem")){ char b[60], n[12]; itoa10((int)(mem_total_kb/1024),n); strncpy0(b,"RAM: ",sizeof b); strcat0(b,n,sizeof b); strcat0(b," MB, app memory free: ",sizeof b); itoa10((int)(heap_free_bytes()/1024),n); strcat0(b,n,sizeof b); strcat0(b," KB",sizeof b); tprintln(w,b); }
    else if(IS("ver")||IS("uname")) tprintln(w,OS_NAME " " OS_VERSION " - 32-bit x86, written from scratch in C and assembly");
    else if(IS("color")){ t->color=(t->color+1)%4; }
    else if(IS("reboot")){ music_stop(); reboot(); }
    else if(IS("net")||IS("ip")||IS("ipconfig")) cmd_net(w);
    else if(IS("usb")){ char b[80], n[12];
        strncpy0(b,"USB: ",sizeof b); strcat0(b,usb.status[0]?usb.status:"-",sizeof b); tprintln(w,b);
        strncpy0(b,"Keyboards: ",sizeof b); itoa10(usb.keyboards,n); strcat0(b,n,sizeof b); strcat0(b,"  Mice: ",sizeof b); itoa10(usb.mice,n); strcat0(b,n,sizeof b);
        strcat0(b,"  Other devices: ",sizeof b); itoa10(usb.other,n); strcat0(b,n,sizeof b); tprintln(w,b);
        strncpy0(b,"Reports received: ",sizeof b); itoa10((int)usb.reports,n); strcat0(b,n,sizeof b); tprintln(w,b);
        if(usbstick.present){ char m[16]; strncpy0(b,"Stick: ",sizeof b); strcat0(b,usbstick.vendor,sizeof b); strcat0(b," ",sizeof b); strcat0(b,usbstick.product,sizeof b);
            itoa10((int)(usbstick.sectors/2048),m); strcat0(b,"  ",sizeof b); strcat0(b,m,sizeof b); strcat0(b," MB",sizeof b); tprintln(w,b);
            strncpy0(b,"  ",sizeof b); if(fat.mounted){ strcat0(b,"Mounted: ",sizeof b); strcat0(b,fat.label,sizeof b); strcat0(b," (",sizeof b); strcat0(b,fat.status,sizeof b); strcat0(b,") - open Files > USB stick",sizeof b); }
            else strcat0(b,fat.status[0]?fat.status:usbstick.status,sizeof b); tprintln(w,b); }
        const char *hx="0123456789ABCDEF";
        { char pl[80]; strncpy0(pl,"Ports: ",sizeof pl); extern int usb_port_summary(char*,int); usb_port_summary(pl+7,(int)sizeof pl-7); tprintln(w,pl); }
        for(int i=0;i<usb.ndev;i++){ usbdev_info_t *u=&usb.dev[i]; char l[90]; strncpy0(l,"  ",sizeof l);
            char id[10]={hx[(u->vid>>12)&15],hx[(u->vid>>8)&15],hx[(u->vid>>4)&15],hx[u->vid&15],':',hx[(u->pid>>12)&15],hx[(u->pid>>8)&15],hx[(u->pid>>4)&15],hx[u->pid&15],0};
            strcat0(l,id,sizeof l);
            for(int k=0;k<u->nif;k++){ char t[16]; strncpy0(t," if",sizeof t); char c[4]; itoa10(u->ifc[k],c); strcat0(t,c,sizeof t); strcat0(t,"/",sizeof t); itoa10(u->ifs[k],c); strcat0(t,c,sizeof t); strcat0(t,"/",sizeof t); itoa10(u->ifp[k],c); strcat0(t,c,sizeof t); strcat0(l,t,sizeof l); }
            if(u->msc) strcat0(l," = USB stick",sizeof l);
            if(u->kbd) strcat0(l," = keyboard",sizeof l);
            if(u->mouse) strcat0(l,u->mouse==2?" = mouse (report)":" = mouse",sizeof l);
            if(u->gone) strcat0(l," (unplugged)",sizeof l);
            if(u->cls==9) strcat0(l," = USB hub (not supported yet)",sizeof l);
            tprintln(w,l); } }
    else if(IS("disks")){ if(!ndisks) tprintln(w,"No SATA (AHCI) disks found.");
        for(int i=0;i<ndisks;i++){ char b[80], sz[16]; disk_size_str(i,sz); strncpy0(b,"Disk ",sizeof b); char n[4]; itoa10(i+1,n); strcat0(b,n,sizeof b); strcat0(b,": ",sizeof b); strcat0(b,disks[i].model,sizeof b); strcat0(b,"  ",sizeof b); strcat0(b,sz,sizeof b); tprintln(w,b); }
        tprintln(w,store.status); }
    else if(IS("save")){ if(store_save()) tprintln(w,"Saved."); else tprintln(w,store.disk<0?"Nothing to save to: CamadamOS isn't installed on a disk.":"Saving failed."); }
    else if(IS("sound")){ tprintln(w,snd.hda?snd.codec:"PC speaker"); if(!snd.hda && snd.codec[0]) tprintln(w,snd.codec); }
    else if(IS("ping")) cmd_ping(w,arg);
    else if(IS("host")||IS("nslookup")){ u32 ip; if(!*arg) tprintln(w,"Usage: host <name>"); else if(dns_resolve(arg,&ip)){ char s[20]; ip_to_str(ip,s); tprintln(w,s); } else tprintln(w,"Not found."); }
    else if(IS("get")||IS("curl")||IS("wget")) cmd_get(w,arg);
    else if(IS("web")||IS("browser")) app_web(*arg?arg:0);
    else if(IS("play")){ int n=arg[0]-'1'; if(n>=0 && n<song_count){ music_play(n); tputs(w,"Playing: "); tprintln(w,songs[n].title); } else { for(int i=0;i<song_count;i++){ char b[60]; b[0]='1'+i; b[1]=' '; b[2]=0; strcat0(b,songs[i].title,sizeof b); tprintln(w,b); } } }
    else if(IS("stop")) music_stop();
    else if(IS("screenshot")){ screenshot(); }
    else if(IS("beep")) piano_note(880,200);
    else if(IS("camadam")){
        tprintln(w,"  ____                               _                   ");
        tprintln(w," / ___|  __ _  _ __ ___    __ _   __| |  __ _  _ __ ___  ");
        tprintln(w,"| |     / _` || '_ ` _ \\  / _` | / _` | / _` || '_ ` _ \\ ");
        tprintln(w,"| |___ | (_| || | | | | || (_| || (_| || (_| || | | | | |");
        tprintln(w," \\____| \\__,_||_| |_| |_| \\__,_| \\__,_| \\__,_||_| |_| |_|");
        tprintln(w,"   ...an operating system made from scratch, one byte at a time.");
    }
    else if(IS("apps")) tprintln(w,"mines solitaire paint files web music piano notepad calc snake settings chess tetris games basic");
    else if(IS("mines")) app_mines(); else if(IS("solitaire")) app_solitaire(); else if(IS("paint")) app_paint(-1);
    else if(IS("files")) app_files(t->cwd); else if(IS("music")) app_music(-1); else if(IS("piano")) app_piano();
    else if(IS("notepad")||IS("edit")){ int f=*arg?resolve(t,arg):-1; app_notepad(f); }
    else if(IS("calc")) app_calc(); else if(IS("snake")) app_snake(); else if(IS("settings")) app_settings();
    else if(IS("basic")){ char pth[80]; int f=*arg?resolve(t,arg):-1; if(f>=0){ fs_path(f,pth,sizeof pth); app_basic(pth); } else app_basic(0); }
    else if(IS("tetris")) app_tetris(); else if(IS("games")) app_games();
    else if(IS("chess")){
        if(!strncmp(arg,"perft",5)){ int d=atoi(arg+5); if(d<1||d>5) d=3; u32 t0=ticks; u32 n=chess_perft(d); char b[64],x[16];
            strncpy0(b,"perft ",64); itoa10(d,x); strcat0(b,x,64); strcat0(b," = ",64); itoa10(n,x); strcat0(b,x,64);
            strcat0(b,"  (",64); itoa10(ticks-t0,x); strcat0(b,x,64); strcat0(b," ms)",64); tprintln(w,b); }
        else app_chess(); }
    else { tputs(w,c); tprintln(w,": unknown command. Type 'help'."); }
    #undef IS
}
static void t_draw(window_t *w){
    term_t *t=w->st; int cw=win_cw(w), ch=win_ch(w);
    u32 bg=tcols[t->color][0], fg=tcols[t->color][1];
    fill_rect(0,0,cw,ch,bg);
    int rows=(ch-12)/17;
    int total=t->n; /* last line contains the prompt + input */
    int first=imax(0,total-rows-t->scroll);
    for(int r=0;r<rows && first+r<total;r++){
        int li=first+r; int y=6+r*17;
        draw_text(8,y,t->lines[li],fg);
        if(li==total-1){
            int x=8+(int)strlen(t->lines[li])*8;
            draw_text(x,y,t->in,RGB(255,255,255));
            if((ticks/500)%2==0) fill_rect(x+(int)strlen(t->in)*8,y+2,8,14,fg);
        }
    }
}
static void t_key(window_t *w,int k);
static void t_copy_all(window_t *w){
    term_t *t=w->st; int total=0; for(int i=0;i<t->n;i++) total+=(int)strlen(t->lines[i])+1;
    char *b=kmalloc(total+1); if(!b) return; int k=0;
    for(int i=0;i<t->n;i++){ int L=(int)strlen(t->lines[i]); memcpy(b+k,t->lines[i],L); k+=L; b[k++]='\n'; }
    clip_set(b,k); kfree(b); wm_toast("Copied the terminal text");
}
static void t_key(window_t *w,int k){
    term_t *t=w->st;
    if(k==KEY_CTRL('c')){ t_copy_all(w); return; }
    if(k==KEY_CTRL('v')){ const char *c=clip_get(); for(;*c;c++){ if(*c=='\r') continue; t_key(w,*c=='\n'?'\n':(u8)*c); if(!w->used) return; } return; }
    if(k==KEY_CTRL('l')){ t->n=0; prompt(w); return; }
    if(k=='\n'){
        char line[TC]; strncpy0(line,t->in,TC);
        tputs(w,t->in); tputs(w,"\n");
        if(line[0]){ if(t->nh==16){ memmove(t->hist[0],t->hist[1],15*TC); t->nh--; } strncpy0(t->hist[t->nh++],line,TC); }
        t->hpos=t->nh; t->in[0]=0;
        run(w,line);
        if(w->used && w->st==t) prompt(w);
        return;
    }
    if(k=='\b'){ int L=(int)strlen(t->in); if(L) t->in[L-1]=0; return; }
    if(k==KEY_UP){ if(t->hpos>0){ t->hpos--; strncpy0(t->in,t->hist[t->hpos],TC); } return; }
    if(k==KEY_DOWN){ if(t->hpos<t->nh-1){ t->hpos++; strncpy0(t->in,t->hist[t->hpos],TC); } else { t->hpos=t->nh; t->in[0]=0; } return; }
    if(k>=32 && k<127){ int L=(int)strlen(t->in); int room=cols(w)-(int)strlen(t->lines[t->n-1])-1; if(L<room && L<TC-1){ t->in[L]=(char)k; t->in[L+1]=0; } }
}
static void t_mouse(window_t *w,int ev,int x,int y,int b){ term_t *t=w->st; (void)x; (void)y; if(ev==EV_WHEEL) t->scroll=clampi(t->scroll-b*3,0,imax(0,t->n-5)); }
static void t_tick(window_t *w){ static u32 last; (void)w; if(ticks-last>500){ last=ticks; wm_invalidate(); } }
void app_terminal(void){
    window_t *w=wm_open("Terminal",680,420,IC_TERM); if(!w) return;
    term_t *t=kzalloc(sizeof *t);
    w->st=t; w->draw=t_draw; w->key=t_key; w->mouse=t_mouse; w->tick=t_tick;
    w->resizable=1; w->min_w=360; w->min_h=180;
    tprintln(w,OS_NAME " " OS_VERSION " terminal. Type 'help' to see the commands.");
    tprintln(w,"");
    prompt(w);
}
