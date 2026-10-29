/* Teaching model only: these are user-space PCBs, not kernel task_structs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "parse.h"
#define MAX_PCB 128
#define NAME_LEN 32
typedef struct PCB { char name[NAME_LEN], state; unsigned ntime,rtime; struct PCB *link; } PCB;
typedef struct { PCB *head,*tail; } Queue;
typedef struct { PCB pcbs[MAX_PCB]; int count; Queue ready,blocked; unsigned long step; } Model;
static unsigned long checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void append(Queue *q, PCB *p) {
    p->link=NULL;
    if (q->tail) q->tail->link=p; else q->head=p;
    q->tail=p; /* New tail must terminate the list, including an empty list. */
}
static PCB *pop(Queue *q) {
    PCB *p=q->head;
    if (!p) return NULL;
    q->head=p->link;
    if (!q->head) q->tail=NULL; /* Removing last node must clear BOTH ends. */
    p->link=NULL; return p;
}
static void promote(Model *m) { if(m->ready.head) m->ready.head->state='R'; }
static int index_of(const Model *m,const PCB *p) {
    for(int i=0;i<m->count;i++) if(p==&m->pcbs[i]) return i;
    return -1;
}
static const char *invariants(const Model *m) {
    unsigned seen[MAX_PCB]={0};
    const Queue *qs[]={&m->ready,&m->blocked};
    if(m->count<0 || m->count>MAX_PCB) return "invalid registry size";
    for(int k=0;k<2;k++) {
        const Queue *q=qs[k];
        if((q->head==NULL)!=(q->tail==NULL)) return "head/tail mismatch";
        const PCB *last=NULL; int nodes=0,running=0;
        for(const PCB *p=q->head;p;p=p->link) {
            int i=index_of(m,p);
            if(i<0) return "unknown pointer";
            if(++nodes>m->count || seen[i]++) return "cycle or duplicate membership";
            if(p->rtime>p->ntime || !p->ntime) return "invalid runtime";
            if(k==1 && p->state!='b') return "blocked state mismatch";
            if(k==0) {
                if(p->state=='R') { if(++running>1 || p!=q->head) return "running node not unique head"; }
                else if(p->state!='w') return "ready state mismatch";
            }
            last=p;
        }
        if(last!=q->tail) return "tail not final node";
        /* Check pointer membership before dereferencing to avoid an invalid tail. */
        if(q->tail && (index_of(m,q->tail)<0 || q->tail->link)) return "tail next non-null";
    }
    for(int i=0;i<m->count;i++) {
        const PCB *p=&m->pcbs[i];
        if(p->state=='F') {
            if(seen[i] || p->link || p->rtime!=p->ntime) return "finished PCB still active or incomplete";
        } else if(seen[i]!=1) return "lost active PCB";
    }
    return NULL;
}
static int apply(Model *m,const char *event,const char *name,unsigned runtime,char *process) {
    PCB *p=NULL; process[0]='-'; process[1]=0;
    if(!strcmp(event,"create")) {
        if(!name || !*name || strlen(name)>=NAME_LEN || !runtime || m->count==MAX_PCB) return -1;
        for(const char *s=name;*s;s++) if(!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_')) return -1;
        for(int i=0;i<m->count;i++) if(!strcmp(name,m->pcbs[i].name)) return -1;
        p=&m->pcbs[m->count++]; strcpy(p->name,name); p->state='w'; p->ntime=runtime;
        append(&m->ready,p);
    } else if(!strcmp(event,"block")) {
        p=pop(&m->ready); if(p) { p->state='b'; append(&m->blocked,p); }
    } else if(!strcmp(event,"wake")) {
        p=pop(&m->blocked); if(p) { p->state='w'; append(&m->ready,p); }
    } else if(!strcmp(event,"step") || !strcmp(event,"finish")) {
        p=m->ready.head;
        if(p) {
            if(!strcmp(event,"finish")) p->rtime=p->ntime; else ++p->rtime;
            if(p->rtime==p->ntime) { (void)pop(&m->ready); p->state='F'; }
        }
    } else return -1;
    if(p) strcpy(process,p->name);
    promote(m); ++m->step; return p?1:0;
}
static void snapshot(FILE *f,const Queue *q) {
    fputc('"',f); int bound=0;
    for(const PCB *p=q->head;p && bound++<MAX_PCB;p=p->link)
        fprintf(f,"%s:%c:%u/%u%s",p->name,p->state,p->rtime,p->ntime,p->link?";":"");
    fputc('"',f);
}
static int record(Model *m,FILE *f,const char *event,const char *name,unsigned runtime) {
    char proc[NAME_LEN]; int rc=apply(m,event,name,runtime,proc);
    if(rc<0) { fprintf(stderr,"invalid event at step %lu: %s\n",m->step+1,event); return 1; }
    const char *why=invariants(m);
    if(why) {
        /* A corrupt list is unsafe to traverse again merely to print it. */
        fprintf(f,"%lu,%s,%s,ERROR,\"UNSAFE\",\"UNSAFE\",FAIL\n",m->step,event,proc);
        fprintf(stderr,"invariant failure at step %lu: %s\n",m->step,why); return 2;
    }
    fprintf(f,"%lu,%s,%s,%s,",m->step,event,proc,rc?"APPLIED":"NOOP");
    snapshot(f,&m->ready); fputc(',',f); snapshot(f,&m->blocked); fprintf(f,",PASS\n");
    return 0;
}
/* Independent FIFO reference model checks order/state/runtime after each action. */
static void self_test(void) {
    Model m={0}; char p[NAME_LEN];
    CHECK(apply(&m,"wake",NULL,0,p)==0); CHECK(!invariants(&m));
    CHECK(apply(&m,"block",NULL,0,p)==0); CHECK(apply(&m,"finish",NULL,0,p)==0);
    CHECK(apply(&m,"create","A",2,p)==1); CHECK(m.ready.head==m.ready.tail); CHECK(m.ready.head->state=='R');
    CHECK(apply(&m,"create","A",2,p)==-1); CHECK(apply(&m,"create","bad,",2,p)==-1);
    CHECK(apply(&m,"block",NULL,0,p)==1); CHECK(!m.ready.head && !m.ready.tail); CHECK(!invariants(&m));
    CHECK(apply(&m,"wake",NULL,0,p)==1); CHECK(!m.blocked.head && !m.blocked.tail);
    CHECK(apply(&m,"step",NULL,0,p)==1); CHECK(m.pcbs[0].rtime==1);
    CHECK(apply(&m,"step",NULL,0,p)==1); CHECK(!m.ready.head && !m.ready.tail); CHECK(!invariants(&m));
    /* Deliberately corrupt lists to prove the checker can detect each class. */
    Model z={0}; CHECK(apply(&z,"create","X",3,p)==1); CHECK(apply(&z,"create","Y",3,p)==1);
    z.ready.tail->link=z.ready.head; CHECK(invariants(&z)); z.ready.tail->link=NULL;
    z.ready.tail=&z.pcbs[0]; CHECK(invariants(&z)); z.ready.tail=&z.pcbs[1];
    z.pcbs[1].state='R'; CHECK(invariants(&z)); z.pcbs[1].state='w';
    z.blocked.head=z.blocked.tail=z.ready.tail; CHECK(invariants(&z)); z.blocked=(Queue){0};
    z.ready.head->state='b'; CHECK(invariants(&z)); z.ready.head->state='R';
    z.ready.head=&z.pcbs[1]; CHECK(invariants(&z)); z.ready.head=&z.pcbs[0];
    z.pcbs[0].state='F'; CHECK(invariants(&z)); z.pcbs[0].state='R'; CHECK(!invariants(&z));
    Model full={0};
    for(int i=0;i<MAX_PCB;i++) { char name[32]; snprintf(name,sizeof name,"Full%d",i); CHECK(apply(&full,"create",name,1,p)==1); }
    CHECK(apply(&full,"create","Overflow",1,p)==-1); CHECK(!invariants(&full));
    for(unsigned seed=1;seed<=10;seed++) {
        Model t={0}; int rq[MAX_PCB],bq[MAX_PCB],nr=0,nb=0; unsigned elapsed[MAX_PCB]={0};
        uint32_t rng=seed;
        for(int i=0;i<32;i++) { char name[32]; snprintf(name,sizeof name,"P%d",i); CHECK(apply(&t,"create",name,(unsigned)(i%7+1),p)==1); rq[nr++]=i; }
        for(int s=0;s<5000;s++) {
            /* Refresh every 100 events so most randomized checks exercise live nodes,
             * instead of eventually spending thousands of events on empty queues. */
            if(s && s%100==0) {
                memset(&t,0,sizeof t); memset(elapsed,0,sizeof elapsed); nr=nb=0;
                for(int i=0;i<32;i++) { char name[32]; snprintf(name,sizeof name,"P%d",i); CHECK(apply(&t,"create",name,(unsigned)(i%7+1),p)==1); rq[nr++]=i; }
            }
            rng=rng*1664525u+1013904223u; unsigned op=(rng>>16)%4;
            const char *events[]={"step","block","wake","finish"};
            int idx=-1;
            if(op==2 && nb) { idx=bq[0]; memmove(bq,bq+1,(size_t)(--nb)*sizeof(int)); rq[nr++]=idx; }
            else if(op!=2 && nr) {
                idx=rq[0];
                if(op==0) ++elapsed[idx];
                if(op==3) elapsed[idx]=t.pcbs[idx].ntime;
                if(op==1 || elapsed[idx]==t.pcbs[idx].ntime) { memmove(rq,rq+1,(size_t)(--nr)*sizeof(int)); if(op==1) bq[nb++]=idx; }
            }
            CHECK(apply(&t,events[op],NULL,0,p)==(idx>=0)); CHECK(!invariants(&t));
            PCB *n=t.ready.head;
            for(int i=0;i<nr;i++) { CHECK(n==&t.pcbs[rq[i]]); CHECK(n->state==(i?'w':'R')); n=n->link; } CHECK(!n);
            n=t.blocked.head; for(int i=0;i<nb;i++) { CHECK(n==&t.pcbs[bq[i]]); CHECK(n->state=='b'); n=n->link; } CHECK(!n);
            for(int i=0;i<32;i++) CHECK(t.pcbs[i].rtime==elapsed[i]);
        }
    }
    printf("PCB PASS: %lu assertions; 10 seeds x 5000 reference-model operations; 7 corruption checks\n",checks);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--test")) { self_test(); return 0; }
    if(argc==2 && !strcmp(argv[1],"--help")) { puts("pcb_queues --demo | --scenario FILE | --test\nscenario: create NAME RUNTIME; step; block; wake; finish (one event per line)"); return 0; }
    FILE *in=NULL;
    if(argc==3 && !strcmp(argv[1],"--scenario")) { in=fopen(argv[2],"r"); if(!in) { perror(argv[2]); return 1; } }
    else if(!(argc==2 && !strcmp(argv[1],"--demo"))) { fprintf(stderr,"Use --help\n"); return 1; }
    Model m={0}; puts("step,event,process,result,ready,blocked,invariant");
    if(!in) {
        const char *events[]={"wake","block","create","create","create","step","block","block","wake","step","wake","finish","step","step","finish","wake","block"};
        const char *names[]={NULL,NULL,"A","B","C"};
        for(size_t i=0;i<sizeof events/sizeof *events;i++) if(record(&m,stdout,events[i],i<5?names[i]:NULL,i==2?3:i==3?2:i==4?4:0)) return 1;
    } else {
        char line[256],e[32],name[64],num[64],extra[2]; unsigned long lineno=0;
        while(fgets(line,sizeof line,in)) {
            ++lineno; if(!strchr(line,'\n') && !feof(in)) { fprintf(stderr,"line too long\n"); fclose(in); return 1; }
            if(line[0]=='#' || line[0]=='\n') continue;
            int n=sscanf(line,"%31s %63s %63s %1s",e,name,num,extra); unsigned long long v=0;
            if(n<1 || e[0]=='#') continue;
            if((!strcmp(e,"create") && (n!=3 || !parse_u64(num,UINT_MAX,&v) || !v)) || (strcmp(e,"create") && n!=1) || record(&m,stdout,e,n==3?name:NULL,(unsigned)v)) {
                fprintf(stderr,"scenario failed line %lu\n",lineno); fclose(in); return 1;
            }
        }
        if(ferror(in)) { fclose(in); return 1; } fclose(in);
    }
    return ferror(stdout)?1:0;
}
