/* Pool-backed revision of the original F# minor Blues performance study. */
#include "ma3_show.h"
#include "ma_dark_lead.h"
#include "host_parse.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ma3_blues_material.h"

static ma3_show show;
static void controls(ma_voice_pool *p,const ma3_show *s,size_t frame) {
    float seconds=(float)frame/MA3_RATE;
    float arc=.5f-.5f*sinf(6.28318530718f*(seconds/234+.25f));
    ma_voice_pool_set_macro(&p[ROLE_PAD_DARK],MA_MACRO_BLOOM,.08f+.12f*arc);
    ma_voice_pool_set_macro(&p[ROLE_PAD_HAZE],MA_MACRO_SWARM,.05f+.10f*arc);
    ma_voice_pool_set_lfo(&p[ROLE_PAD_HAZE],.008f,.09f);
    ma_voice_pool_set_channel_pressure(&p[ROLE_BASS_MOTION],0,.20f+.15f*arc);
    for(unsigned i=0;i<sizeof LEAD_EVENTS/sizeof *LEAD_EVENTS;i++) {
        const note_event *e=&LEAD_EVENTS[i];
        float age=seconds-e->at_seconds;
        if(age<0) continue;
        float entry=age<1.8f?-.12f*(1-age/1.8f):0;
        float onset=age<2.2f?0:age<3.6f?(age-2.2f)/1.4f:1;
        float vibrato=onset*(.012f+.020f*e->pressure)*sinf(6.28318530718f*3.65f*age);
        ma_voice_pool_set_pitch_bend(&p[ROLE_LEAD],(uint8_t)i,entry+vibrato);
    }
    (void)s;
}
static bool add(const note_event *notes,size_t count,unsigned part) {
    for(size_t i=0;i<count;i++) {
        const note_event *e=&notes[i];
        ma3_event n={.frame=(size_t)(e->at_seconds*MA3_RATE+.5f),.part=(uint8_t)part,
            .channel=part==ROLE_LEAD?(uint8_t)i:0,.note=e->note,.velocity=e->velocity,
            .pressure=e->pressure,.on=true};
        if(!ma3_add_event(&show,n)) return false;
        n.frame=(size_t)((e->at_seconds+e->held_seconds)*MA3_RATE+.5f);
        n.on=false;n.velocity=0;
        if(!ma3_add_event(&show,n)) return false;
    }
    return true;
}
int main(int argc,char **argv) {
    const char *prefix=NULL;unsigned seconds=0;bool check=false;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--render")&&i+1<argc) prefix=argv[++i];
        else if(!strcmp(argv[i],"--seconds")&&i+1<argc) {
            uint64_t n=0;if(!host_parse_u64(argv[++i],1,248,&n)) return 2;seconds=(unsigned)n;
        } else if(!strcmp(argv[i],"--check")) check=true;
        else {
            fprintf(stderr,"Usage: %s --check | --render PREFIX [--seconds 1..248]\n",argv[0]);return 2;
        }
    }
    if((check&&prefix)||(seconds&&!prefix)) return 2;
    show=(ma3_show){.name="MA3 Blade Runner Blues",.part_count=5,.stem_count=3,.seconds=248,
        .stem_name={"pads","bass","lead"},.controls=controls};
    const char *names[]={"dark-pad","haze","root-bass","motion-bass","lead"};
    const unsigned limits[]={32,16,4,8,8},unison[]={2,2,1,1,2},stem[]={0,0,1,1,2};
    const float gain[]={1.6f,1.0f,2.6f,2.0f,3.8f},ch[]={.30f,.34f,0,.10f,.16f},rv[]={.28f,.32f,.08f,.15f,.28f};
    for(unsigned p=0;p<5;p++) {
        ma_patch patch=performance_patch((voice_role)p);
        if(p==ROLE_BASS_ROOT) patch.width=0;
        if(p==ROLE_LEAD) {patch.width=.30f;patch.amp_adsr.attack_ms=360;}
        ma3_part_init(&show.part[p],names[p],patch,limits[p],unison[p],stem[p],gain[p],ch[p],rv[p]);
        show.part[p].config.detune_cents=p==ROLE_LEAD?3.5f:5.5f;
    }
    bool ok=add(PAD_DARK_EVENTS,sizeof PAD_DARK_EVENTS/sizeof *PAD_DARK_EVENTS,ROLE_PAD_DARK)
        &&add(PAD_HAZE_EVENTS,sizeof PAD_HAZE_EVENTS/sizeof *PAD_HAZE_EVENTS,ROLE_PAD_HAZE)
        &&add(BASS_ROOT_EVENTS,sizeof BASS_ROOT_EVENTS/sizeof *BASS_ROOT_EVENTS,ROLE_BASS_ROOT)
        &&add(BASS_MOTION_EVENTS,sizeof BASS_MOTION_EVENTS/sizeof *BASS_MOTION_EVENTS,ROLE_BASS_MOTION)
        &&add(LEAD_EVENTS,sizeof LEAD_EVENTS/sizeof *LEAD_EVENTS,ROLE_LEAD)&&ma3_check(&show);
    if(ok&&prefix) ok=ma3_render(&show,prefix,seconds);
    return ok?0:1;
}
