/* MA3-PERF: hosted timing only. No allocation, hashing or event dispatch is
 * inside a measured block. Run alone and pin externally with taskset. */
#define _GNU_SOURCE
#include "../src/mamutanalog.h"
#include <errno.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <math.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

enum { RATE=48000, DEFAULT_WARMUP=8, DEFAULT_BLOCKS=32, MAX_FRAMES=512 };
static ma_voice_pool pool;

/* Optional hardware counters (MA3_PERF_PMU=auto|required|off, default auto):
 * user-space cycles and instructions of this thread, read as one group
 * immediately around the render call, inside the clock brackets. Cycles do
 * not depend on clock frequency, so A/B screens stay comparable on a host
 * whose frequency moves; time-based deadline checks are unchanged. */
static int pmu_fd=-1,pmu_member_fd=-1;
static char pmu_note[96]="off";
typedef struct { uint64_t enabled,running,cycles,instructions; } pmu_sample;
static int pmu_mode(void) {
    const char *s=getenv("MA3_PERF_PMU");
    if(!s||!strcmp(s,"auto")) return 1;
    if(!strcmp(s,"off")) return 0;
    if(!strcmp(s,"required")) return 2;
    fputs("Invalid MA3_PERF_PMU\n",stderr); exit(2);
}
/* MA3_PERF_ENGINE=auto|scalar (default auto): the pool's render engine. */
static ma_pool_engine engine_mode(void) {
    const char *s=getenv("MA3_PERF_ENGINE");
    if(!s||!strcmp(s,"auto")) return MA_POOL_ENGINE_AUTO;
    if(!strcmp(s,"scalar")) return MA_POOL_ENGINE_SCALAR;
    fputs("Invalid MA3_PERF_ENGINE\n",stderr); exit(2);
}
static ma_pool_engine bench_engine=MA_POOL_ENGINE_AUTO;
static int pmu_event(uint64_t config,int group) {
    struct perf_event_attr a; memset(&a,0,sizeof a);
    a.type=PERF_TYPE_HARDWARE; a.size=sizeof a; a.config=config;
    a.disabled=group<0; a.exclude_kernel=1; a.exclude_hv=1;
    a.read_format=PERF_FORMAT_GROUP|PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING;
    return (int)syscall(SYS_perf_event_open,&a,0,-1,group,0UL);
}
static void pmu_open(int mode) {
    if(!mode) return;
    int leader=pmu_event(PERF_COUNT_HW_CPU_CYCLES,-1),member=-1,err=0;
    if(leader<0) err=errno;
    else if((member=pmu_event(PERF_COUNT_HW_INSTRUCTIONS,leader))<0) err=errno;
    else if(ioctl(leader,PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP)) err=errno;
    if(err) {
        if(member>=0) close(member);
        if(leader>=0) close(leader);
        snprintf(pmu_note,sizeof pmu_note,"unavailable(%s)",strerror(err));
        if(mode==2) { fprintf(stderr,"MA3_PERF_PMU=required: counters %s\n",pmu_note); exit(2); }
        return;
    }
    pmu_fd=leader; pmu_member_fd=member;
    snprintf(pmu_note,sizeof pmu_note,"cycles+instructions scope=user_thread");
}
static pmu_sample pmu_read(void) {
    pmu_sample s={0};
    if(pmu_fd<0) return s;
    uint64_t v[5];  /* nr, time_enabled, time_running, cycles, instructions */
    if(read(pmu_fd,v,sizeof v)!=(ssize_t)sizeof v||v[0]!=2) { perror("pmu read"); exit(1); }
    s.enabled=v[1]; s.running=v[2]; s.cycles=v[3]; s.instructions=v[4];
    return s;
}
static int cmp_u64(const void *a,const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return (x>y)-(x<y);
}
static unsigned env_u(const char *name, unsigned fallback) {
    const char *s=getenv(name);
    if(!s) return fallback;
    if(!*s) goto invalid;
    for(const char *p=s;*p;p++) if(*p<'0'||*p>'9') goto invalid;
    errno=0; char *end=0; unsigned long v=strtoul(s,&end,10);
    if(errno||*end||v>4096) goto invalid;
    return (unsigned)v;
invalid:
    fprintf(stderr,"Invalid %s\n",name); exit(2);
}
static int filter_u(const char *name,const unsigned *values,unsigned count) {
    const char *s=getenv(name);
    if(!s||!strcmp(s,"all")) return -1;
    unsigned v=env_u(name,0);
    for(unsigned i=0;i<count;i++) if(v==values[i]) return (int)v;
    fprintf(stderr,"Invalid %s\n",name); exit(2);
}
typedef struct { unsigned active,held,sustained,released; } occupancy;
typedef struct { occupancy before,after; } block_occupancy;
static occupancy observe(void) {
    occupancy o={0};
    for(unsigned i=0;i<pool.config.limit;i++) if(pool.slot[i].awake) {
        o.active++;
        switch(pool.group[pool.slot[i].group].phase) {
        case MA_CARD_HELD: o.held++; break;
        case MA_CARD_SUSTAINED: o.sustained++; break;
        case MA_CARD_RELEASED: o.released++; break;
        default: break;
        }
    }
    return o;
}
static const char *effect_name(unsigned arm) {
    return arm==0?"dry":arm==1?"chorus":arm==2?"reverb":"wet";
}
static unsigned rank_index(unsigned blocks,unsigned percentile) {
    return (blocks*percentile+99)/100-1;
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
    return !filter||!strcmp(filter,"all")||!strcmp(filter,name);
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
static void release_setup(unsigned scenario,unsigned count) {
    if(scenario!=4) return;
    ma_voice_pool_set_sustain(&pool,0,true);
    for(unsigned n=0;n<count;n++) (void)ma_voice_pool_note_off(&pool,0,(uint8_t)(36+n%48),0);
}
static uint64_t run_case(const char *patch_name,const ma_patch *patch,unsigned bcs,
    unsigned limit,unsigned frames,unsigned scenario,unsigned arm,unsigned pass,
    unsigned warmup,unsigned blocks,FILE *raw,unsigned case_id) {
    unsigned unison=scenario>=5?1u<<(scenario-4):1u;
    if(limit%unison) return 0;
    ma_voice_pool_config c=ma_voice_pool_default_config;
    c.limit=(uint8_t)limit;c.unison_count=(uint8_t)unison;c.engine=bench_engine;
    ma_patch p=*patch;
    p.bcs_amount=bcs?ma_patch_granica.bcs_amount:0;
    p.bcs_regime=ma_patch_granica.bcs_regime;
    if(!ma_voice_pool_init(&pool,RATE,&c,&p)||!populate(limit,unison,scenario)) exit(1);
    arm_effects(arm);
    unsigned count=scenario==0?0:scenario==1?1:scenario==2?8:limit;
    release_setup(scenario,count);
    ma_frame out[MAX_FRAMES]; double *cpu=calloc(blocks,sizeof *cpu),*wall=calloc(blocks,sizeof *wall);
    block_occupancy *occupied=calloc(blocks,sizeof *occupied);
    uint64_t *cyc=calloc(blocks,sizeof *cyc),*ins=calloc(blocks,sizeof *ins);
    unsigned char *mux=calloc(blocks,1);
    if(!cpu||!wall||!occupied||!cyc||!ins||!mux) exit(1);
    unsigned active_min=limit,active_max=0,held_max=0,sustained_max=0,released_max=0;
    unsigned cpu_misses=0,wall_misses=0,multiplexed=0;
    double deadline=(double)frames/RATE*1e6;
    struct rusage before={0},after={0}; getrusage(RUSAGE_SELF,&before);
    uint64_t hash=0;
    for(unsigned b=0;b<warmup+blocks;b++) {
        if(scenario==4&&b==warmup+blocks/2) ma_voice_pool_set_sustain(&pool,0,false);
        if(b>=warmup) occupied[b-warmup].before=observe();
        struct timespec ws=clock_now(CLOCK_MONOTONIC),cs=clock_now(CLOCK_THREAD_CPUTIME_ID);
        pmu_sample p0=pmu_read();
        ma_voice_pool_render(&pool,out,frames);
        pmu_sample p1=pmu_read();
        struct timespec ce=clock_now(CLOCK_THREAD_CPUTIME_ID),we=clock_now(CLOCK_MONOTONIC);
        if(b>=warmup) {
            unsigned n=b-warmup;
            cpu[n]=us(cs,ce);wall[n]=us(ws,we);occupied[n].after=observe();
            cpu_misses+=cpu[n]>deadline;wall_misses+=wall[n]>deadline;
            cyc[n]=p1.cycles-p0.cycles;ins[n]=p1.instructions-p0.instructions;
            mux[n]=p1.enabled-p0.enabled!=p1.running-p0.running;multiplexed+=mux[n];
            occupancy endpoints[]={occupied[n].before,occupied[n].after};
            for(unsigned j=0;j<2;j++) {
                occupancy o=endpoints[j];
                if(o.active<active_min) active_min=o.active;
                if(o.active>active_max) active_max=o.active;
                if(o.held>held_max) held_max=o.held;
                if(o.sustained>sustained_max) sustained_max=o.sustained;
                if(o.released>released_max) released_max=o.released;
            }
        }
        for(unsigned i=0;i<frames;i++) {
            if(!isfinite(out[i].left)||!isfinite(out[i].right)||fabsf(out[i].left)>1||fabsf(out[i].right)>1) exit(1);
            hash=tw_fnv1a64(&out[i],sizeof out[i],hash);
        }
    }
    getrusage(RUSAGE_SELF,&after);
    if(raw) for(unsigned n=0;n<blocks;n++) {
        occupancy a=occupied[n].before,z=occupied[n].after;
        fprintf(raw,"%ld,%u,%s,%u,%u,%u,%u,%s,%u,%u,%s,%.3f,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            (long)getpid(),case_id,patch_name,bcs,limit,frames,scenario,effect_name(arm),pass,n,
            scenario==4?(n<blocks/2?"sustain":"release"):(scenario==0?"idle":"held"),cpu[n],wall[n],
            a.active,z.active,a.held,z.held,a.sustained,z.sustained,a.released,z.released,
            cpu[n]>deadline,wall[n]>deadline);
        if(pmu_fd>=0) fprintf(raw,",%" PRIu64 ",%" PRIu64 ",%u\n",cyc[n],ins[n],mux[n]);
        else fputs(",,,\n",raw);
    }
    uint64_t ladder_resets=0;
    for(unsigned i=0;i<limit;i++) ladder_resets+=pool.slot[i].voice.ladder.reset_count;
    qsort(cpu,blocks,sizeof *cpu,cmp);qsort(wall,blocks,sizeof *wall,cmp);
    qsort(cyc,blocks,sizeof *cyc,cmp_u64);qsort(ins,blocks,sizeof *ins,cmp_u64);
    double sum=0,wall_sum=0,cyc_sum=0,ins_sum=0;
    for(unsigned i=0;i<blocks;i++){sum+=cpu[i];wall_sum+=wall[i];cyc_sum+=(double)cyc[i];ins_sum+=(double)ins[i];}
    printf("%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%ld,%ld,%ld,%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%" PRIu64 ",%" PRIu64 ",%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f,%u,%u,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64,
        patch_name,bcs,limit,frames,scenario,effect_name(arm),
        pass,unison,pool.diagnostics.active_slots,pool.diagnostics.peak_slots,
        sum/blocks,cpu[rank_index(blocks,50)],cpu[rank_index(blocks,99)],cpu[blocks-1],
        wall_sum/blocks,wall[rank_index(blocks,50)],wall[rank_index(blocks,99)],wall[blocks-1],
        before.ru_maxrss,after.ru_maxrss,after.ru_minflt-before.ru_minflt,
        hash,pool.effects.pre_peak?tw_fnv1a64(&pool.effects.pre_peak,sizeof pool.effects.pre_peak,0):0,
        pool.effects.post_peak?tw_fnv1a64(&pool.effects.post_peak,sizeof pool.effects.post_peak,0):0,
        pool.diagnostics.steals_released,pool.effects.sanitizations,
        case_id,warmup,blocks,active_min,active_max,held_max,sustained_max,released_max,
        pool.diagnostics.active_slots,deadline,cpu_misses,wall_misses,
        pool.effects.clips,pool.output.diagnostics.sanitization_count,
        pool.output.diagnostics.knee_hit_count,pool.output.diagnostics.tiny_flush_count,
        pool.diagnostics.sleeps,ladder_resets);
    if(pmu_fd>=0) printf(",%.1f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f,%" PRIu64 ",%" PRIu64 ",%u\n",
        cyc_sum/blocks,cyc[rank_index(blocks,50)],cyc[rank_index(blocks,99)],cyc[blocks-1],
        ins_sum/blocks,ins[0],ins[blocks-1],multiplexed);
    else puts(",,,,,,,,");
    free(cpu);free(wall);free(occupied);free(cyc);free(ins);free(mux);return hash;
}
int main(int argc,char **argv) {
    if(argc!=2||(strcmp(argv[1],"--run")&&strcmp(argv[1],"--list")&&strcmp(argv[1],"--overhead"))) {
        puts("Usage: bench_ma3_perf --run|--list|--overhead\nRun alone under taskset -c N. Filters: MA3_PERF_PATCH, _BCS, _LIMIT, _FRAMES, _SCENARIO, _ARM.\nOptional measured block CSV: MA3_PERF_RAW. Run label: MA3_PERF_RUN_ID.\nHardware counters: MA3_PERF_PMU=auto|required|off (default auto).\nPool engine: MA3_PERF_ENGINE=auto|scalar (default auto).");
        return argc==1?0:1;
    }
    unsigned warmup=env_u("MA3_PERF_WARMUP",DEFAULT_WARMUP),blocks=env_u("MA3_PERF_BLOCKS",DEFAULT_BLOCKS);
    if(!blocks) { fputs("MA3_PERF_BLOCKS must be nonzero\n",stderr);return 2; }
    int pmu=pmu_mode();
    bench_engine=engine_mode();
    const ma_patch *patches[]={&ma_patch_tepih,&ma_patch_lead,&ma_patch_raster,&ma_patch_prizma,&ma_patch_granica};
    const char *names[]={"Tepih","Lead","Raster","Prizma","Granica"};
    const char *pf=getenv("MA3_PERF_PATCH");
    unsigned pairs[]={0,1},limits[]={8,16,32,64},sizes[]={128,256,512},scenarios[]={0,1,2,3,4,5,6,7},arms[]={0,1,2,3};
    int bf=filter_u("MA3_PERF_BCS",pairs,2),lf=filter_u("MA3_PERF_LIMIT",limits,4),
        ff=filter_u("MA3_PERF_FRAMES",sizes,3),sf=filter_u("MA3_PERF_SCENARIO",scenarios,8),
        af=filter_u("MA3_PERF_ARM",arms,4);
    unsigned patch_count=0;
    for(unsigned p=0;p<5;p++) patch_count+=selected(pf,names[p]);
    unsigned cases=patch_count*(bf<0?2:1)*(lf<0?4:1)*(ff<0?3:1)*(sf<0?8:1)*(af<0?4:1);
    if(!cases) { fputs("No cases selected: invalid MA3_PERF_PATCH\n",stderr);return 2; }
    printf("# filters patch=%s bcs=%d limit=%d frames=%d scenario=%d arm=%d cases=%u rows=%u; -1=all\n",
        pf?pf:"all",bf,lf,ff,sf,af,cases,cases*2);
    if(!strcmp(argv[1],"--list")) return fflush(stdout)||ferror(stdout)?1:0;
    cpu_set_t set;CPU_ZERO(&set);if(sched_getaffinity(0,sizeof set,&set)){perror("sched_getaffinity");return 1;}
    struct utsname host;if(uname(&host))return 1;
    printf("# host=%s kernel=%s arch=%s compiler=%s rate=%u warmup=%u blocks=%u pool_bytes=%zu effects_bytes=%zu pid=%ld run_id=%s affinity=",
        host.nodename,host.release,host.machine,__VERSION__,RATE,!strcmp(argv[1],"--overhead")?0:warmup,blocks,sizeof pool,sizeof pool.effects,
        (long)getpid(),getenv("MA3_PERF_RUN_ID")?getenv("MA3_PERF_RUN_ID"):"unspecified");
    for(unsigned i=0;i<CPU_SETSIZE;i++) if(CPU_ISSET(i,&set)) printf("%u;",i);
    puts("");
    pmu_open(pmu);
    printf("# pmu=%s; clock_order=wall_cpu_pmu_render_pmu_cpu_wall\n",pmu_note);
    {
        ma_voice_pool_config probe=ma_voice_pool_default_config;probe.engine=bench_engine;
        if(!ma_voice_pool_init(&pool,RATE,&probe,&ma_patch_tepih)) return 1;
        printf("# engine=%s; vector_lanes=%s\n",bench_engine==MA_POOL_ENGINE_AUTO?"auto":"scalar",
            pool.lanes_enabled?"active":"off");
    }
    if(strcmp(argv[1],"--overhead")) puts("# boundary=render_only; hash_includes_warmup; quantile=nearest_rank; occupancy=block_endpoints; diagnostics=cumulative_including_warmup; passes=in_process_determinism_repeats");
    if(!strcmp(argv[1],"--overhead")) {
        puts("# boundary=empty_bracket; warmup_ignored=true; clock_order=wall_cpu_pmu_pmu_cpu_wall; no_overhead_subtraction");
        static double cpu[4096],wall[4096]; static uint64_t cyc[4096],ins[4096];
        for(unsigned n=0;n<blocks;n++) {
            struct timespec ws=clock_now(CLOCK_MONOTONIC),cs=clock_now(CLOCK_THREAD_CPUTIME_ID);
            pmu_sample p0=pmu_read(),p1=pmu_read();
            struct timespec ce=clock_now(CLOCK_THREAD_CPUTIME_ID),we=clock_now(CLOCK_MONOTONIC);
            cpu[n]=us(cs,ce);wall[n]=us(ws,we);
            cyc[n]=p1.cycles-p0.cycles;ins[n]=p1.instructions-p0.instructions;
        }
        puts("block,cpu_us,wall_us,cycles,instructions");
        for(unsigned n=0;n<blocks;n++) {
            printf("%u,%.3f,%.3f",n,cpu[n],wall[n]);
            if(pmu_fd>=0) printf(",%" PRIu64 ",%" PRIu64 "\n",cyc[n],ins[n]); else puts(",,");
        }
        return fflush(stdout)||ferror(stdout)?1:0;
    }
    FILE *raw=0;
    const char *raw_path=getenv("MA3_PERF_RAW");
    if(raw_path) {
        if(!*raw_path) { fputs("Empty MA3_PERF_RAW\n",stderr);return 2; }
        raw=fopen(raw_path,"wx");if(!raw){perror(raw_path);return 1;}
        fprintf(raw,"# warmup=%u blocks=%u rate=%u run_id=%s\n",warmup,blocks,RATE,
            getenv("MA3_PERF_RUN_ID")?getenv("MA3_PERF_RUN_ID"):"unspecified");
        fputs("pid,case_id,patch,bcs,limit,frames,scenario,arm,pass,block,phase,cpu_us,wall_us,active_before,active_after,held_before,held_after,sustained_before,sustained_after,released_before,released_after,cpu_miss,wall_miss,cycles,instructions,pmu_multiplexed\n",raw);
    }
    puts("patch,bcs,limit,frames,scenario,arm,pass,unison,active,peak,cpu_mean_us,cpu_p50_us,cpu_p99_us,cpu_max_us,wall_edge_mean_us,wall_p50_us,wall_p99_us,wall_max_us,rss_before_kib,rss_after_kib,minor_faults,pcm_hash,effect_pre_fingerprint,effect_post_fingerprint,released_steals,sanitizations,case_id,warmup,blocks,active_min,active_max,held_max,sustained_max,released_max,final_active,deadline_us,cpu_misses,wall_misses,effect_clips,output_sanitizations,output_knee_hits,output_tiny_flushes,sleeps,ladder_resets,cycles_mean,cycles_p50,cycles_p99,cycles_max,instructions_mean,instructions_min,instructions_max,pmu_multiplexed_blocks");
    unsigned case_id=0;
    for(unsigned p=0;p<5;p++) if(selected(pf,names[p]))
    for(unsigned bcs=0;bcs<2;bcs++) if(bf<0||bcs==(unsigned)bf)
    for(unsigned limit=8;limit<=64;limit*=2) if(lf<0||limit==(unsigned)lf)
    for(unsigned frames=128;frames<=512;frames*=2) if(ff<0||frames==(unsigned)ff)
    for(unsigned scenario=0;scenario<8;scenario++) if(sf<0||scenario==(unsigned)sf)
    for(unsigned arm=0;arm<4;arm++) if(af<0||arm==(unsigned)af) {
        case_id++;
        uint64_t a=run_case(names[p],patches[p],bcs,limit,frames,scenario,arm,1,warmup,blocks,raw,case_id);
        uint64_t b=run_case(names[p],patches[p],bcs,limit,frames,scenario,arm,2,warmup,blocks,raw,case_id);
        if(a!=b){fputs("PCM mismatch\n",stderr);return 1;}
    }
    if(raw) {
        bool failed=ferror(raw);
        if(fclose(raw)||failed) { fputs("raw output failure\n",stderr);return 1; }
    }
    if(fflush(stdout)||ferror(stdout)) return 1;
    return case_id==cases?0:1;
}
