/* Full vocal and all backing roles on Mamut pools, including former organs. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#include "ma3_hurt_material.h"
#include "ma3_hurt_parts.h"

static ma3_show show;
static performance_score score;
static void controls(ma_voice_pool *p,const ma3_show *s,size_t frame) {
    float t=(float)frame/(float)tick_frame(BARS*TICKS_PER_BAR);if(t>1)t=1;
    float progress=t*t*(3-2*t);
    float energy=.5f-.5f*cosf(6.28318530718f*t);
    ma_voice_pool_set_macro(&p[PAD],MA_MACRO_BLOOM,.06f+.10f*energy);
    ma_voice_pool_set_macro(&p[TEXTURE],MA_MACRO_SWARM,.03f+.07f*energy);
    ma_voice_pool_set_lfo(&p[PAD],.008f,.08f);
    ma_voice_pool_set_lfo(&p[TEXTURE],.006f,.11f);
    ma_patch vocal=s->part[VOCAL].patch;
    vocal.filter_cutoff_hz=850-610*progress;
    vocal.filter_resonance=.16f+.05f*progress;
    vocal.filter_drive=.16f+.05f*progress;
    vocal.mixer_pressure=.14f+.04f*progress;
    ma_voice_pool_set_patch(&p[VOCAL],&vocal);
    ma_voice_pool_set_channel_pressure(&p[VOCAL],0,.10f+.16f*energy);
    ma_voice_pool_set_mod_wheel(&p[MELODY],0,.08f+.10f*energy);
}
int main(int argc,char **argv) {
    const char *input="notes-midi/local/nine-inch-nails-hurt.mid";
    const char *vocal="notes-midi/local/hurt-songparts-vocals.mid",*prefix=NULL;
    unsigned seconds=0;bool check=false;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--render")&&i+1<argc) prefix=argv[++i];
        else if(!strcmp(argv[i],"-i")&&i+1<argc) input=argv[++i];
        else if(!strcmp(argv[i],"-v")&&i+1<argc) vocal=argv[++i];
        else if(!strcmp(argv[i],"--seconds")&&i+1<argc) {
            uint64_t n=0;if(!host_parse_u64(argv[++i],1,253,&n))return 2;seconds=(unsigned)n;
        } else if(!strcmp(argv[i],"--check")) check=true;
        else {
            fprintf(stderr,"Usage: %s --check | --render PREFIX [--seconds 1..253] [-i backing.mid] [-v vocal.mid]\n",argv[0]);return 2;
        }
    }
    if((check&&prefix)||(seconds&&!prefix)) return 2;
    const char *reason=NULL;
    bool ok=midi_load(&score.midi,&score.bytes,input,UINT16_MAX,&reason)&&score_validate(&score,&reason)
        &&midi_load(&score.vocal,&score.vocal_bytes,vocal,1,&reason)
        &&arrange(&score)&&check_arrangement(&score);
    show=(ma3_show){.name="MA3 Hurt all Mamut",.part_count=PART_COUNT,.stem_count=5,.seconds=253,
        .stem_name={"tonal","rhythm","melody","vocal","harmony"},.controls=controls};
    hurt_parts_init(&show);
    for(size_t i=0;ok&&i<score.count;i++) {
        arrangement_event e=score.event[i];
        ok=ma3_add_event(&show,(ma3_event){.frame=tick_frame(e.tick),.part=(uint8_t)e.part,
            .note=e.note,.velocity=e.velocity,.on=e.on,.pressure=e.part==VOCAL?.22f:.12f});
    }
    if(ok)ok=ma3_check(&show);
    if(ok&&prefix)ok=ma3_render(&show,prefix,seconds);
    if(!ok)fprintf(stderr,"Hurt: %s\n",reason?reason:"score/render validation failed");
    score_dispose(&score);
    return ok?0:1;
}
