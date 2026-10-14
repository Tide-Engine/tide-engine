"""Build the publisher PDF from real demo screenshots. Requires ReportLab."""
from pathlib import Path
import sys
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import Paragraph
from reportlab.lib.styles import ParagraphStyle

ROOT = Path(__file__).resolve().parents[3]
ART = ROOT / 'docs/static/prototypes/copper-coil'
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'output/pdf/copper-coil-publisher-pitch.pdf'
OUT.parent.mkdir(parents=True, exist_ok=True)
pdfmetrics.registerFont(TTFont('Segoe', 'C:/Windows/Fonts/segoeui.ttf'))
pdfmetrics.registerFont(TTFont('SegoeBold', 'C:/Windows/Fonts/seguisb.ttf'))
W, H = 960, 540
INK, PANEL, CREAM, DIM, COPPER, COIL = [HexColor(v) for v in ['#102026','#192f34','#f4ecd2','#8faeac','#ff9247','#59d9c4']]
c = canvas.Canvas(str(OUT), pagesize=(W,H))
c.setTitle('Copper & Coil | Playable prototype and publisher pitch')
c.setAuthor('Tide Engine')

def text(words,x,y,size=16,tone=CREAM,bold=False):
    c.setFillColor(tone)
    c.setFont('SegoeBold' if bold else 'Segoe',size)
    c.drawString(x,y,words)

def para(words,x,y,width,size=16,tone=CREAM):
    style=ParagraphStyle('body',fontName='Segoe',fontSize=size,leading=size*1.38,textColor=tone)
    p=Paragraph(words,style)
    _,height=p.wrap(width,H)
    p.drawOn(c,x,y-height)
    return height

def page(number,kicker,title):
    c.setFillColor(INK); c.rect(0,0,W,H,fill=1,stroke=0)
    text(kicker.upper(),48,500,11,COIL,True)
    if title: text(title,48,452,32,CREAM,True)
    c.setStrokeColor(PANEL); c.line(48,40,912,40)
    text('COPPER & COIL   /   WORKING TITLE   /   PLAYABLE PROTOTYPE',48,23,9,DIM)
    text(f'{number:02d}',891,23,9,DIM)

def image(name,x,y,width,height):
    c.drawImage(str(ART/name),x,y,width,height,preserveAspectRatio=True,anchor='c',mask='auto')

page(1,'Cooperative circuit adventure','')
text('COPPER',48,413,64,COPPER,True)
text('& COIL',48,343,64,CREAM,True)
text('Two robots.',50,283,28,CREAM,True)
text('One bright idea.',50,247,28,CREAM,True)
para('Restore a forgotten power station together. Rewire its logic, watch the electricity flow, and bring every chamber back to life.',50,216,320,17,DIM)
image('cover.png',396,196,530,298)
text('1-2 PLAYERS',50,99,13,COIL,True)
text('ONLINE + SHARED KEYBOARD + SOLO',50,76,12,DIM)
text('Six playable chambers',410,131,23,CREAM,True)
para('An original miniature workshop built around the joy of a circuit that finally works.',410,108,450,15,DIM)
c.showPage()

page(2,'The player experience','Make the connection. Together.')
image('gameplay.png',42,90,610,343)
text('ACTUAL GAMEPLAY',50,70,9,DIM)
steps=[('01','READ THE MACHINE','An understandable goal: both inputs, exactly one, neither, or a safety interlock.'),
       ('02','SHARE THE WORK','Copper changes one bank. Coil changes the other. Talk through the signal path.'),
       ('03','TEST EVERY CASE','A live truth table checks every input combination. A lucky switch setting is not enough.'),
       ('04','BRING IT TO LIFE','Both robots commission the circuit on separate pads, then cross the powered bridge.')]
for i,(num,label,body) in enumerate(steps):
    y=407-i*87
    text(num,686,y,16,COPPER,True)
    text(label,723,y,12,COIL,True)
    para(body,686,y-15,224,13,CREAM)
c.showPage()

page(3,'What is playable today','Six small machines. Six bright ideas.')
rows=[('01','It takes two','AND: both generators must run.'),
      ('02','A little resistance','AND + NOT: A on, B off.'),
      ('03','One or the other','XOR: exactly one generator.'),
      ('04','The quiet hours','OR + NOT: power when both are off.'),
      ('05','On the same wavelength','XOR + NOT: inputs must agree.'),
      ('06','The last light','Two mixers: exactly one input, plus safety C.')]
for i,(num,name,body) in enumerate(rows):
    y=405-i*52
    text(num,50,y,17,COPPER,True)
    text(name,91,y,17,CREAM,True)
    text(body,91,y-20,13,DIM)
c.setFillColor(PANEL);c.roundRect(583,99,329,321,12,fill=1,stroke=0)
text('IN THIS BUILD',605,388,12,COIL,True)
para('Online rooms and two assigned robots<br/><br/>Shared keyboard or solo switching<br/><br/>Live wires and truth-table testing<br/><br/>Undo, reset, hints and chamber selection<br/><br/>Original art and six sound cues<br/><br/>Windows and browser builds',605,365,279,15)
text('All six circuits use the same workshop layout.',50,67,11,DIM)
c.showPage()

page(4,'The publishing conversation','A clear hook. A focused next step.')
text('THE HOOK',50,401,12,COPPER,True)
para('The pleasure of redstone-style logic, made into a friendly two-player puzzle adventure. Electricity gives immediate feedback; the robots make cooperation visible.',50,380,401,18)
text('AUDIENCE HYPOTHESIS',50,271,12,COPPER,True)
para('Pairs who enjoy reasoning aloud, building little machines, and a calm shared sense of discovery. This is a positioning hypothesis, not a sales forecast.',50,249,401,16,DIM)
text('NEXT MILESTONE - PROPOSED',504,401,12,COIL,True)
para('A funded vertical slice with distinct spatial chambers, movable modules and richer routing; co-op puzzle playtests; save/resume; gamepad support and platform invites.',504,380,401,18)
text('PARTNERSHIP DISCUSSION',504,271,12,COIL,True)
para('Development funding, player research, positioning and launch support. Budget, team and schedule need to be scoped with the owner after the demo is evaluated.',504,249,401,16,DIM)
c.setFillColor(PANEL);c.roundRect(48,71,864,85,10,fill=1,stroke=0)
text('READY TO EVALUATE',68,130,12,COIL,True)
para('Six playable circuits and an included guided walkthrough. Automated native and WebAssembly checks cover every chamber, player authorization, shared undo and snapshots. No external player study or production budget is claimed.',68,111,823,13,CREAM)
c.showPage()
c.save()
print(OUT)
