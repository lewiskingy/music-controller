"""Portrait case and keyed charging cradle for Waveshare's original 4.3 board.

Coordinates: X across the portrait screen, Y upwards, Z from glass to rear.
The board is turned 90 degrees so its USB sockets face the lower short edge.
All standalone parts are exported in their flat print orientations.
"""
import cadquery as cq

W, H, DEPTH = 80, 138, 39
BOARD_Y = 10  # Extra space below the board for an internal USB plug/pigtail.
SCREWS = [(x, y) for x in (-36, 36) for y in (-65, 65)]
CONTACT_X = (-7, 7)
CONTACT_Z = 32.0
CONTACT_INSERT_Z = 28.2
DOCK_FLOOR = 6
DOCK_SLOT_WIDTH = W + 0.8
DOCK_SLOT_DEPTH = DEPTH + 0.8
KEY_X, KEY_Z = 27, 30


def box(w, h, d, z=0, x=0, y=0):
    return cq.Workplane('XY').box(w, h, d, centered=(True, True, False)).translate((x, y, z))


def rounded(w, h, d, z=0, r=4):
    return box(w, h, d, z).edges('|Z').fillet(r)


def hole(x, y, r, z, d):
    return cq.Workplane('XY').center(x, y).circle(r).extrude(d).translate((0, 0, z))


# Screen front seats at Z=2.3, including 0.3 mm soft tape on the 2 mm bezel.
front = rounded(W, H, 23).cut(rounded(69.3, 127.1, 23, 2, r=0.6))
front = front.cut(box(56, 97, 4, -1, x=-2.4, y=BOARD_Y))
# Short-edge service openings. Bottom has a 20 mm internal plug/cable bay;
# top retains access to the opposite peripheral connectors. Long sides are closed.
front = front.cut(box(48, 12, 16, 7, y=-67))
front = front.cut(box(53, 12, 16, 7, x=1, y=67))
for x, y in SCREWS:
    front = front.cut(hole(x, y, 1.25, 11, 14))

back = rounded(W, H, 2)
rim = rounded(68.5, 126.3, 1.2, 2, r=0.6).cut(rounded(64.5, 122.3, 2, 1.9, r=0.6))
rim = rim.cut(box(60, 15, 5, 1, y=-61)).cut(box(60, 15, 5, 1, y=61))
back = back.union(rim)
for x, y in SCREWS:
    back = back.cut(hole(x, y, 1.7, -1, 5)).cut(hole(x, y, 3.1, -0.1, 1.1))
# Back is flipped about X for assembly: print Y is the negative of assembled Y.
for x in (-30, 30):
    for board_y in (-49 + BOARD_Y, 49 + BOARD_Y):
        back = back.union(hole(x, -board_y, 2.75, 2, 22))

tray = box(48, 42, 2).union(box(48, 42, 12, 2).cut(box(44, 38, 13, 2)))
tray = tray.cut(box(12, 8, 8, 7, y=20))  # Lead notch towards lower USB bay.
insert = box(24.6, 3, 7.6)
for x in CONTACT_X:
    insert = insert.cut(cq.Workplane('XZ').center(x, 3.8).circle(1.6).extrude(8, both=True))
blank = box(24.6, 3, 7.6)

ext = rounded(W, H, 14).cut(rounded(69.3, 127.1, 16, -1, r=0.6))
# Contacts are on the bottom short face, behind the USB service opening.
ext = ext.cut(box(25, 10, 8, 5, y=-H/2))
# Asymmetric key beside the contacts, preventing reversed insertion.
ext = ext.cut(box(6.8, 10, 6.8, KEY_Z-23-3.4, x=KEY_X, y=-H/2))
for x, y in SCREWS:
    ext = ext.cut(hole(x, y, 1.7, -1, 16))

# Narrow portrait slot on a deeper base for the taller controller.
dock = rounded(96, 96, DOCK_FLOOR, r=5)
wall_y = DOCK_SLOT_DEPTH/2 + 1.5
wall_x = DOCK_SLOT_WIDTH/2 + 1.5
wall_w = DOCK_SLOT_WIDTH + 6
front_wall = box(wall_w, 3, 12, DOCK_FLOOR, y=-wall_y)
# A centred relief keeps the lower USB service opening accessible for inspection.
front_wall = front_wall.cut(box(50, 8, 9, 9, y=-wall_y))
rear_wall = box(wall_w, 3, 52, DOCK_FLOOR, y=wall_y)
# Clearance for lower rear M3 button heads: counterbores may leave 0.7 mm protruding.
for x in (-36, 36):
    rear_wall = rear_wall.cut(box(8, 2, 9, DOCK_FLOOR, x=x, y=20.5))
dock = dock.union(front_wall).union(rear_wall)
for x in (-wall_x, wall_x):
    dock = dock.union(box(3, DOCK_SLOT_DEPTH+6, 18, DOCK_FLOOR, x=x))
dock = dock.union(box(6, 6, 3, DOCK_FLOOR, x=-KEY_X, y=KEY_Z-DEPTH/2))
for x in CONTACT_X:
    dock = dock.cut(hole(x, CONTACT_Z-DEPTH/2, 1.6, -1, DOCK_FLOOR+2))
dock = dock.cut(box(22, 48, 2, -.1, y=24))

parts = {'01_front': front, '02_rear_extension': ext, '03_back': back,
         '04_battery_tray': tray, '05_contact_insert': insert,
         '06_contact_blank': blank, '08_dock': dock}


def manufacturer_to_case(model):
    """Official STEP axes -> portrait case; glass front is Z=2.3."""
    return (model.mirror('XY').translate((-.05, 2.47, 7.1))
            .rotate((0, 0, 0), (0, 0, 1), 90).translate((0, BOARD_Y, 0)))


def assembled_parts(include_insert=False):
    result = {'front': front,
              'extension': ext.translate((0, 0, 23)),
              'back': back.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, 0, DEPTH)),
              'battery_tray': tray.rotate((0, 0, 0), (1, 0, 0), 180).translate((0, BOARD_Y, 37))}
    if include_insert:
        result['contact_insert'] = insert.translate((0, -H/2+1.5, CONTACT_INSERT_Z))
    return result


def case_to_dock(shape):
    # Transform (X,Y,Z) to (-X, Z-19.5, Y+75): a rigid rotation, not a reflection.
    return (shape.rotate((0, 0, 0), (0, 1, 1), 180)
            .translate((0, -DEPTH/2, H/2+DOCK_FLOOR)))
