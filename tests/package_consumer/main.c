#include <ote/ote.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char mgrs[64];
    ote_to_mgrs(46.8529, -121.7604, 5, mgrs, (int)sizeof(mgrs));
    ote_sun_moon sky;
    ote_sun_moon_at(1718971200.0, 51.5, 0.0, &sky);
    printf("%s sun %.1f\n", mgrs, sky.sun_elevation);
    return strcmp(mgrs, "10T ES 94499 89563") == 0 && fabs(sky.sun_elevation - 61.9) < 0.5 ? 0 : 1;
}
