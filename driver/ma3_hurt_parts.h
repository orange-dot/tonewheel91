/* Part setup shared by the MA3 Hurt exhibits: one patch, slot limit,
 * unison, stem, gain and effect mix per arrangement part. Include after
 * ma3_show.h, ma_dark_lead.h and ma3_hurt_material.h. */
#ifndef MA3_HURT_PARTS_H
#define MA3_HURT_PARTS_H

static ma_patch hurt_voice_patch(unsigned part) {
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
static void hurt_parts_init(ma3_show *show) {
    const unsigned limits[]={16,24,4,8,16,8,4,4,8,24};
    const unsigned unison[]={1,2,1,1,2,2,1,1,1,4};
    const unsigned stems[]={0,0,0,0,0,2,1,1,3,4};
    const float gain[]={.9f,.9f,2.8f,1.0f,.7f,1.5f,1.1f,1.7f,1.8f,1.0f};
    const float ch[]={.12f,.28f,0,.14f,.22f,.16f,0,.06f,.08f,.32f};
    const float rv[]={.18f,.27f,.05f,.22f,.25f,.25f,.04f,.14f,.18f,.32f};
    for(unsigned p=0;p<PART_COUNT;p++) {
        ma3_part_init(&show->part[p],PART_NAMES[p],hurt_voice_patch(p),limits[p],unison[p],stems[p],gain[p],ch[p],rv[p]);
        show->part[p].config.detune_cents=p==MELODY?3:5;
    }
}

#endif
