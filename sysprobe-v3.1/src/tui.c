#define _POSIX_C_SOURCE 200809L
#include "sysprobe.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HIST 160
#define MAX_SVCS 512
#define R "\033[0m"
#define B "\033[1m"
#define DIM "\033[2m"
#define CY "\033[38;5;51m"
#define GR "\033[38;5;84m"
#define MG "\033[38;5;177m"
#define YE "\033[38;5;221m"
#define RED "\033[38;5;203m"
#define BG "\033[48;5;237m"
#define FG "\033[38;5;255m"
typedef struct {char name[150],load[20],active[20],sub[24];} Service;
typedef struct {int view,paused,sort,selected,scroll,filter_mode,confirm,help,details,sys_services,refresh;char filter[96],notice[220];long target_pid;unsigned long long target_start;char target_name[150];int target_signal;char action[20];} UI;
static struct termios saved;
static int term_on;
static volatile sig_atomic_t stop_now;
static Service services[MAX_SVCS];
static size_t service_n;
static double history[HIST];
static int history_n;
static double net_rx,net_tx;
static long tick_rate;
static void restore(void){if(term_on){tcsetattr(STDIN_FILENO,TCSAFLUSH,&saved);fputs("\033[?25h\033[?1049l" R,stdout);fflush(stdout);term_on=0;}}
static void on_sig(int s){(void)s;stop_now=1;}
static int term_start(void){if(!isatty(0)||!isatty(1)){fputs("SysProbe dashboard requires an interactive terminal (TTY).\n",stderr);return -1;}if(tcgetattr(0,&saved))return -1;struct termios t=saved;t.c_lflag&=(tcflag_t)~(ICANON|ECHO);t.c_iflag&=(tcflag_t)~(IXON|ICRNL);t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;if(tcsetattr(0,TCSAFLUSH,&t))return -1;term_on=1;atexit(restore);struct sigaction sa={0};sa.sa_handler=on_sig;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);sigaction(SIGHUP,&sa,NULL);fputs("\033[?1049h\033[?25l\033[2J",stdout);return 0;}
static void mv(int y,int x){printf("\033[%d;%dH",y,x);}
static void put(int y,int x,const char *color,const char *fmt,...){mv(y,x);fputs(color,stdout);va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);fputs(R,stdout);}
static void clip(const char *s,int n){for(int i=0;i<n&&s[i];i++){unsigned char c=(unsigned char)s[i];putchar(c>=32&&c<127?c:'?');}}
static void line(int y,int x,int w){if(w<=0)return;mv(y,x);fputs(DIM,stdout);for(int i=0;i<w;i++)putchar('-');fputs(R,stdout);}
static void bar(int y,int x,int w,double p,const char *color){if(w<1)return;if(p<0)p=0;if(p>100)p=100;int fill=(int)(p*w/100.0+.5);mv(y,x);fputs(color,stdout);for(int i=0;i<w;i++)putchar(i<fill?'#':'.');fputs(R,stdout);}
static double pct(unsigned long long a,unsigned long long b){return b?100.0*(double)a/(double)b:0;}
static double gib(unsigned long long n){return (double)n/1073741824.;}
static double kibg(unsigned long long n){return (double)n/1048576.;}
static double elapsed(struct timespec a,struct timespec b){return (double)(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9;}
static int key(int timeout){fd_set f;FD_ZERO(&f);FD_SET(0,&f);struct timeval tv={timeout/1000,(timeout%1000)*1000};if(select(1,&f,NULL,NULL,&tv)<=0)return -1;unsigned char c;if(read(0,&c,1)!=1)return -1;if(c==27){unsigned char s[2];if(read(0,s,2)==2&&s[0]=='['){if(s[1]=='A')return 'k';if(s[1]=='B')return 'j';}return 27;}return c;}
static int match(const char *s,const char *needle){if(!*needle)return 1;for(;*s;s++){size_t i=0;while(needle[i]&&s[i]&&tolower((unsigned char)needle[i])==tolower((unsigned char)s[i]))i++;if(!needle[i])return 1;}return 0;}
static int cp_pid(const void*a,const void*b){const SpProcess*x=a,*y=b;return (x->pid>y->pid)-(x->pid<y->pid);}
static int cp_ram(const void*a,const void*b){const SpProcess*x=a,*y=b;if(x->rss_kib!=y->rss_kib)return x->rss_kib<y->rss_kib?1:-1;return cp_pid(a,b);}
static int cp_cpu(const void*a,const void*b){const SpProcess*x=a,*y=b;if(x->cpu_live!=y->cpu_live)return x->cpu_live<y->cpu_live?1:-1;return cp_pid(a,b);}
static void sort_proc(SpSnapshot*s,int mode){if(mode==0)qsort(s->processes,s->process_count,sizeof s->processes[0],cp_cpu);else if(mode==1)qsort(s->processes,s->process_count,sizeof s->processes[0],cp_ram);else qsort(s->processes,s->process_count,sizeof s->processes[0],cp_pid);}
static void rates(SpSnapshot*now,const SpSnapshot*old,double seconds){if(!old||seconds<=0)return;for(size_t i=0;i<now->process_count;i++){SpProcess*p=&now->processes[i];for(size_t j=0;j<old->process_count;j++){const SpProcess*q=&old->processes[j];if(p->pid==q->pid&&p->start_ticks==q->start_ticks&&p->cpu_ticks>=q->cpu_ticks){p->cpu_live=100.0*(double)(p->cpu_ticks-q->cpu_ticks)/(seconds*(double)tick_rate);break;}}}}
static void refresh_services(int sys){char command[240];snprintf(command,sizeof command,"systemctl %s list-units --type=service --all --no-pager --no-legend --plain 2>/dev/null",sys?"--system":"--user");FILE*f=popen(command,"r");service_n=0;if(!f)return;char l[512];while(fgets(l,sizeof l,f)&&service_n<MAX_SVCS){Service s={0};if(sscanf(l," %149s %19s %19s %23s",s.name,s.load,s.active,s.sub)==4 && strstr(s.name,".service"))services[service_n++]=s;}pclose(f);}
static int systemctl_action(int sys,const char*action,const char*unit,char*msg,size_t cap){pid_t pid=fork();if(pid<0){snprintf(msg,cap,"fork failed: %s",strerror(errno));return -1;}if(pid==0){int devnull=open("/dev/null",1);if(devnull>=0){dup2(devnull,STDOUT_FILENO);dup2(devnull,STDERR_FILENO);close(devnull);}execlp("systemctl","systemctl",sys?"--system":"--user","--no-ask-password","--no-pager",action,unit,(char*)NULL);_exit(127);}int status=0;if(waitpid(pid,&status,0)<0){snprintf(msg,cap,"waitpid: %s",strerror(errno));return -1;}int ok=WIFEXITED(status)&&WEXITSTATUS(status)==0;snprintf(msg,cap,"%.8s %.80s: %s",action,unit,ok?"OK":"failed (check permissions/journal)");return ok?0:-1;}
static unsigned long long pid_start(long pid){char path[64],buf[8192];snprintf(path,sizeof path,"/proc/%ld/stat",pid);FILE*f=fopen(path,"r");if(!f)return 0;if(!fgets(buf,sizeof buf,f)){fclose(f);return 0;}fclose(f);char*r=strrchr(buf,')');if(!r||r[1]!=' ')return 0;char*p=r+2;for(int field=3;field<=22;field++){while(*p==' ')p++;if(!*p)return 0;char*end=p;while(*end&&*end!=' '&&*end!='\n')end++;if(field==22){char tmp=*end;*end=0;unsigned long long n=strtoull(p,NULL,10);*end=tmp;return n;}p=end;}return 0;}
static int proc_action(UI*u){if(u->target_pid<=1||u->target_pid==getpid()||!u->target_start){snprintf(u->notice,sizeof u->notice,"Refusing unsafe PID");return -1;}if(pid_start(u->target_pid)!=u->target_start){snprintf(u->notice,sizeof u->notice,"PID changed/exited: no signal sent");return -1;}if(kill((pid_t)u->target_pid,u->target_signal)){snprintf(u->notice,sizeof u->notice,"PID %ld: %s",u->target_pid,strerror(errno));return -1;}snprintf(u->notice,sizeof u->notice,"Sent %s to PID %ld",u->target_signal==SIGTERM?"SIGTERM":"SIGKILL",u->target_pid);return 0;}
static int list_index(const SpSnapshot*s,const UI*u,int index){int n=0;for(size_t i=0;i<s->process_count;i++)if(match(s->processes[i].name,u->filter)){if(n++==index)return (int)i;}return -1;}
static int service_index(const UI*u,int index){int n=0;for(size_t i=0;i<service_n;i++)if(match(services[i].name,u->filter)){if(n++==index)return (int)i;}return -1;}
static int filtered_count(const SpSnapshot*s,const UI*u){int n=0;if(u->view==1){for(size_t i=0;i<service_n;i++)if(match(services[i].name,u->filter))n++;}else{for(size_t i=0;i<s->process_count;i++)if(match(s->processes[i].name,u->filter))n++;}return n;}
static void draw(const SpSnapshot*s,const UI*u,int w,int h){fputs("\033[H\033[2J",stdout);if(w<78||h<24){put(2,3,YE B,"SysProbe v2 | Terminal too small");put(4,3,DIM,"Resize to 78 columns x 24 rows. Press q to quit.");fflush(stdout);return;}
 time_t now=time(NULL);struct tm tmv;localtime_r(&now,&tmv);char clockbuf[24];strftime(clockbuf,sizeof clockbuf,"%H:%M:%S",&tmv);
 put(1,2,CY B,"SYSPROBE");put(1,13,DIM,"v2.0  |  Linux Task Manager");put(1,w-25,u->paused?YE B:GR B,"%s   %s",clockbuf,u->paused?"PAUSED":"LIVE");
 put(2,2,u->view==0?CY B:DIM,"[1] PROCESSES");put(2,20,u->view==1?CY B:DIM,"[2] SERVICES");put(2,38,u->view==2?CY B:DIM,"[3] PERFORMANCE");line(3,2,w-3);
 int left=(w-5)/2,rx=left+4,rw=w-rx-2;
 put(4,3,CY B,"CPU");put(4,12,FG,"%5.1f%%",s->cpu_percent);bar(5,3,left-2,s->cpu_percent,GR);
 put(6,3,DIM,"CPU history");int used=history_n<left-2?history_n:left-2;mv(7,3);fputs(GR,stdout);for(int i=0;i<left-2-used;i++)putchar(' ');static const char shades[]=" .:-=+*#%@";for(int i=history_n-used;i<history_n;i++){int a=(int)(history[i]*.09+.5);if(a<0)a=0;if(a>9)a=9;putchar(shades[a]);}fputs(R,stdout);
 put(4,rx,MG B,"MEMORY");put(4,rx+12,FG,"%.1f / %.1f GiB",kibg(s->mem_total_kib-s->mem_available_kib),kibg(s->mem_total_kib));bar(5,rx,rw-1,pct(s->mem_total_kib-s->mem_available_kib,s->mem_total_kib),MG);
 put(6,rx,DIM,"Swap %.2f / %.2f GiB",kibg(s->swap_total_kib-s->swap_free_kib),kibg(s->swap_total_kib));bar(7,rx,rw-1,pct(s->swap_total_kib-s->swap_free_kib,s->swap_total_kib),CY);
 put(8,3,CY B,"DISK /");put(8,12,FG,"%.1f / %.1f GiB",gib(s->disk_total_bytes-s->disk_available_bytes),gib(s->disk_total_bytes));bar(9,3,left-2,pct(s->disk_total_bytes-s->disk_available_bytes,s->disk_total_bytes),YE);
 put(8,rx,CY B,"NETWORK");put(8,rx+11,FG,"RX %.0f / TX %.0f KiB/s",net_rx/1024.,net_tx/1024.);
 put(10,3,DIM,"Host: %-.*s   Kernel: %-.*s",left/3,s->nodename,left/2,s->release);line(11,2,w-3);
 if(u->view==2){put(13,3,CY B,"PERFORMANCE OVERVIEW");put(15,4,FG,"CPU load       %5.1f%%",s->cpu_percent);put(17,4,FG,"RAM available  %.2f GiB",kibg(s->mem_available_kib));put(19,4,FG,"Swap used      %.2f GiB",kibg(s->swap_total_kib-s->swap_free_kib));put(21,4,FG,"Disk available %.2f GiB",gib(s->disk_available_bytes));put(23,4,FG,"Kernel         %.*s",w-25,s->release);}
 else{put(12,3,CY B,u->view==0?"PROCESSES":"SYSTEMD SERVICES");put(12,22,DIM,u->view==0?"sort: %s   filter: %s":"scope: %s   filter: %s",u->view==0?(u->sort==0?"CPU%":u->sort==1?"RAM":"PID"):(u->sys_services?"SYSTEM":"USER"),u->filter[0]?u->filter:"(none)");
 if(u->view==0)put(13,3,DIM,"%-7s %-*s %-11s %7s %8s %5s", "PID",w-51,"NAME","USER","CPU%","RAM MiB","STATE");
 else put(13,3,DIM,"%-*s %-11s %-12s %-13s",w-44,"UNIT","LOAD","ACTIVE","SUB");
 int visible=h-17;for(int i=0;i<visible;i++){int idx=u->scroll+i;if(idx>=filtered_count(s,u))break;int y=14+i;mv(y,3);fputs(idx==u->selected?BG CY B:FG,stdout);
 if(u->view==0){int j=list_index(s,u,idx);if(j<0)break;const SpProcess*p=&s->processes[j];char uname[12];struct passwd*pw=getpwuid((uid_t)p->uid);snprintf(uname,sizeof uname,"%.11s",pw?pw->pw_name:"unknown");printf("%-7ld ",p->pid);int namew=w-51;clip(p->name,namew);int len=(int)strlen(p->name);for(int k=len;k<namew;k++)putchar(' ');printf(" %-11s %7.1f %8.1f %5c",uname,p->cpu_live,(double)p->rss_kib/1024.,p->state);}
 else{int j=service_index(u,idx);if(j<0)break;Service*sv=&services[j];int namew=w-44;clip(sv->name,namew);int len=(int)strlen(sv->name);for(int k=len;k<namew;k++)putchar(' ');printf(" %-11s %-12s %-13s",sv->load,sv->active,sv->sub);}fputs(R,stdout);}
 }
 line(h-3,2,w-3);
 if(u->filter_mode)put(h-2,3,YE B,"SEARCH: %s_    [Enter] done  [Esc] cancel",u->filter);
 else if(u->confirm)put(h-2,3,RED B,"CONFIRM %s %s? [y] yes / [n] no",u->action,u->target_name);
 else if(u->view==0)put(h-2,3,DIM,"[j/k] move [/] find [s] sort [t] end task [K] force kill [d] details [?] help");
 else if(u->view==1)put(h-2,3,DIM,"[j/k] move [/] find [u] user/system [e] stop [b] start [r] refresh");
 else put(h-2,3,DIM,"[1] processes [2] services [3] performance [?] help");
 put(h-1,3,DIM,"[q] quit  [space] pause  [r] refresh  |  %s",u->notice[0]?u->notice:"Actions require confirmation; permissions are respected.");
 if(u->details&&u->view==0){int j=list_index(s,u,u->selected);if(j>=0){const SpProcess*p=&s->processes[j];int y=h/2-3;put(y++,7,BG CY B," TASK DETAILS                                   ");put(y++,7,BG," PID: %-10ld                              ",p->pid);put(y++,7,BG," NAME: %-32.32s       ",p->name);put(y++,7,BG," CPU: %7.2f%%   RAM: %8.1f MiB             ",p->cpu_live,(double)p->rss_kib/1024.);put(y++,7,BG," STATE: %c   START TICKS: %-14llu    ",p->state,p->start_ticks);put(y++,7,BG," Press d or Esc to close                     ");}}
 if(u->help){int y=5;put(y++,6,BG CY B," SYSPROBE v2 KEYBOARD GUIDE                         ");put(y++,6,BG," 1 processes  2 services  3 performance               ");put(y++,6,BG," j/k or arrows navigate   / search/filter             ");put(y++,6,BG," t SIGTERM (graceful)    K SIGKILL (force)            ");put(y++,6,BG," d process details       s sort by CPU/RAM/PID        ");put(y++,6,BG," u user/system services  e stop / b start service    ");put(y++,6,BG," y confirm action        n cancel                     ");put(y++,6,BG," space pause  r refresh  ? help  q quit               ");put(y++,6,BG," No automatic sudo. System services may deny control.");}
 fflush(stdout);
}
static void bound(UI*u,const SpSnapshot*s,int h){int n=filtered_count(s,u);if(u->selected>=n)u->selected=n?n-1:0;if(u->selected<0)u->selected=0;int rows=h-17;if(rows<1)rows=1;if(u->selected<u->scroll)u->scroll=u->selected;if(u->selected>=u->scroll+rows)u->scroll=u->selected-rows+1;}
int sp_run_tui(void){if(term_start())return 1;tick_rate=sysconf(_SC_CLK_TCK);if(tick_rate<1)tick_rate=100;SpSnapshot*cur=calloc(1,sizeof *cur),*previous=NULL;if(!cur){restore();return 1;}UI u={0};u.refresh=1;SpOptions opts={.interval_ms=80,.top=SP_MAX_PROCESSES,.processes=1};struct timespec last={0};unsigned long long rx=0,tx=0;refresh_services(0);while(!stop_now){struct winsize ws={0};if(ioctl(1,TIOCGWINSZ,&ws)){ws.ws_col=110;ws.ws_row=32;}int w=ws.ws_col,h=ws.ws_row;
 if(u.refresh&&!u.paused){SpSnapshot*next=calloc(1,sizeof *next);if(!next){snprintf(u.notice,sizeof u.notice,"Out of memory");break;}char err[200]={0};if(sp_collect(next,&opts,err,sizeof err)){snprintf(u.notice,sizeof u.notice,"Collector: %.170s",err);free(next);}else{struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);double seconds=last.tv_sec?elapsed(last,now):0;if(seconds>0){net_rx=next->net_rx_bytes>=rx?(next->net_rx_bytes-rx)/seconds:0;net_tx=next->net_tx_bytes>=tx?(next->net_tx_bytes-tx)/seconds:0;rates(next,cur,seconds);}last=now;rx=next->net_rx_bytes;tx=next->net_tx_bytes;previous=cur;cur=next;free(previous);sort_proc(cur,u.sort);if(history_n==HIST){memmove(history,history+1,(HIST-1)*sizeof(double));history_n--;}history[history_n++]=cur->cpu_percent;bound(&u,cur,h);}}
 if(u.view==1&&u.refresh)refresh_services(u.sys_services);
 u.refresh=0;bound(&u,cur,h);draw(cur,&u,w,h);
 for(int z=0;z<9&&!stop_now;z++){int c=key(100);if(c<0)continue;
 if(u.filter_mode){if(c==27){u.filter_mode=0;}else if(c==13||c==10){u.filter_mode=0;u.selected=u.scroll=0;}else if(c==127||c==8){size_t n=strlen(u.filter);if(n)u.filter[n-1]=0;u.selected=u.scroll=0;}else if(c>=32&&c<127){size_t n=strlen(u.filter);if(n+1<sizeof u.filter){u.filter[n]=(char)c;u.filter[n+1]=0;u.selected=u.scroll=0;}}}
 else if(u.confirm){if(c=='y'||c=='Y'){if(u.view==0)proc_action(&u);else systemctl_action(u.sys_services,u.action,u.target_name,u.notice,sizeof u.notice);u.refresh=1;}u.confirm=0;}
 else if(c=='q'||c==3){stop_now=1;break;}
 else if(c=='?'){u.help=!u.help;u.details=0;}
 else if(u.help){if(c==27||c=='h')u.help=0;}
 else if(c=='d'&&u.view==0)u.details=!u.details;
 else if(c==27)u.details=0;
 else if(c=='1'||c=='2'||c=='3'){u.view=c-'1';u.selected=u.scroll=0;u.filter[0]=0;u.details=0;if(u.view==1)refresh_services(u.sys_services);}
 else if(c=='/')u.filter_mode=1;
 else if(c==' ')u.paused=!u.paused;
 else if(c=='r'){u.paused=0;u.refresh=1;}
 else if(c=='u'&&u.view==1){u.sys_services=!u.sys_services;refresh_services(u.sys_services);u.selected=u.scroll=0;}
 else if(c=='s'&&u.view==0){u.sort=(u.sort+1)%3;sort_proc(cur,u.sort);u.selected=u.scroll=0;}
 else if(c=='j'){u.selected++;bound(&u,cur,h);}
 else if(c=='k'){u.selected--;bound(&u,cur,h);}
 else if((c=='t'||c=='K')&&u.view==0){int ix=list_index(cur,&u,u.selected);if(ix>=0){const SpProcess*p=&cur->processes[ix];u.target_pid=p->pid;u.target_start=p->start_ticks;u.target_signal=c=='t'?SIGTERM:SIGKILL;snprintf(u.target_name,sizeof u.target_name,"PID %ld (%.100s)",p->pid,p->name);snprintf(u.action,sizeof u.action,"%s",c=='t'?"SIGTERM":"SIGKILL");u.confirm=1;}}
 else if((c=='e'||c=='b')&&u.view==1){int ix=service_index(&u,u.selected);if(ix>=0){snprintf(u.target_name,sizeof u.target_name,"%s",services[ix].name);snprintf(u.action,sizeof u.action,"%s",c=='e'?"stop":"start");u.confirm=1;}}
 draw(cur,&u,w,h);if(u.refresh)break;}
 if(!u.paused)u.refresh=1;
 }
 free(cur);restore();return 0;}
