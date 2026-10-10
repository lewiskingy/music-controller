import cadquery as cq
from pathlib import Path
# millimetres. Model centre shifted to case centre. Screen faces z=0.
W,H=118,80; screws=[(x,y) for x in (-55,55) for y in (-36,36)]
def box(w,h,d,z=0,x=0,y=0):return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).translate((x,y,z))
def rounded(w,h,d,z=0,r=4):return box(w,h,d,z).edges('|Z').fillet(r)
def hole(x,y,r,z,d):return cq.Workplane('XY').center(x,y).circle(r).extrude(d).translate((0,0,z))
# Front face down: 2 mm bezel; screen front seats on soft tape at z=2.
front=rounded(W,H,23).cut(rounded(107.1,69.3,23,2,r=0.6))
# Visible display opening is offset upwards by 2.4 mm.
front=front.cut(box(97,56,4,-1,x=0,y=2.4))
# wide side access windows avoid dependence on plug shells
front=front.cut(box(12,48,16,7,x=-57,y=0)).cut(box(12,53,16,7,x=57,y=-1))
# Rear cover posts bear on the metal mounting bosses.
for x,y in screws:
 front=front.cut(hole(x,y,1.25,11,14))
# replaceable bottom charge-contact insert opening
front=front.cut(box(25,10,8,14,x=0,y=-39))
# back: separate 2mm plate plus locating rim, 15mm electronics/battery space behind board
back=rounded(W,H,2)
rim=rounded(106.3,68.5,1.2,2,r=0.6).cut(rounded(102.3,64.5,2,1.9,r=0.6))
# no continuous rim: connector side access is open
rim=rim.cut(box(15,60,5,1,x=-51)).cut(box(15,60,5,1,x=51))
back=back.union(rim)
for x,y in screws:
 back=back.cut(hole(x,y,1.7,-1,5)).cut(hole(x,y,3.1,-0.1,1.1))
# separate battery tray mounted to back with foam tape; cavity 44x38 accommodates estimated 103440
tray=box(48,42,2).union(box(48,42,12,2).cut(box(44,38,13,2)))
tray=tray.cut(box(12,8,8,7,x=0,y=20))
# contact insert: attach behind bottom opening. Copper contacts fitted through 3.2 mm holes.
insert=box(24.6,3,7.6).cut(cq.Workplane('XZ').center(-7,3.8).circle(1.6).extrude(8,both=True)).cut(cq.Workplane('XZ').center(7,3.8).circle(1.6).extrude(8,both=True))
# blanking insert for case without charging modifications
blank=box(24.6,3,7.6)
# Four rear compression supports touch foam on the metal mounting bosses.
for x in (-49,49):
 for y in (-30,30): back=back.union(hole(x,y,2.75,2,22))
# battery enclosure needs extra depth: front is 23 high; rear extension provides 14 mm
ext=rounded(W,H,14).cut(rounded(107.1,69.3,16,-1,r=0.6))
ext=ext.cut(box(6.8,10,6,5,x=40,y=-40))
for x,y in screws:ext=ext.cut(hole(x,y,1.7,-1,16))
# Optional upright desktop cradle: 39 mm enclosure depth + 0.8 mm clearance.
dock=rounded(132,76,6,r=5).union(box(125,3,12,6,y=-21.4)).union(box(125,3,34,6,y=21.4))
# End stops and guide blocks keep the controller centred.
for x in (-61.25,61.25): dock=dock.union(box(3,45.8,12,6,x=x))
dock=dock.union(box(6,6,3,6,x=40,y=9.5))
# 3.2 mm pilot holes: select compatible pogo pins or modify to chosen hardware.
for x in (-7,7): dock=dock.cut(hole(x,-1.5,1.6,-1,8))
# underbase cable routing groove
dock=dock.cut(box(22,42,2,-.1,y=16))
parts={'08_dock':dock,'01_front':front,'02_rear_extension':ext,'03_back':back,'04_battery_tray':tray,'05_contact_insert':insert,'06_contact_blank':blank}


def assembled_parts():
    """Case coordinate system: glass faces -Z; front face at Z=0."""
    return {
        "front": front,
        "extension": ext.translate((0, 0, 23)),
        "back": back.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, 39)),
        "battery_tray": tray.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, 37)),
    }
