/* Host build of the channel plan of marine_app.c: prints one line per selector
 * entry, "designator rx_10Hz tx_10Hz rx_only low_only", for
 * tools/check_channels.py to compare with the App18 reference table, and
 * checks the keypad entry (chEntryComplete) the same way the app uses it.
 *
 *   cc -O2 -DHOST_TEST -o host_channels test/host_channels.c && ./host_channels
 */
#include <stdio.h>
#include <string.h>
#include "../app/marine/marine_app.c"

/* Type the digits of keys ('.' = a pause long enough to take the channel):
 * the designator selected, -1 for "no such channel", -2 still waiting. Also
 * reports whether it was taken at once (no pause needed). */
static int type(const char *keys, int *atOnce)
{
    uint16_t v = 0; uint8_t n = 0;
    *atOnce = 0;
    for (const char *k = keys; *k; k++) {
        if (*k == '.') {
            if (!n) continue;
            uint8_t i = chFind(v);
            return i < NCH ? CH[i] : -1;
        }
        v = (uint16_t)(v * 10u + (uint16_t)(*k - '0')); n++;
        if (chEntryComplete(v, n)) {
            *atOnce = 1;
            uint8_t i = chFind(v);
            return (k[1] && k[1] != '.') ? -3 /* digits left over */ : (i < NCH ? CH[i] : -1);
        }
    }
    return -2;
}

static int entry_tests(void)
{
    int fails = 0, at;
    char buf[8];
    /* every channel, typed in full (2 or 4 digits), with a pause after it if needed */
    for (uint8_t i = 0; i < NCH; i++) {
        snprintf(buf, sizeof buf, CH[i] < 100u ? "%02u." : "%u.", CH[i]);
        const int got = type(buf, &at);
        if (got != CH[i]) { fprintf(stderr, "entry %s -> %d, want %u\n", buf, got, CH[i]); fails++; }
    }
    /* one digit then a pause: channel 0d (and 0 alone is no channel) */
    static const struct { const char *keys; int want, atOnce; } T[] = {
        { "1.", 1, 0 },  { "2.", 2, 0 },  { "6.", 6, 0 },  { "7.", 7, 0 },  { "8.", 8, 0 },
        { "3", 3, 1 },   { "4", 4, 1 },   { "5", 5, 1 },   { "9", 9, 1 },          /* no 3x, 4x, 5x, 9x */
        { "0.", -1, 0 }, { "05", 5, 1 },  { "72", 72, 1 }, { "16", 16, 1 }, { "88", 88, 1 },
        { "10.", 10, 0 }, { "1019", 1019, 1 }, { "102.", -1, 0 }, { "1027", 1027, 1 },
        { "20.", 20, 0 }, { "2078", 2078, 1 }, { "70", -1, 1 }, { "29", -1, 1 },
        { "27", -1, 1 },  { "1021", -1, 1 },
    };
    for (size_t t = 0; t < sizeof T / sizeof T[0]; t++) {
        const int got = type(T[t].keys, &at);
        if (got != T[t].want || at != T[t].atOnce) {
            fprintf(stderr, "entry \"%s\" -> %d%s, want %d%s\n", T[t].keys, got, at ? " at once" : "",
                    T[t].want, T[t].atOnce ? " at once" : "");
            fails++;
        }
    }
    return fails;
}

int main(void)
{
    for (uint8_t i = 0; i < NCH; i++) {
        const uint16_t d = CH[i];
        printf("%u %lu %lu %d %d\n", d, (unsigned long)chRx(d),
               (unsigned long)(chRxOnly(d) ? 0u : chTx(d)), chRxOnly(d), chLowOnly(d));
    }
    if (entry_tests()) return 1;
    return 0;
}
