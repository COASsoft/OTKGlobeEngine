using System;
using OpenTakEngine;

string mgrs = Engine.ToMgrs(46.8529, -121.7604, 5);
Console.WriteLine($"mgrs {mgrs}");
int ok = Native.ote_from_mgrs(mgrs, out double lat, out double lon);
Console.WriteLine($"roundtrip {ok} {lat:F5} {lon:F5}");
var sky = Engine.SunMoonAt(DateTimeOffset.FromUnixTimeSeconds(1718971200), 51.5, 0.0);
var day = Engine.SunMoonTimes(DateTimeOffset.FromUnixTimeSeconds(1718928000), 51.5074, -0.1278);
Console.WriteLine($"sun {sky.SunElevation:F1} lux {sky.SunLux:F0} sunrise {DateTimeOffset.FromUnixTimeMilliseconds((long)(day.Sunrise * 1000)):HH:mm}");
bool astro = sky.SunElevation > 55 && sky.SunLux > 50000 && Math.Abs(day.Sunrise - 1718941380) < 180;
return ok == 1 && astro && Math.Abs(lat - 46.8529) < 1e-4 && Math.Abs(lon + 121.7604) < 1e-4 ? 0 : 1;
