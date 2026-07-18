/* MIDI byte parser. Freestanding — same bytes from ALSA rawmidi or a UART. */
#include "tonewheel.h"

int tw_midi_parse(tw_midi_parser *p, uint8_t byte, tw_midi_msg *out) {
    if (byte >= 0xF8) return 0; /* real-time: transparent to the stream */
    if (byte >= 0xF0) {         /* system common / SysEx: cancels running status */
        p->status = 0;
        p->have = 0;
        return 0;
    }
    if (byte & 0x80) {
        p->status = byte;
        p->have = 0;
        p->need = ((byte & 0xE0) == 0xC0) ? 1 : 2; /* Cn/Dn carry one byte */
        return 0;
    }
    if (p->status == 0) return 0; /* stray data byte */
    p->data[p->have++] = byte;
    if (p->have < p->need) return 0;
    p->have = 0; /* running status persists */
    out->status = p->status;
    out->d1 = p->data[0];
    out->d2 = (p->need == 2) ? p->data[1] : 0;
    return 1;
}
