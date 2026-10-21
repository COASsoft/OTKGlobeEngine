#include <otk/otk.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char mgrs[64];
    otk_to_mgrs(46.8529, -121.7604, 5, mgrs, (int)sizeof(mgrs));
    otk_sun_moon sky;
    otk_sun_moon_at(1718971200.0, 51.5, 0.0, &sky);
    printf("%s sun %.1f\n", mgrs, sky.sun_elevation);
    return strcmp(mgrs, "10T ES 94499 89563") == 0 && fabs(sky.sun_elevation - 61.9) < 0.5 ? 0 : 1;
}
