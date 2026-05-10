#pragma once
// AGC DSKY 5-bit keycodes as written into input channel 015.
// (See Virtual AGC project docs and Apollo Guidance Computer schematic.)

#define DSKY_KEY_0       16
#define DSKY_KEY_1        1
#define DSKY_KEY_2        2
#define DSKY_KEY_3        3
#define DSKY_KEY_4        4
#define DSKY_KEY_5        5
#define DSKY_KEY_6        6
#define DSKY_KEY_7        7
#define DSKY_KEY_8        8
#define DSKY_KEY_9        9
#define DSKY_KEY_VERB    17
#define DSKY_KEY_NOUN    31
#define DSKY_KEY_PLUS    26
#define DSKY_KEY_MINUS   27
#define DSKY_KEY_ENTR    28
#define DSKY_KEY_CLR     30
// PRO is NOT a regular ch015 keycode — yaDSKY2 sends it via OutputPro()
// which writes bit 0x4000 of InputChannel[032] (clear-while-held). For now
// we alias it to a sentinel so callers can distinguish, and the input
// transports map it to a no-op until we wire ch032 properly.
#define DSKY_KEY_PRO     63   /* sentinel — handled specially, not a ch015 code */
#define DSKY_KEY_KEYREL  25   /* canonical KEY REL ch015 keycode */
#define DSKY_KEY_RSET    18
