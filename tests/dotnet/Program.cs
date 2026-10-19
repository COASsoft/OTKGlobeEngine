using System;
using OpenTakEngine;

string mgrs = Engine.ToMgrs(46.8529, -121.7604, 5);
Console.WriteLine($"mgrs {mgrs}");
int ok = Native.ote_from_mgrs(mgrs, out double lat, out double lon);
Console.WriteLine($"roundtrip {ok} {lat:F5} {lon:F5}");
return ok == 1 && Math.Abs(lat - 46.8529) < 1e-4 && Math.Abs(lon + 121.7604) < 1e-4 ? 0 : 1;
