/* Deterministic single-CPU teaching simulator, NOT a Linux scheduler. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parse.h"
#define MAX_TASKS 128
#define MAX_EVENTS 200000
#define MAX_TIME 1000000000LL
typedef struct { char id[32]; long long arrival,burst,remaining,first,completion; int order; } Task;
typedef struct { int task; long long start,end; } Event;
typedef struct { Task tasks[MAX_TASKS]; int n; Event events[MAX_EVENTS]; int ne,switches; double waiting,turnaround,response; } Run;
static unsigned long checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int valid_id(const char *s) {
    if(!*s || strlen(s)>=32 || !strcmp(s,"IDLE")) return 0;
    for(;*s;s++) if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_')) return 0;
    return 1;
}
static int load(const char *path,Run *r) {
    FILE *f=fopen(path,"r"); if(!f) { perror(path); return 0; }
    char line[256]; int ln=0; long long total=0;
    while(fgets(line,sizeof line,f)) {
        ++ln; line[strcspn(line,"\r\n")]=0;
        if(!*line || line[0]=='#') continue;
        if(ln==1 && !strcmp(line,"id,arrival,burst")) continue;
        char *a=strchr(line,','); if(!a) goto invalid; *a++=0;
        char *b=strchr(a,','); if(!b) goto invalid; *b++=0;
        unsigned long long ar,bu;
        if(r->n==MAX_TASKS || !valid_id(line) || !parse_u64(a,MAX_TIME,&ar) || !parse_u64(b,MAX_TIME,&bu) || !bu) goto invalid;
        if(total+(long long)bu>MAX_TIME) goto invalid;
        for(int i=0;i<r->n;i++) if(!strcmp(r->tasks[i].id,line)) goto invalid;
        Task *t=&r->tasks[r->n]; strcpy(t->id,line); t->arrival=(long long)ar; t->burst=(long long)bu; t->order=r->n; ++r->n; total+=(long long)bu;
    }
    if(ferror(f) || !r->n) goto invalid;
    fclose(f); return 1;
invalid:
    fprintf(stderr,"invalid workload %s at line %d (max 128 tasks; total burst <= 1e9)\n",path,ln); fclose(f); return 0;
}
static int add_event(Run *r,int task,long long start,long long end) {
    if(r->ne==MAX_EVENTS) { fprintf(stderr,"event limit exceeded (200000)\n"); return 0; }
    r->events[r->ne++]=(Event){task,start,end}; return 1;
}
static int earlier(const Task *a,const Task *b) { return a->arrival<b->arrival || (a->arrival==b->arrival && a->order<b->order); }
static int simulate(Run *r,int algorithm,long long quantum) {
    if(r->n<1 || r->n>MAX_TASKS || algorithm<0 || algorithm>2 || quantum<=0) return 0;
    r->ne=r->switches=0; r->waiting=r->turnaround=r->response=0;
    int sorted[MAX_TASKS],rq[MAX_TASKS],head=0,nq=0,next=0,done=0,last=-1;
    for(int i=0;i<r->n;i++) {
        Task *t=&r->tasks[i]; if(t->arrival<0 || t->burst<=0) return 0;
        t->remaining=t->burst; t->first=-1; t->completion=-1; sorted[i]=i;
    }
    for(int i=1;i<r->n;i++) { int v=sorted[i],j=i; while(j && earlier(&r->tasks[v],&r->tasks[sorted[j-1]])) { sorted[j]=sorted[j-1]; --j; } sorted[j]=v; }
    long long now=0;
    while(done<r->n) {
        int selected=-1;
        if(algorithm==2) {
            while(next<r->n && r->tasks[sorted[next]].arrival<=now) { rq[(head+nq)%MAX_TASKS]=sorted[next++]; ++nq; }
            if(nq) { selected=rq[head]; head=(head+1)%MAX_TASKS; --nq; }
        } else {
            for(int j=0;j<r->n;j++) {
                int i=sorted[j]; Task *t=&r->tasks[i];
                if(!t->remaining || t->arrival>now) continue;
                if(selected<0 || (algorithm==1 && t->burst<r->tasks[selected].burst)) selected=i;
            }
        }
        if(selected<0) {
            long long wake=MAX_TIME*3;
            for(int i=0;i<r->n;i++) if(r->tasks[i].remaining && r->tasks[i].arrival>now && r->tasks[i].arrival<wake) wake=r->tasks[i].arrival;
            if(wake==MAX_TIME*3 || !add_event(r,-1,now,wake)) return 0;
            now=wake; last=-1; continue; /* IDLE resets adjacency; idle transitions do not count. */
        }
        Task *t=&r->tasks[selected]; long long span=t->remaining;
        if(algorithm==2 && span>quantum) span=quantum;
        if(t->first<0) t->first=now;
        if(last>=0 && selected!=last) ++r->switches;
        if(!add_event(r,selected,now,now+span)) return 0;
        last=selected; now+=span; t->remaining-=span;
        if(!t->remaining) { t->completion=now; ++done; }
        if(algorithm==2) {
            /* Arrivals at slice end join BEFORE the just-preempted task. */
            while(next<r->n && r->tasks[sorted[next]].arrival<=now) { rq[(head+nq)%MAX_TASKS]=sorted[next++]; ++nq; }
            if(t->remaining) { rq[(head+nq)%MAX_TASKS]=selected; ++nq; }
        }
    }
    for(int i=0;i<r->n;i++) {
        Task *t=&r->tasks[i]; long long turn=t->completion-t->arrival;
        r->waiting+=(double)(turn-t->burst)/r->n; r->turnaround+=(double)turn/r->n; r->response+=(double)(t->first-t->arrival)/r->n;
    }
    return 1;
}
static void baseline(Run *r) {
    memset(r,0,sizeof *r); r->n=3;
    for(int i=0;i<3;i++) { r->tasks[i].id[0]=(char)('A'+i); r->tasks[i].burst=i==0?5:i==1?2:3; r->tasks[i].order=i; }
}
static void exact_events(const Run *r,const int *ids,const int *ends,int n) {
    CHECK(r->ne==n); long long start=0;
    for(int i=0;i<n;i++) { CHECK(r->events[i].task==ids[i]); CHECK(r->events[i].start==start); CHECK(r->events[i].end==ends[i]); start=ends[i]; }
}
static void self_test(void) {
    Run *r=calloc(1,sizeof *r); CHECK(r);
    const int f[]={0,1,2},fe[]={5,7,10},s[]={1,2,0},se[]={2,5,10},rr[]={0,1,2,0,2,0},re[]={2,4,6,8,9,10};
    baseline(r); CHECK(simulate(r,0,2)); exact_events(r,f,fe,3); CHECK(fabs(r->waiting-4)<1e-9); CHECK(fabs(r->turnaround-22.0/3)<1e-9); CHECK(fabs(r->response-4)<1e-9); CHECK(r->switches==2);
    baseline(r); CHECK(simulate(r,1,2)); exact_events(r,s,se,3); CHECK(fabs(r->waiting-7.0/3)<1e-9); CHECK(r->tasks[0].first==5); CHECK(r->switches==2);
    baseline(r); CHECK(simulate(r,2,2)); exact_events(r,rr,re,6); CHECK(fabs(r->waiting-13.0/3)<1e-9); CHECK(fabs(r->response-2)<1e-9); CHECK(r->switches==5);
    long long completion[]={10,4,9},wait[]={5,2,6},response[]={0,2,4};
    for(int i=0;i<3;i++) { CHECK(r->tasks[i].completion==completion[i]); CHECK(r->tasks[i].completion-r->tasks[i].burst==wait[i]); CHECK(r->tasks[i].first==response[i]); }
    baseline(r); CHECK(simulate(r,2,100)); exact_events(r,f,fe,3);
    baseline(r); CHECK(simulate(r,2,1)); CHECK(r->ne==10); CHECK(r->tasks[1].completion==5); CHECK(r->tasks[2].completion==8); CHECK(r->switches==8);
    /* Equal bursts tie by arrival then input order; idle doesn't count as switch. */
    baseline(r); for(int i=0;i<3;i++) { r->tasks[i].arrival=i==2?10:3; r->tasks[i].burst=2; }
    for(int a=0;a<3;a++) { CHECK(simulate(r,a,1)); CHECK(r->events[0].task==-1); CHECK(r->tasks[0].first==3); CHECK(r->tasks[2].first==10); CHECK(r->tasks[2].completion==12); CHECK(r->switches==(a==2?3:1)); }
    /* Non-preemption: short newcomer cannot interrupt a running long task. */
    baseline(r); r->n=2; r->tasks[1].arrival=1; CHECK(simulate(r,1,2)); CHECK(r->tasks[1].first==5);
    /* At time 2 B arrives before A is requeued. */
    baseline(r); r->n=2; r->tasks[1].arrival=2; CHECK(simulate(r,2,2)); CHECK(r->events[1].task==1);
    baseline(r); r->n=1; CHECK(simulate(r,2,1)); CHECK(r->switches==0); CHECK(r->waiting==0);
    /* Input need not be sorted by arrival. Multiple idle gaps are explicit. */
    baseline(r); r->tasks[0].arrival=5; r->tasks[0].burst=2;
    r->tasks[1].arrival=0; r->tasks[1].burst=1; r->tasks[2].arrival=2; r->tasks[2].burst=1;
    for(int a=0;a<3;a++) { CHECK(simulate(r,a,1)); CHECK(r->tasks[0].completion==7); CHECK(r->tasks[1].completion==1); CHECK(r->tasks[2].completion==3); CHECK(r->waiting==0); CHECK(r->switches==0); }
    CHECK(!simulate(r,2,0)); CHECK(!simulate(r,2,-1)); r->tasks[0].burst=0; CHECK(!simulate(r,0,1));
    printf("Scheduler PASS: %lu assertions; exact theoretical timelines + arrival/idle/tie/q boundaries\n",checks); free(r);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--test")) { self_test(); return 0; }
    if(argc==2 && !strcmp(argv[1],"--help")) { puts("scheduler WORKLOAD.csv fcfs|sjf|rr QUANTUM OUTPUT_PREFIX\noutputs: .tasks.csv .timeline.csv .summary.csv; time unit is simulated tick"); return 0; }
    if(argc!=5) { fprintf(stderr,"Use --help\n"); return 1; }
    int a=!strcmp(argv[2],"fcfs")?0:!strcmp(argv[2],"sjf")?1:!strcmp(argv[2],"rr")?2:-1;
    unsigned long long q;
    if(a<0 || !parse_u64(argv[3],MAX_TIME,&q) || !q) { fprintf(stderr,"invalid algorithm or quantum\n"); return 1; }
    Run *r=calloc(1,sizeof *r); if(!r) return 1;
    if(!load(argv[1],r) || !simulate(r,a,(long long)q)) { free(r); return 1; }
    const char *suffixes[]={"tasks","timeline","summary"};
    for(int k=0;k<3;k++) {
        char path[4096]; int n=snprintf(path,sizeof path,"%s.%s.csv",argv[4],suffixes[k]);
        if(n<0 || (size_t)n>=sizeof path) { free(r); return 1; }
        FILE *f=fopen(path,"w"); if(!f) { perror(path); free(r); return 1; }
        if(k==0) {
            fprintf(f,"id,arrival,burst,first_start,completion,waiting,turnaround,response\n");
            for(int i=0;i<r->n;i++) { Task *t=&r->tasks[i]; long long turn=t->completion-t->arrival; fprintf(f,"%s,%lld,%lld,%lld,%lld,%lld,%lld,%lld\n",t->id,t->arrival,t->burst,t->first,t->completion,turn-t->burst,turn,t->first-t->arrival); }
        } else if(k==1) {
            fprintf(f,"task,start,end\n"); for(int i=0;i<r->ne;i++) { Event *e=&r->events[i]; fprintf(f,"%s,%lld,%lld\n",e->task<0?"IDLE":r->tasks[e->task].id,e->start,e->end); }
        } else fprintf(f,"algorithm,quantum,mean_waiting,mean_turnaround,mean_response,context_switches\n%s,%llu,%.9f,%.9f,%.9f,%d\n",argv[2],q,r->waiting,r->turnaround,r->response,r->switches);
        int error=ferror(f); if(fclose(f) || error) { free(r); return 1; }
    }
    printf("%s waiting=%.9f turnaround=%.9f response=%.9f switches=%d\n",argv[2],r->waiting,r->turnaround,r->response,r->switches); free(r); return 0;
}
