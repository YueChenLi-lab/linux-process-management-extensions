#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "parse.h"
typedef struct {
    int role,error,actual_nice,cpu,policy; pid_t pid;
    double start,end,cpu_seconds; unsigned long long iterations,checksum;
} Report;
static volatile sig_atomic_t interrupted;
static void on_signal(int s) { interrupted=s; }
static double clock_seconds(clockid_t clockid) {
    struct timespec t; if(clock_gettime(clockid,&t)) return -1;
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int write_all(int fd,const void *data,size_t size) {
    const char *p=data;
    while(size) { ssize_t n=write(fd,p,size); if(n<0 && errno==EINTR) continue; if(n<=0) return 0; p+=n; size-=(size_t)n; } return 1;
}
static int read_all(int fd,void *data,size_t size) {
    char *p=data;
    while(size) { ssize_t n=read(fd,p,size); if(n<0 && errno==EINTR && !interrupted) continue; if(n<=0) return 0; p+=n; size-=(size_t)n; } return 1;
}
/* Unsigned integer recurrence is defined modulo 2^64; printed checksum keeps work observable. */
static uint64_t work(uint64_t x,unsigned long long n) {
    for(unsigned long long i=0;i<n;i++) { x^=x<<13; x^=x>>7; x^=x<<17; }
    return x;
}
static void child(int role,int nice_value,int cpu,unsigned long long iterations,int gate,int ready,int output,int stopfd) {
    Report r={0}; r.role=role; r.pid=getpid();
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set);
    if(sched_setaffinity(0,sizeof set,&set)) r.error=errno;
    if(!r.error && setpriority(PRIO_PROCESS,0,nice_value)) r.error=errno;
    errno=0; r.actual_nice=getpriority(PRIO_PROCESS,0); if(errno) r.error=errno;
    r.cpu=sched_getcpu(); r.policy=sched_getscheduler(0);
    if(!r.error && (r.actual_nice!=nice_value || r.cpu!=cpu || r.policy!=SCHED_OTHER)) r.error=EINVAL;
    if(role && fcntl(stopfd,F_SETFL,O_NONBLOCK)<0) r.error=errno;
    char token='r'; if(!write_all(ready,&token,1)) _exit(20); close(ready);
    if(!read_all(gate,&token,1)) _exit(21);
    close(gate);
    if(!r.error) {
        double c0=clock_seconds(CLOCK_PROCESS_CPUTIME_ID); r.start=clock_seconds(CLOCK_MONOTONIC);
        uint64_t x=UINT64_C(88172645463325252);
        if(!role) { x=work(x,iterations); r.iterations=iterations; }
        else {
            /* Fixed chunk and input/code path, continue competing until parent signals target exit.
             * Competitor duration is controlled, not a second fixed-size target workload. */
            for(;;) {
                char stop; ssize_t n=read(stopfd,&stop,1);
                if(n==1 || n==0 || interrupted) break;
                if(n<0 && errno!=EAGAIN && errno!=EINTR) { r.error=errno; break; }
                x=work(x,65536); r.iterations+=65536;
            }
        }
        r.end=clock_seconds(CLOCK_MONOTONIC); r.cpu_seconds=clock_seconds(CLOCK_PROCESS_CPUTIME_ID)-c0; r.checksum=x;
        if(r.start<0 || r.end<r.start || r.cpu_seconds<0) r.error=EIO;
    }
    if(role) close(stopfd);
    if(!write_all(output,&r,sizeof r)) _exit(22);
    close(output); _exit(r.error?23:0);
}
static int wait_bounded(pid_t pid,int *status,double deadline) {
    struct timespec pause={0,1000000};
    while(!interrupted && clock_seconds(CLOCK_MONOTONIC)<deadline) {
        pid_t n=waitpid(pid,status,WNOHANG); if(n==pid) return 1;
        if(n<0 && errno!=EINTR) return 0;
        nanosleep(&pause,NULL);
    }
    return 0;
}
static void terminate_reap(pid_t pid) {
    if(pid<=0) return;
    (void)kill(pid,SIGKILL); int st; while(waitpid(pid,&st,0)<0 && errno==EINTR) {}
}
static int run_trial(int cpu,int ni,int competition,unsigned long long iterations,double timeout,Report *target,Report *competitor,int *ts,int *cs) {
    int gate[2],ready[2],out[2],stop[2];
    if(pipe(gate)) return 0;
    if(pipe(ready)) { close(gate[0]); close(gate[1]); return 0; }
    if(pipe(out)) { close(gate[0]); close(gate[1]); close(ready[0]); close(ready[1]); return 0; }
    if(pipe(stop)) { close(gate[0]); close(gate[1]); close(ready[0]); close(ready[1]); close(out[0]); close(out[1]); return 0; }
    pid_t pids[2]={0,0}; int count=competition?2:1,ok=1; double deadline=clock_seconds(CLOCK_MONOTONIC)+timeout;
    for(int role=0;role<count;role++) {
        pids[role]=fork();
        if(pids[role]<0) { pids[role]=0; ok=0; break; }
        if(!pids[role]) {
            close(gate[1]); close(ready[0]); close(out[0]); close(stop[1]);
            if(!role) close(stop[0]);
            child(role,role?0:ni,cpu,iterations,gate[0],ready[1],out[1],stop[0]);
        }
    }
    close(gate[0]); close(ready[1]); close(out[1]); close(stop[0]);
    /* Ready bytes are sent only after affinity/nice setup; gate releases both children. */
    if(ok) {
        int flags=fcntl(ready[0],F_GETFL);
        if(flags<0 || fcntl(ready[0],F_SETFL,flags|O_NONBLOCK)<0) ok=0;
        int received=0; struct timespec pause={0,1000000};
        while(ok && received<count && !interrupted && clock_seconds(CLOCK_MONOTONIC)<deadline) {
            char token; ssize_t n=read(ready[0],&token,1);
            if(n==1) received++;
            else if(n==0 || (n<0 && errno!=EAGAIN && errno!=EINTR)) ok=0;
            else nanosleep(&pause,NULL);
        }
        if(received!=count) ok=0;
    }
    close(ready[0]);
    if(ok) { const char tokens[2]={'g','g'}; ok=write_all(gate[1],tokens,(size_t)count); }
    close(gate[1]);
    if(ok) { ok=wait_bounded(pids[0],ts,deadline); if(ok) pids[0]=0; }
    if(competition && ok) { char token='s'; ok=write_all(stop[1],&token,1); }
    close(stop[1]);
    if(competition && ok) { ok=wait_bounded(pids[1],cs,deadline); if(ok) pids[1]=0; }
    if(!ok) for(int i=0;i<count;i++) terminate_reap(pids[i]);
    if(ok) for(int i=0;i<count;i++) {
        Report r;
        if(!read_all(out[0],&r,sizeof r) || r.role<0 || r.role>=count) { ok=0; break; }
        if(r.role) *competitor=r; else *target=r;
    }
    close(out[0]);
    if(!ok) fprintf(stderr,"trial failed/timeout/interrupted; all created children reaped\n");
    return ok;
}
static uint32_t rng_next(uint32_t *s) { *s=*s*1664525u+1013904223u; return *s; }
int main(int argc,char **argv) {
    unsigned long long rounds=10,iterations=100000000,seed=20261029,cpu_option=0,timeout=30; int cpu=-1;
    const char *output=NULL;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("nice_benchmark --output FILE [--rounds 10] [--iterations 100000000] [--seed 20261029] [--cpu N] [--timeout 30]\nFour conditions per round, shuffled with recorded seed. Linux/SCHED_OTHER only; no root required.\nOn any error returns nonzero and retains CSV rows. Competitor uses fixed 65536-iteration chunks until target exits."); return 0;
        }
        const char *arg=argv[i]; if(++i>=argc) { fprintf(stderr,"missing option value\n"); return 1; }
        if(!strcmp(arg,"--output")) { output=argv[i]; continue; }
        unsigned long long v;
        if(!parse_u64(argv[i],100000000000ULL,&v)) { fprintf(stderr,"invalid numeric option\n"); return 1; }
        if(!strcmp(arg,"--rounds") && v>=1 && v<=1000) rounds=v;
        else if(!strcmp(arg,"--iterations") && v>=1 && v<=10000000000ULL) iterations=v;
        else if(!strcmp(arg,"--seed") && v<=UINT32_MAX) seed=v;
        else if(!strcmp(arg,"--cpu") && v<CPU_SETSIZE) { cpu_option=v; cpu=(int)cpu_option; }
        else if(!strcmp(arg,"--timeout") && v>=1 && v<=300) timeout=v;
        else { fprintf(stderr,"unknown/out-of-range option %s\n",arg); return 1; }
    }
    if(!output) { fprintf(stderr,"--output FILE required\n"); return 1; }
    cpu_set_t allowed;
    if(sched_getaffinity(0,sizeof allowed,&allowed)) { perror("sched_getaffinity NOT_RUN"); return 2; }
    if(cpu<0) for(int i=0;i<CPU_SETSIZE;i++) if(CPU_ISSET(i,&allowed)) { cpu=i; break; }
    if(cpu<0 || !CPU_ISSET(cpu,&allowed)) { fprintf(stderr,"CPU unavailable NOT_RUN\n"); return 2; }
    if(sched_getscheduler(0)!=SCHED_OTHER) { fprintf(stderr,"requires SCHED_OTHER NOT_RUN\n"); return 2; }
    errno=0; int inherited=getpriority(PRIO_PROCESS,0);
    if(errno || inherited!=0) { fprintf(stderr,"parent nice must be 0; cannot safely restore inherited priority NOT_RUN\n"); return 2; }
    struct utsname u; if(uname(&u)) { perror("uname"); return 2; }
    FILE *f=fopen(output,"w"); if(!f) { perror(output); return 1; }
    struct sigaction sa={0}; sa.sa_handler=on_signal; sigemptyset(&sa.sa_mask); sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);
    signal(SIGPIPE,SIG_IGN);
    fprintf(f,"run,round,condition_order,seed,target_nice,competition,cpu,kernel,arch,target_pid,actual_nice,actual_cpu,policy,iterations,start_mono,end_mono,elapsed_s,cpu_s,checksum,target_wait_status,target_error,competitor_pid,competitor_nice,competitor_cpu,competitor_start_mono,competitor_end_mono,competitor_elapsed_s,competitor_cpu_s,competitor_iterations,competitor_wait_status,competitor_error,start_skew_s,status\n");
    uint32_t state=(uint32_t)seed; int failures=0; unsigned run=0;
    for(unsigned round=1;round<=rounds && !interrupted;round++) {
        int order[4]={0,1,2,3}; for(int i=3;i>0;i--) { int j=(int)((rng_next(&state)>>8)%(unsigned)(i+1)); int v=order[i]; order[i]=order[j]; order[j]=v; }
        for(int k=0;k<4 && !interrupted;k++) {
            int condition=order[k],ni=(condition%2)*10,competition=condition/2,ts=-1,cs=0;
            Report t={0},c={0}; int ok=run_trial(cpu,ni,competition,iterations,(double)timeout,&t,&c,&ts,&cs);
            ok=ok && !t.error && WIFEXITED(ts) && WEXITSTATUS(ts)==0 && (!competition || (!c.error && WIFEXITED(cs) && WEXITSTATUS(cs)==0));
            ++run;
            fprintf(f,"%u,%u,%d,%llu,%d,%d,%d,%s,%s,%d,%d,%d,%d,%llu,%.9f,%.9f,%.9f,%.9f,%llu,%d,%d,%d,%d,%d,%.9f,%.9f,%.9f,%.9f,%llu,%d,%d,%.9f,%s\n",run,round,k+1,seed,ni,competition,cpu,u.release,u.machine,(int)t.pid,t.actual_nice,t.cpu,t.policy,t.iterations,t.start,t.end,t.end-t.start,t.cpu_seconds,t.checksum,ts,t.error,(int)c.pid,c.actual_nice,c.cpu,c.start,c.end,c.end-c.start,c.cpu_seconds,c.iterations,cs,c.error,competition?c.start-t.start:0,ok?"PASS":"FAIL");
            if(fflush(f)) { failures++; break; }
            fprintf(stderr,"run=%u round=%u nice=%d competition=%d elapsed=%.6f cpu=%.6f %s\n",run,round,ni,competition,t.end-t.start,t.cpu_seconds,ok?"PASS":"FAIL");
            if(!ok) { failures++; break; }
        }
        if(failures) break;
    }
    int io_error=ferror(f); if(fclose(f)) io_error=1;
    if(interrupted || failures || io_error) return 1;
    fprintf(stderr,"COMPLETE: %u trials; %llu rounds per condition; cpu=%d; seed=%llu\n",run,rounds,cpu,seed);
    return run==rounds*4?0:1;
}
