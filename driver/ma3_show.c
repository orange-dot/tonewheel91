#include "ma3_show.h"
#include "wav.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BLOCK=128, OUTPUTS=MA3_STEMS+1 };
static ma_voice_pool pools[MA3_PARTS];
typedef struct { uint64_t hash; double square; float peak; } level;

void ma3_part_init(ma3_part *p, const char *name, ma_patch patch,
    unsigned limit, unsigned unison, unsigned stem, float gain, float chorus_mix, float reverb_mix) {
    *p=(ma3_part){.name=name, .patch=patch, .config=ma_voice_pool_default_config,
        .chorus={chorus_mix>0,chorus_mix,.35f,.25f},
        .reverb={reverb_mix>0,reverb_mix,.65f,.55f},
        .gain=gain,.character=.20f,.stem=stem};
    p->config.limit=(uint8_t)limit; p->config.unison_count=(uint8_t)unison;
    p->config.session_seed ^= tw_fnv1a64(name,strlen(name),0);
}
bool ma3_add_event(ma3_show *s, ma3_event e) {
    if (s->event_count==MA3_EVENTS) return false;
    e.order=(unsigned)s->event_count;
    s->event[s->event_count++]=e;
    return true;
}
static int compare(const void *a,const void *b) {
    const ma3_event *x=a,*y=b;
    if(x->frame!=y->frame) return x->frame<y->frame?-1:1;
    if(x->on!=y->on) return x->on?1:-1;
    return (x->order>y->order)-(x->order<y->order);
}
bool ma3_check(ma3_show *s) {
    if (!s->part_count || s->part_count>MA3_PARTS || !s->stem_count
        || s->stem_count>MA3_STEMS || !s->seconds || s->seconds>600) return false;
    unsigned held[MA3_PARTS][16][128]={0}, active[MA3_PARTS]={0}, peak[MA3_PARTS]={0}, notes[MA3_PARTS]={0};
    qsort(s->event,s->event_count,sizeof *s->event,compare);
    for (size_t i=0;i<s->event_count;i++) {
        ma3_event e=s->event[i];
        if(e.part>=s->part_count || e.channel>=16 || e.note>=128 || e.velocity>=128
            || e.frame >= (size_t)s->seconds*MA3_RATE || (e.on&&!e.velocity)) return false;
        unsigned *n=&held[e.part][e.channel][e.note];
        if(e.on) { (*n)++; notes[e.part]++; active[e.part]++;
            if(active[e.part]>peak[e.part]) peak[e.part]=active[e.part]; }
        else {if(!*n) return false; (*n)--; active[e.part]--;}
    }
    printf("%s: %zu events, %u seconds, %u pools\n",s->name,s->event_count,s->seconds,s->part_count);
    for(unsigned p=0;p<s->part_count;p++) {
        const ma3_part *part=&s->part[p];
        if(active[p] || part->stem>=s->stem_count || !(part->gain>=0&&part->gain<=16)
            || !ma_voice_pool_init(&pools[p],MA3_RATE,&part->config,&part->patch)
            || peak[p]*part->config.unison_count>part->config.limit) return false;
        printf("  %-12s %u notes, held peak %u, slots %u, unison %u, chorus %.2f reverb %.2f\n",
            part->name,notes[p],peak[p],part->config.limit,part->config.unison_count,
            (double)part->chorus.mix,(double)part->reverb.mix);
    }
    return true;
}
static bool pass(const ma3_show *s, unsigned seconds, wav_f32_writer *writers, level *levels) {
    for(unsigned p=0;p<s->part_count;p++) {
        const ma3_part *part=&s->part[p];
        if(!ma_voice_pool_init(&pools[p],MA3_RATE,&part->config,&part->patch)) return false;
        ma_voice_pool_set_character(&pools[p],part->character);
        ma_effects_set_chorus(&pools[p].effects,part->chorus);
        ma_effects_set_reverb(&pools[p].effects,part->reverb);
    }
    size_t next=0, frames=(size_t)seconds*MA3_RATE;
    unsigned aggregate_peak=0;
    for(size_t at=0;at<frames;) {
        size_t count=frames-at; if(count>BLOCK) count=BLOCK;
        float pcm[OUTPUTS][2*BLOCK]={0};
        for(size_t i=0;i<count;i++) {
            size_t frame=at+i;
            if(s->controls && frame%480==0) s->controls(pools,s,frame);
            while(next<s->event_count && s->event[next].frame==frame) {
                ma3_event e=s->event[next++]; ma_voice_pool *p=&pools[e.part];
                if(e.on) {
                    if(!ma_voice_pool_note_on(p,e.channel,e.note,e.velocity)) return false;
                    ma_voice_pool_set_poly_pressure(p,e.channel,e.note,e.pressure);
                } else if(!ma_voice_pool_note_off(p,e.channel,e.note,0)) return false;
            }
            ma_frame stems[MA3_STEMS]={0}; unsigned active=0;
            for(unsigned p=0;p<s->part_count;p++) {
                ma_frame f=ma_voice_pool_tick(&pools[p]);
                const ma3_part *part=&s->part[p];
                stems[part->stem].left+=f.left*part->gain;
                stems[part->stem].right+=f.right*part->gain;
                active+=pools[p].diagnostics.active_slots;
            }
            if(active>aggregate_peak) aggregate_peak=active;
            /* One presentation fade on every stem; no limiter or gain riding. */
            size_t end=(size_t)s->seconds*MA3_RATE;
            float fade=frame<MA3_RATE/2 ? (float)frame/(MA3_RATE/2) : 1;
            if(frame>=end-2*MA3_RATE) fade=(float)(end-1-frame)/(2*MA3_RATE-1);
            ma_frame mix={0};
            for(unsigned st=0;st<s->stem_count;st++) {
                ma_frame f={stems[st].left*fade,stems[st].right*fade};
                pcm[st][2*i]=f.left;pcm[st][2*i+1]=f.right;
                mix.left+=f.left;mix.right+=f.right;
            }
            pcm[s->stem_count][2*i]=mix.left;pcm[s->stem_count][2*i+1]=mix.right;
        }
        for(unsigned st=0;st<=s->stem_count;st++) {
            level *l=&levels[st];
            for(size_t i=0;i<2*count;i++) {
                float x=pcm[st][i];
                if(!isfinite(x)||fabsf(x)>1) return false;
                if(fabsf(x)>l->peak) l->peak=fabsf(x);
                l->square+=(double)x*x;
            }
            l->hash=tw_fnv1a64(pcm[st],2*count*sizeof(float),l->hash);
            if(writers && wav_f32_write(&writers[st],pcm[st],count)) return false;
        }
        at+=count;
        if(at%(30*MA3_RATE)==0) {printf("  rendered %zu/%u s\n",at/MA3_RATE,seconds);fflush(stdout);}
    }
    printf("  aggregate active peak %u (limits apply separately to each timbre)\n",aggregate_peak);
    for(unsigned p=0;p<s->part_count;p++) {
        ma_pool_diagnostics d=pools[p].diagnostics;
        printf("  %-12s active %u peak %u steals R/S/H=%" PRIu64 "/%" PRIu64 "/%" PRIu64
            " sleeps=%" PRIu64 " FX clips=%" PRIu64 "\n",s->part[p].name,
            d.active_slots,d.peak_slots,d.steals_released,d.steals_sustained,d.steals_held,
            d.sleeps,pools[p].effects.clips);
        if(d.steals_held || d.steals_sustained || pools[p].effects.sanitizations) return false;
        if(seconds==s->seconds && d.active_slots) return false;
    }
    return true;
}
bool ma3_render(const ma3_show *s,const char *prefix,unsigned seconds) {
    if(!seconds) seconds=s->seconds;
    if(seconds>s->seconds || !prefix || !*prefix) return false;
    level first[OUTPUTS]={0}, second[OUTPUTS]={0};
    wav_f32_writer writers[OUTPUTS]={0};
    char temporary[OUTPUTS][1024]={{0}}, paths[OUTPUTS][1024]={{0}};
    for(unsigned st=0;st<=s->stem_count;st++) {
        const char *name=st==s->stem_count?"mix":s->stem_name[st];
        int n=snprintf(paths[st],sizeof paths[st],"%s_%s.wav",prefix,name);
        int m=snprintf(temporary[st],sizeof temporary[st],"%s_%s.wav.ma-tmp",prefix,name);
        if(n<0||m<0||(size_t)n>=sizeof paths[st]||(size_t)m>=sizeof temporary[st]) return false;
    }
    printf("  reference pass\n");fflush(stdout);
    if(!pass(s,seconds,NULL,first)) return false;
    bool ok=true;
    for(unsigned st=0;st<=s->stem_count;st++)
        if(wav_f32_open(&writers[st],temporary[st],(size_t)seconds*MA3_RATE,MA3_RATE,2)) {ok=false;break;}
    if(ok) {puts("  output pass");fflush(stdout);ok=pass(s,seconds,writers,second);}
    for(unsigned st=0;st<=s->stem_count;st++)
        if(first[st].hash!=second[st].hash) ok=false;
    for(unsigned st=0;st<=s->stem_count;st++) {
        if(ok) {if(wav_f32_close(&writers[st])) ok=false;}
        else wav_f32_abort(&writers[st]);
    }
    if(!ok) {for(unsigned st=0;st<=s->stem_count;st++) if(temporary[st][0]) remove(temporary[st]);return false;}
    for(unsigned st=0;st<=s->stem_count;st++) {
        if(rename(temporary[st],paths[st])) return false;
        printf("  %s peak=%.9f rms=%.9f FNV64=%016" PRIx64 " two runs identical\n",
            paths[st],(double)second[st].peak,sqrt(second[st].square/(2.0*seconds*MA3_RATE)),second[st].hash);
    }
    return true;
}
