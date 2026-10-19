import struct, math, sys
def utm(lat, lon, zone):
    a=6378137.0; f=1/298.257223563; e2=f*(2-f); k0=0.9996; ep2=e2/(1-e2)
    lon0=math.radians((zone-1)*6-180+3); phi=math.radians(lat); lam=math.radians(lon)
    s,c,t=math.sin(phi),math.cos(phi),math.tan(phi); n=a/math.sqrt(1-e2*s*s); T=t*t; C=ep2*c*c; A=c*(lam-lon0)
    e4=e2*e2; e6=e4*e2
    M=a*((1-e2/4-3*e4/64-5*e6/256)*phi-(3*e2/8+3*e4/32+45*e6/1024)*math.sin(2*phi)+(15*e4/256+45*e6/1024)*math.sin(4*phi)-(35*e6/3072)*math.sin(6*phi))
    E=k0*n*(A+(1-T+C)*A**3/6+(5-18*T+T*T+72*C-58*ep2)*A**5/120)+500000
    N=k0*(M+n*t*(A*A/2+(5-T+9*C+4*C*C)*A**4/24+(61-58*T+T*T+600*C-330*ep2)*A**6/720))
    return E,N
cx, cy = utm(46.8529, -121.7604, 10)
pts=[]
for i in range(60000):
    t=i/60000
    ang=t*40*math.pi; r=300*(1-t)+40
    x=cx+r*math.cos(ang); y=cy+r*math.sin(ang); z=4420+600*t
    col=(int(65535*t), int(65535*(1-t)), 40000)
    pts.append((x,y,z,col))
sx=sy=sz=0.01; ox,oy,oz=cx,cy,0
geokeys=struct.pack('<4H',1,1,0,2)+struct.pack('<4H',1024,0,1,1)+struct.pack('<4H',3072,0,1,32610)
vlr=struct.pack('<H16sHH32s',0,b'LASF_Projection',34735,len(geokeys),b'geokeys')+geokeys
header_size=227; offset=header_size+len(vlr); rec=26
xs=[p[0] for p in pts]; ys=[p[1] for p in pts]; zs=[p[2] for p in pts]
h=bytearray(b'LASF')+struct.pack('<HH',0,0)+bytes(16)+bytes([1,2])+b'synthetic'.ljust(32,b'\0')+b'test'.ljust(32,b'\0')+struct.pack('<HHHI I B H I',1,2026,header_size,offset,1,2,rec,len(pts))
h+=struct.pack('<5I',len(pts),0,0,0,0)
h+=struct.pack('<3d',sx,sy,sz)+struct.pack('<3d',ox,oy,oz)+struct.pack('<6d',max(xs),min(xs),max(ys),min(ys),max(zs),min(zs))
assert len(h)==header_size, len(h)
body=bytearray()
for x,y,z,c in pts:
    body+=struct.pack('<3iHBBbBH3H',round((x-ox)/sx),round((y-oy)/sy),round((z-oz)/sz),100,0,0,0,0,0,*c)
open(sys.argv[1],'wb').write(bytes(h)+vlr+bytes(body))
print('ok', len(pts), cx, cy)
