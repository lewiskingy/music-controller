# ESP32-S3 music controller — print package v1

## What to send to a printing service
Print one each of 01_front.stl, 02_rear_extension.stl, 03_back.stl and 04_battery_tray.stl. Print either 05_contact_insert.stl (charging dock build) or 06_contact_blank.stl (USB-only build). Print one 08_dock.stl if wanted. Do not print case_assembly.step: it is an assembly reference, not a single printable part.

All dimensions are millimetres; print at 100%. PETG, 0.20 mm layers, 4 perimeters, 5 top/bottom layers, 25–30% infill, dimensional tolerance target ±0.2 mm. Keep supplied orientations for the front, extension, back, tray and dock: their largest flat face is on the bed. Rotate the contact insert/blank 90 degrees about X so its broad 24.6 × 7.6 mm face rests on the bed. No supports should be needed in those orientations. Use a brim for the back cover if needed; its four narrow compression posts must remain straight. Deburr screw pilots, slide fits and contact holes by hand. No automatic scaling or geometry repair that changes hole centres.

Suggested service instruction: “Please print the six selected STL files, one each, in PETG at 100% scale (mm), 0.2 mm layers, 4 walls, 25% infill. Follow orientations in README. Please check meshes and preserve dimensions. Please quote the optional dock separately.”

## Dimensions and source inspection
Downloaded official archive: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4in3_3D_Drawing.zip
It contains esp32-s3-touch-lcd-4_3.stp, a Creo assembly dated 2023-12-13, containing 693 solids. This is the original 4.3 model, not the B/C variants.

Official drawing: touch glass 106.10 × 67.80 mm; active display 95.54 × 54.36 mm; PCB approximately 106 × 68 mm; mounting centres 98 × 60 mm, 4 mm from PCB edges.
STEP total bounds: X −53.000 to 53.100; Y −36.479 to 31.783; Z −12.100 to 4.800 mm. Total depth 16.90 mm, including rear connectors. The STEP includes small flexible/connector projections beyond the nominal drawing outline.
PCB hole centres: X −48.950 / 49.050; Y −32.470 / 27.530; hole radius 2.150 mm. Metal bosses rear surface Z −7.700. Design coordinates translate X by −0.05, Y by +2.47, reflect Z and translate Z by +7.10: glass front seats at Z=2.30 and boss ends at Z=14.80. Back support ends at Z=15.00. Use soft interface pads, not rigid clamping of the glass.

Enclosure outside: 118 × 80 × 39 mm assembled (front 23 + extension 14 + back 2). Screen cavity 107.1 × 69.3 mm. Bezel opening 97 × 56 mm, centre offset +2.4 mm in Y to match the unequal screen borders. Wide connector access on both sides also provides ventilation; this enclosure is not sealed. Four rear posts support the metal mounting bosses. Four separate enclosure screws hold the body together.
Battery tray inside 44 × 38 × 12 mm. Assumed 103440 pack is about 40 × 34 × 10 mm, but model code is not a certified dimension. Measure the actual pack including protection circuit and wrapping; do not force a pack that does not fit. Tray is removable and can be resized in the source.
Dock base 132 × 76 mm, maximum height 40 mm. Landscape controller stands upright. Slot 119.5 × 39.8 mm; underside key prevents front/back reversal. Contact centres are 14 mm apart. Dock contact bores and case insert bores are 3.2 mm; they are mounting pilots, not a specification for an arbitrary pogo connector.

## Assembly — USB-only first
1. Dry-fit all printed parts without electronics. M3 × 25 mm screws pass through the back and extension into the front's 2.5 mm pilots. Use four screws. Tap pilots carefully with an M3 tap, or use suitable plastic-thread screws. Do not overtighten. Head counterbores are shallow and heads may protrude slightly.
2. Put approximately 0.3 mm thin soft tape on the front bezel bearing area, outside the visible display. Seat the display with its two USB sockets at the left when viewed from the rear as in the manufacturer's drawing; check active area alignment before tightening. Protect flex cables.
3. Add roughly 0.2 mm soft pads to rear compression-post tips. The posts must meet the four metal bosses, never components. Pads/tape account for the designed 0.5 mm combined clearance. Adjust padding only after checking fit; stop if the display is under pressure or the cover bows.
4. Attach battery tray to the centre of the back's inside face with thin mounting tape. Route leads through its notch, away from the four posts. Secure the pack with removable tape on its broad face; no screws, sharp edges or compression against the pouch. The tray provides 2 mm clearance around the assumed pack and 2 mm height clearance.
5. Verify battery chemistry, polarity and its permitted charge current. Board documentation specifies 580 mA charging. Connect only a suitable 3.7 V single-cell pack to the PH2.0 battery connector. A fitting connector is not proof of correct polarity.
6. Fit blank insert at bottom opening with removable tape or a small bead of adhesive. Close case with extension between shell and back. Check operation and USB charging before adding dock wiring.

## Optional contact charging
This is a mechanical enclosure and cradle, not a preassembled electrical product. A printing service supplies plastic only. Have a maker assemble the wiring if needed.
Required hardware: two smooth conductive contacts with 3 mm stems and approximately 5 mm heads, two spring contacts with bodies suitable for the dock's 3.2 mm bores, insulated flexible wire, strain relief, short USB-C plug/pigtail for the board, a fused/current-limited regulated 5 V USB supply, and rubber feet. Allow at least 2 A supply capacity for operation plus charging. Select actual contact hardware before committing to the dock print; included editable CAD allows bore changes.

Case insert fits opening at X=0, Y=−38.5, Z=14.2 in assembly coordinates; outer contact surface lies on case underside Y=−40. Stem centres X=−7/+7, Z=18.0. Retain insert and contacts with insulating adhesive and strain-relieve wires. Keep metal heads flush; recess/adjust dock supports if heads protrude. Connect contacts only to USB plug VBUS (5 V) and GND. Do not connect dock power to BAT or 3V3. Use a proper USB power pigtail/breakout and check continuity/polarity rather than wire colour. Keep board's other USB input unplugged during dock use unless its schematic has been checked for backfeed.

Dock bore centres X=−7/+7, Y=−1.5; top bearing floor Z=6. Fit spring pins for approximately 0.5–1 mm compression when seated; actual travel depends on purchased parts. Route wires underneath, secure, and add feet providing at least 2 mm cable clearance. Test mechanical contact and keying unpowered first. Fix contact polarity consistently between case and dock and measure 5 V at the internal plug before connecting the board. Dock electronics need USB-C sink CC resistors if a USB-C receptacle is used; a proper 5 V USB input module includes these. Never expose battery terminals as docking contacts.

## Validation and limits
CAD solid validity and one-solid-per-part checks completed during generation; STL mesh checks are supplied in mesh_check.json. Board-to-case collision volumes are in collision_check.json. Previews are rendered from the exported STL files. STEP/source files are editable and included.
These files are print-ready geometry for a first physical fit build, not a physically tested production enclosure. Actual board revision, battery dimensions, USB plug body and purchased contact hardware still require a dry fit. The wide access openings leave room for connectors but an internal USB plug may need to exit through a side opening. Have the service/maker check that plug arrangement before wiring the dock.

## Package contents
STL print files; STEP versions; assembly STEP; rendered preview images; geometry.py and build.py; dimension drawing; source inspection JSON/text; CAD/mesh checks. The original manufacturer ZIP can be downloaded separately from the source URL above. The build fetches it for clearance checking but does not include it in the print package.
