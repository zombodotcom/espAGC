// key_parse.c — token → AGC 5-bit keycode lookup.

#include "dsky_input.h"
#include "dsky_keys.h"

#include <ctype.h>
#include <string.h>

int dsky_input_parse_token(const char *t)
{
    if (!t || !*t) return -1;
    if (t[1] == '\0') {
        char c = (char)toupper((unsigned char)t[0]);
        if (c >= '0' && c <= '9') return (c == '0') ? DSKY_KEY_0 : (c - '0');
        switch (c) {
            case '+': return DSKY_KEY_PLUS;
            case '-': return DSKY_KEY_MINUS;
            case 'E': return DSKY_KEY_ENTR;
            case 'V': return DSKY_KEY_VERB;
            case 'N': return DSKY_KEY_NOUN;
            case 'C': return DSKY_KEY_CLR;
            case 'P': return DSKY_KEY_PRO;
            case 'R': return DSKY_KEY_RSET;
            case 'K': return DSKY_KEY_KEYREL;
        }
        return -1;
    }
    if (!strcasecmp(t, "VRB"))  return DSKY_KEY_VERB;
    if (!strcasecmp(t, "VERB")) return DSKY_KEY_VERB;
    if (!strcasecmp(t, "NUN"))  return DSKY_KEY_NOUN;
    if (!strcasecmp(t, "NOUN")) return DSKY_KEY_NOUN;
    if (!strcasecmp(t, "ENT"))  return DSKY_KEY_ENTR;
    if (!strcasecmp(t, "ENTR")) return DSKY_KEY_ENTR;
    if (!strcasecmp(t, "PRO"))  return DSKY_KEY_PRO;
    if (!strcasecmp(t, "RSET")) return DSKY_KEY_RSET;
    if (!strcasecmp(t, "CLR"))  return DSKY_KEY_CLR;
    if (!strcasecmp(t, "KREL")) return DSKY_KEY_KEYREL;
    if (!strcasecmp(t, "KEYREL")) return DSKY_KEY_KEYREL;
    return -1;
}
