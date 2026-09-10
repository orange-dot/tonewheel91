/* MA3-PERF: hosted timing only. No allocation, hashing or event dispatch is
 * inside a measured block. Run alone and pin externally with taskset. */
#define _GNU_SOURCE
#include "../src/mamutanalog.h"
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>

enum { RATE=48000, DEFAULT_WARMUP=8, DEFAULT_BLOCKS=32, MAX_FRAMES=512 };
static ma_voice_pool pool;
static unsigned env_u(const char *name, unsigned fallback) {
    const char *s=getenv(name); char *end=0;
    if(!s||!*s) return fallback;
    unsigned long v=strtoul(s,&end,10);
    return end!=s&&!*end&&v<1000000 ? (unsigned)v : fallback;
}
static double us(struct timespec a, struct timespec b) {
    return (double)(b.tv_sec-a.tv_sec)*1e6+(double)(b.tv_nsec-a.tv_nsec)*.001;
}
static struct timespec clock_now(clockid_t id) {
    struct timespec t={0}; if(clock_gettime(id,&t)){perror("clock_gettime");exit(1);} return t;
}
static int cmp(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y);
}
static bool selected(const char *filter,const char *name) {
    return !filter||!*filter||!strcmp(filter,"all")||!strcmp(filter,name);
}
static void arm_effects(unsigned arm) {
    ma_effects_set_chorus(&pool.effects,(ma_chorus_controls){
        arm==1||arm==3,.30f,.35f,.25f});
    ma_effects_set_reverb(&pool.effects,(ma_reverb_controls){
        arm==2||arm==3,.25f,.65f,.55f});
}
static bool populate(unsigned limit,unsigned unison,unsigned scenario) {
    unsigned count=scenario==0?0:scenario==1?1:scenario==2?8:limit;
    if(count%unison) count-=count%unison;
    for(unsigned n=0;n<count/unison;n++)
        if(!ma_voice_pool_note_on(&pool,0,(uint8_t)(36+n%48),100)) return false;
    return true;
}
static void release_setup(unsigned scenario,unsigned warmup,unsigned blocks,unsigned count) {
    if(scenario!=4) return;
    ma_voice_pool_set_sustain(&pool,0,true);
    for(unsigned n=0;n<count;n++) (void)ma_voice_pool_note_off(&pool,0,(uint8_t)(36+n%48),0);
    (void)warmup; (void)blocks;
}
static uint64_t run_case(const char *patch_name,const ma_patch *patch,unsigned bcs,
    unsigned limit,unsigned frames,unsigned scenario,unsigned arm,unsigned pass,
    unsigned warmup,unsigned blocks) {
    unsigned unison=scenario>=5?1u<<(scenario-4):1u;
    if(limit%unison) return 0;
    ma_voice_pool_config c=ma_voice_pool_default_config;
    c.limit=(uint8_t)limit;c.unison_count=(uint8_t)unison;
    ma_patch p=*patch;
    p.bcs_amount=bcs?ma_patch_granica.bcs_amount:0;
    p.bcs_regime=ma_patch_granica.bcs_regime;
    if(!ma_voice_pool_init(&pool,RATE,&c,&p)||!populate(limit,unison,scenario)) exit(1);
    arm_effects(arm);
    unsigned count=scenario==0?0:scenario==1?1:scenario==2?8:limit;
    release_setup(scenario,warmup,blocks,count);
    ma_frame out[MAX_FRAMES]; double *cpu=calloc(blocks,sizeof *cpu),*wall=calloc(blocks,sizeof *wall);
    if(!cpu||!wall) exit(1);
    struct rusage before={0},after={0}; getrusage(RUSAGE_SELF,&before);
    uint64_t hash=0;
    for(unsigned b=0;b<warmup+blocks;b++) {
        if(scenario==4&&b==warmup+blocks/2) ma_voice_pool_set_sustain(&pool,0,false);
        struct timespec ws=clock_now(CLOCK_MONOTONIC),cs=clock_now(CLOCK_THREAD_CPUTIME_ID);
        ma_voice_pool_render(&pool,out,frames);
        struct timespec ce=clock_now(CLOCK_THREAD_CPUTIME_ID),we=clock_now(CLOCK_MONOTONIC);
        if(b>=warmup){cpu[b-warmup]=us(cs,ce);wall[b-warmup]=us(ws,we);}
        for(unsigned i=0;i<frames;i++) {
            if(!isfinite(out[i].left)||!isfinite(out[i].right)||fabsf(out[i].left)>1||fabsf(out[i].right)>1) exit(1);
            hash=tw_fnv1a64(&out[i],sizeof out[i],hash);
        }
    }
    getrusage(RUSAGE_SELF,&after);
    qsort(cpu,blocks,sizeof *cpu,cmp);qsort(wall,blocks,sizeof *wall,cmp);
    double sum=0,wall_sum=0;for(unsigned i=0;i<blocks;i++){sum+=cpu[i];wall_sum+=wall[i];}
    printf("%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%ld,%ld,%ld,%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%" PRIu64 ",%" PRIu64 "\n",
        patch_name,bcs,limit,frames,scenario,arm==0?"dry":arm==1?"chorus":arm==2?"reverb":"wet",
        pass,unison,pool.diagnostics.active_slots,pool.diagnostics.peak_slots,
        sum/blocks,cpu[blocks/2],cpu[blocks*99/100],cpu[blocks-1],
        wall_sum/blocks,wall[blocks/2],wall[blocks*99/100],wall[blocks-1],
        before.ru_maxrss,after.ru_maxrss,after.ru_minflt-before.ru_minflt,
        hash,pool.effects.pre_peak?tw_fnv1a64(&pool.effects.pre_peak,sizeof pool.effects.pre_peak,0):0,
        pool.effects.post_peak?tw_fnv1a64(&pool.effects.post_peak,sizeof pool.effects.post_peak,0):0,
        pool.diagnostics.steals_released,pool.effects.sanitizations);
    free(cpu);free(wall);return hash;
}
int main(int argc,char **argv) {
    if(argc!=2||strcmp(argv[1],"--run")){puts("Usage: bench_ma3_perf --run > ma3-perf.csv\nRun alone under taskset -c N. Filters: MA3_PERF_PATCH, _BCS, _LIMIT, _FRAMES, _SCENARIO, _ARM.");return argc==1?0:1;}
    cpu_set_t set;CPU_ZERO(&set);if(sched_getaffinity(0,sizeof set,&set)){perror("sched_getaffinity");return 1;}
    struct utsname host;if(uname(&host))return 1;
    unsigned warmup=env_u("MA3_PERF_WARMUP",DEFAULT_WARMUP),blocks=env_u("MA3_PERF_BLOCKS",DEFAULT_BLOCKS);
    if(!blocks||blocks>4096||warmup>4096)return 2;
    printf("# host=%s kernel=%s arch=%s compiler=%s rate=%u warmup=%u blocks=%u pool_bytes=%zu effects_bytes=%zu affinity=",host.nodename,host.release,host.machine,__VERSION__,RATE,warmup,blocks,sizeof pool,sizeof pool.effects);
    for(unsigned i=0;i<CPU_SETSIZE;i++) {
        if(CPU_ISSET(i,&set)) printf("%u;",i);
    }
    puts("");
    puts("patch,bcs,limit,frames,scenario,arm,pass,unison,active,peak,cpu_mean_us,cpu_p50_us,cpu_p99_us,cpu_max_us,wall_edge_mean_us,wall_p50_us,wall_p99_us,wall_max_us,rss_before_kib,rss_after_kib,minor_faults,pcm_hash,effect_pre_fingerprint,effect_post_fingerprint,released_steals,sanitizations");
    const ma_patch *patches[]={&ma_patch_tepih,&ma_patch_lead,&ma_patch_raster,&ma_patch_prizma,&ma_patch_granica};
    const char *names[]={"Tepih","Lead","Raster","Prizma","Granica"};
    const char *pf=getenv("MA3_PERF_PATCH"),*bf=getenv("MA3_PERF_BCS"),*lf=getenv("MA3_PERF_LIMIT"),*ff=getenv("MA3_PERF_FRAMES"),*sf=getenv("MA3_PERF_SCENARIO"),*af=getenv("MA3_PERF_ARM");
    for(unsigned p=0;p<5;p++)if(selected(pf,names[p]))for(unsigned bcs=0;bcs<2;bcs++)if(!bf||bcs==(unsigned)strtoul(bf,0,10))for(unsigned limit=16;limit<=64;limit*=2)if(!lf||limit==(unsigned)strtoul(lf,0,10))for(unsigned frames=128;frames<=512;frames*=2)if(!ff||frames==(unsigned)strtoul(ff,0,10))for(unsigned scenario=0;scenario<8;scenario++)if(!sf||scenario==(unsigned)strtoul(sf,0,10))for(unsigned arm=0;arm<4;arm++)if(!af||arm==(unsigned)strtoul(af,0,10)){
        if(frames>MAX_FRAMES) continue;
        uint64_t a=run_case(names[p],patches[p],bcs,limit,frames,scenario,arm,1,warmup,blocks);
        uint64_t bb=run_case(names[p],patches[p],bcs,limit,frames,scenario,arm,2,warmup,blocks);
        if(a!=bb){fputs("PCM mismatch\n",stderr);return 1;}
    }
    return 0;
}
