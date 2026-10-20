# Regenerate with Python and mpmath==1.3.0; not needed to run the C tests.
from pathlib import Path
import struct, random, math
import mpmath as mp
mp.mp.dps=400
bits=lambda x:struct.unpack('<Q',struct.pack('<d',x))[0]
value=lambda u:struct.unpack('<d',struct.pack('<Q',u))[0]
r=random.Random(81482)
angles=[0.0,-0.0,1.0,-1.0,math.pi/4,math.pi/2,math.pi,math.nextafter(math.pi/2,0),math.nextafter(math.pi/2,math.inf),2**20,2**50,1e100,1e300,value(0x7fefffffffffffff),value(1),value(0xfffffffffffff)]
angles += [value((r.randrange(1,2047)<<52)|r.getrandbits(52))*(-1 if i&1 else 1) for i in range(20)]
rows=[]
names='sin cos tan asin acos atan atan2 exp exp2 log log2 log10 pow'.split()
def add(name,x,y=0.0):
 a,b=mp.mpf(x),mp.mpf(y)
 fn={'exp2':lambda x:mp.power(2,x),'log2':lambda x:mp.log(x,2),'log10':mp.log10,'pow':mp.power}.get(name,getattr(mp,name,None))
 z=fn(a,b) if name in ('atan2','pow') else fn(a)
 expected=float(z)
 # Preserve exact signed-zero cases separately in the C special-case test.
 if expected==0: expected=0.0
 rows.append((names.index(name),bits(x),bits(y),bits(expected)))
for name in names:
 if name in ('sin','cos','tan','atan'): xs=angles
 elif name in ('asin','acos'): xs=[-1.0,1.0,math.nextafter(-1.0,0),math.nextafter(1.0,0),-.5,.5,1e-100,value(1),-1e-300,0.0]+[r.uniform(-1,1) for i in range(10)]
 elif name in ('exp','exp2'): xs=[-1075.,-1074.,-1022.,-745.,-744.,-710.,-700.,-10.,-1.,0.,1.,10.,700.,709.,709.782712893384,710.,1023.,1024.]+[r.uniform(-700,700) for i in range(10)]
 elif name in ('log','log2','log10'): xs=[abs(x) for x in angles if x!=0]+[.5,math.nextafter(1.,0),math.nextafter(1.,2),value(0x10000000000000)]
 elif name=='atan2':
  for x,y in [(1.,1.),(1.,-1.),(-1.,1.),(-1.,-1.),(value(1),1e300),(1e300,value(1)),(1e-300,-1e300),(-1e300,-1e-300)]+[(r.uniform(-10,10),r.uniform(-10,10)) for i in range(12)]: add(name,x,y)
  continue
 elif name=='pow':
  for x,y in [(2.,-1074.),(2.,1023.),(2.,1024.),(-2.,3.),(-2.,4.),(.5,1023.),(math.nextafter(1.,2),1e15),(math.nextafter(1.,0),-1e15),(1e300,.125),(1e-300,.5),(value(1),.5),(2.,.5)]+[(r.uniform(.01,10),r.uniform(-100,100)) for i in range(12)]: add(name,x,y)
  continue
 for x in xs: add(name,x)
s='// Generated with mpmath 1.3.0 at 400 decimal digits, rounded to binary64.\n// Functions: '+', '.join(f'{i}={n}' for i,n in enumerate(names))+'.\n// x, y and reference results are exact IEEE bit patterns.\n'
s+='static const struct { int function; uint64_t x, y, expected; } double_math_cases[] = {\n'
for f,x,y,z in rows: s+=f'    {{{f}, UINT64_C(0x{x:016X}), UINT64_C(0x{y:016X}), UINT64_C(0x{z:016X})}},\n'
s+='};\n'
Path(__file__).with_name('double_math_cases.h').write_text(s,newline='\n')
print('golden cases',len(rows))
