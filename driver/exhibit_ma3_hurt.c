/* Full vocal and all backing roles on Mamut pools, including former organs. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#include "ma3_hurt_material.h"

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
static ma_patch voice_patch(unsigned part) {
    ma_patch p;
    switch(part) {
    case PLUCK:
        p=dark_patch(pluck_patch(0));p.raster_mix=.07f;p.raster_position=.24f;p.raster_warp=.05f;return p;
    case PAD:
        p=pad_patch(0,.12f,.32f,.08f);
        p.amp_adsr=(ma_adsr){700,2000,.78f,3000};p.filter_cutoff_hz=680;return p;
    case BASS:
        p=dark_patch(bass_patch());p.amp_adsr=(ma_adsr){45,500,.60f,700};
        p.filter_adsr=(ma_adsr){35,500,.20f,650};p.width=0;p.bcs_amount=.20f;return p;
    case TENOR: p=dark_patch(tenor_patch(0));p.mozaik_mix=.035f;return p;
    case TEXTURE:
        p=pulse_patch(0,.16f,.44f,.12f);p.filter_cutoff_hz=480;
        p.amp_adsr=(ma_adsr){230,650,.38f,1800};p.filter_adsr=(ma_adsr){180,800,.20f,1600};return p;
    case MELODY:
        p=ma_dark_lead_patch();p.amp_adsr=(ma_adsr){80,550,.74f,1700};
        p.filter_adsr=(ma_adsr){65,750,.38f,1800};p.filter_cutoff_hz=980;
        p.vco1.saw_level=.12f;p.bcs_amount=.12f;p.bcs_regime=.28f;p.width=.32f;return p;
    case HARMONY:
        p=ma_patch_dubina;p.filter_cutoff_hz=640;p.width=.88f;
        p.amp_adsr=(ma_adsr){850,1800,.70f,4400};p.master_level=.12f;return p;
    case KICK: case PULSE:return drum_patch((part_id)part);
    default:
        p=dark_patch(tenor_patch(-3));
        p.vco1.saw_level=.16f;p.vco1.triangle_level=.50f;p.vco1.sine_level=.34f;
        p.vco2.saw_level=.04f;p.vco2.triangle_level=.40f;p.vco2.sine_level=.38f;
        p.vco2_level=.38f;p.amp_adsr=(ma_adsr){28,220,.72f,420};
        p.filter_adsr=(ma_adsr){18,320,.30f,500};p.filter_cutoff_hz=850;
        p.filter_resonance=.16f;p.filter_drive=.16f;p.filter_env_amount=.18f;
        p.bcs_amount=.18f;p.bcs_regime=.27f;p.body_drive=.14f;p.master_level=.14f;
        p.width=.18f;return p;
    }
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
    const unsigned limits[]={16,24,4,8,16,8,4,4,8,24};
    const unsigned unison[]={1,2,1,1,2,2,1,1,1,4};
    const unsigned stems[]={0,0,0,0,0,2,1,1,3,4};
    const float gain[]={.9f,.9f,2.8f,1.0f,.7f,1.5f,1.1f,1.7f,1.8f,1.0f};
    const float ch[]={.12f,.28f,0,.14f,.22f,.16f,0,.06f,.08f,.32f};
    const float rv[]={.18f,.27f,.05f,.22f,.25f,.25f,.04f,.14f,.18f,.32f};
    for(unsigned p=0;p<PART_COUNT;p++) {
        ma3_part_init(&show.part[p],PART_NAMES[p],voice_patch(p),limits[p],unison[p],stems[p],gain[p],ch[p],rv[p]);
        show.part[p].config.detune_cents=p==MELODY?3:5;
    }
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
