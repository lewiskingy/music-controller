# Portrait controller case and charging dock — print and assembly guide

## Printing-service order

Print one each of `01_front.stl`, `02_rear_extension.stl`, `03_back.stl` and `04_battery_tray.stl`. Choose **one** of `05_contact_insert.stl` (contact charging) or `06_contact_blank.stl` (USB-only). Add `08_dock.stl` for the portrait cradle. Seven alternative STL files are supplied; a complete docked build needs six printed parts.

All units mm; **100% scale**. PETG, 0.20 mm layers, 4 perimeters, 5 top/bottom layers, 25–30% infill, tolerance target ±0.2 mm. Keep front, extension, back, tray and dock in their supplied flat orientations, largest flat face on bed. Rotate the contact/blank insert 90° about X to put its broad 24.6 × 7.6 mm face on the bed. Only the rear cover needs supports: place the battery compartment’s flat outside face on the bed and support the underside of the main cover skirt, 11 mm above the bed. Use removable support interface (or soluble support); keep supports away from the battery pocket and support tips. Remove supports carefully and check the 2 mm panel before assembly. Other parts need no supports. Use a brim for the rear cover if needed: its four board supports must stay straight. Deburr slide fits, contact holes and screw pilots; do not automatically rescale.

Order text: “Please print the six selected STL files in PETG, one each, at 100% scale in mm, 0.2 mm layers, 4 walls and 25–30% infill. Orient as the assembly guide specifies, support the rear-cover skirt, and preserve hole positions. Please quote the dock separately.”

`case_assembly.step` and `docked_assembly.step` are assembled viewing references, **not** single parts to print. Editable per-part STEP files are also included.

## Verified manufacturer geometry

Original Waveshare **ESP32-S3-Touch-LCD-4.3**, not 4.3B/C. Source:
https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3/ESP32-S3-Touch-LCD-4in3_3D_Drawing.zip

Pinned SHA-256: `8721fac43de47b35771c438829a3251a4c0723227d83696b5517efe7d854de15`.
The archive contains `esp32-s3-touch-lcd-4_3.stp`, dated 2023-12-13, with 693 solids. Official glass 106.10 × 67.80 mm; active display 95.54 × 54.36 mm; PCB approximately 106 × 68 mm. Mounting pattern 98 × 60 mm. STEP PCB mounting centres: X −48.950 / 49.050, Y −32.470 / 27.530; hole radius 2.150 mm; metal bosses end at Z −7.700. Total assembly STEP bounds X −53.000..53.100, Y −36.479..31.783, Z −12.100..4.800; depth 16.90 mm including connectors.

The board is physically turned 90° **with its two USB sockets towards the bottom short edge**. The portrait housing does not change firmware. PR20 uses the BSP's 90° logical rotation; confirm displayed top and physical touch alignment on the actual device before final assembly. If those differ, adjust the firmware rotation rather than wiring or mounting the dock backwards.

Model-to-case transform: translate X by −0.05 and Y by +2.47; reflect Z and translate Z by +7.10; rotate 90° around Z; translate portrait Y by +10. Case coordinates are X across the portrait screen, Y upwards, Z from front to rear. Glass front Z=2.30; rear metal bosses Z=14.80. Portrait mounting centres X ±30, Y −39 / +59. Back supports end at Z=15.00. These transforms are checked automatically.

## Final printed geometry

| Item | Dimensions / relationship |
|---|---|
| Portrait enclosure | **80 × 138 × 28 mm** main body; **39 mm** maximum at battery compartment |
| Depth stack | Front 20 + rear extension 6 + back 2 mm; local battery bulge +11 mm |
| Screen cavity | 69.3 × 127.1 mm, including lower cable bay |
| Display opening | 56 × 97 mm, centre X −2.4, Y +10 |
| Lower USB bay | Approximately 20 mm of internal extension below the board; short-edge service opening 48 mm wide |
| Long edges | Closed; service openings are on top and bottom short edges |
| Battery compartment | 54 × 48 mm rounded footprint, centre X +4 / Y −12, 11 mm rear projection |
| Rounded edges | Case plan corners R6; exposed front/back perimeter R0.8; battery compartment corners R5 and outer edge R1.2 |
| Battery tray inside | 44 × 38 × 12 mm; assumed 103440 pack approximately 40 × 34 × 10 mm |
| Portrait dock base | **96 × 96 mm**, 6 mm floor; overall height 58 mm |
| Dock slot | 80.8 mm wide × 28.8 mm deep; case bottom rests at dock Z=6 |
| Rear cradle support | 52 mm above floor; front support has a USB service relief; rear has lower screw-head clearance pockets and a central battery relief; two side rails support the main case |
| Contacts | Bottom short edge behind the USB opening, centres 14 mm apart |
| Dock key | Asymmetric notch and peg beside contacts; 0.4 mm side/depth clearance |

The main case is 11 mm (28%) thinner than the previous 39 mm constant-depth version. The battery remains in a protected local compartment rather than forcing that depth across the entire cover. The lower/offset placement shortens its route towards the lower-side PH2.0 header; lead routing and the actual connector must be confirmed on the real board. The battery tray notch opens towards that lower region. The back skirt needs print supports, as described above.

There is no separate board carrier or loose mounting spacer: the rear cover's four supports bear on soft pads on the metal bosses. The taller lower bezel houses the USB pigtail/cable bay. This is a fully printed enclosure; the earlier wooden enclosure concept is not part of these files.

## Assembly: USB-only dry fit first

1. Dry-fit plastic parts without electronics. Four **M3 × 16 mm** screws pass through the back and extension into front 2.5 mm pilots. Tap carefully for machine screws or use appropriate plastic-thread screws. Use low-profile button heads, at most 6 mm diameter and at most 0.7 mm protrusion after the shallow counterbore. The dock has two lower rear screw-head relief pockets; larger heads need a source adjustment. Do not overtighten.
2. Apply approximately 0.3 mm soft tape to the front bezel bearing area, outside the visible screen. Seat the glass with USB sockets down and the model orientation above. Protect flex cables; check the window alignment.
3. Put approximately 0.2 mm soft pads on the four rear support tips. They must meet the metal bosses, not components. Front tape plus these pads account for 0.5 mm combined clearance. Stop if the glass is stressed or the back bows; adjust only after inspecting fit.
4. Attach the battery tray within the raised compartment with thin removable tape on its sidewalls (0.4 mm clearance per side), keeping its outside base flush against the inside compartment roof, centred at **assembled X=+4, Y=−12, tray outside base Z=37**. The tray's cable notch faces the lower USB bay after the back is flipped into its assembled position. The standalone print's notch faces +Y. Keep wires clear of the supports and screw paths. Measure the battery including protection circuit and wrapping; do not squeeze or pierce the pouch. Secure it with removable tape on a broad face.
5. Verify a suitable 3.7 V single-cell battery, its connector polarity, protection and permitted charging current. The board specifies 580 mA charging. A PH2.0 plug fitting is not proof of correct polarity. Connect the battery only to the board's battery connector.
6. Choose blank or contact insert. It fits the bottom extension opening at **X=0, Y=−67.5, Z=18.2** (outer bottom face Y=−69). Use insulating adhesive to retain it; do not obstruct screws or contacts.
7. Close the case, test normal USB power/charging and confirm portrait screen/touch operation before adding dock wiring.

## Optional charging contacts and internal USB pigtail

The cradle is plastic only. Required hardware: two smooth conductive contacts (3 mm stems, approximately 5 mm heads), two spring contacts compatible with the 3.2 mm dock bores, insulated flexible wiring, strain relief, suitable short USB-C power pigtail, fused/current-limited 5 V USB input, and rubber feet. A regulated **5 V / 2 A** supply provides operating/charging headroom. Have a maker assemble the wiring if needed.

An internal plug connects the board's verified USB power input to the contacts on the same lower short edge. Use a small plug body **no longer than about 16 mm, no wider than 14 mm and no taller than 8 mm**, leaving additional wire-bend space inside the approximately 20 mm bay. Prefer a low-profile plug with a flexible lead or rearward cable exit. The CAD checks a 14 × 20 × 8 mm plug-plus-routing envelope at both socket positions. This is a clearance specification, not a claim that any commercially sold USB plug fits. Select/measure the actual pigtail before ordering. Direct wiring requires separately verifying the board's USB input schematic/pads.

Case contact stem centres: **X=−7 / +7, Y=−69, Z=22**. Keep metal heads flush with the bottom surface; adjust dock bearing clearance if heads protrude. Route insulated wires through the lower bay towards the rear contacts and secure them. Contact holes are mounting pilots: adapt the source to the purchased components if necessary.

The rigid case-to-dock transform is **(X,Y,Z) → (−X, Z−14, Y+75)**. Thus case left/right swap when viewed from the dock front. Dock bores are **X=−7 / +7, Y=+8**, with bearing floor Z=6. The dock key is at X=−27, Y=+9; its matching case notch is at X=+27, Y=−69, Z=23. Use these coordinates and continuity checks to establish polarity; do not infer it from a left/right drawing alone.

Choose pin travel and installation height to give roughly **0.5–1 mm spring compression** when seated. Secure spring-pin barrels and wires; the underbase cable groove leads to the rear. Add feet providing at least 2 mm cable clearance. A USB-C input receptacle needs a proper 5 V sink module with CC resistors. The mechanical design does not supply electronics or USB negotiation components.

Feed **USB VBUS 5 V and GND only**, never BAT or 3V3. Leave the second board USB power input disconnected during dock use unless the schematic has been checked for backfeed. Test docking and keying unpowered, measure correct 5 V at the internal plug, then connect the board. The key rejects reversed insertion; it does not replace electrical polarity checking. Do not expose battery terminals as docking contacts.

## Ventilation and heat check

The back is mostly solid, including the battery compartment. The existing top/bottom service openings provide air paths; additional ventilation slots are not included without temperature measurements. Check temperatures during sustained docked charging with the display and Wi-Fi active, following the actual battery’s charging-temperature limits. Keep the pouch clear of components and do not cover the service openings with adhesive. CAD clearance does not establish thermal suitability.

## Validation and limits

`mesh_check.json`: solid validity and closed, manifold exported STL meshes. `collision_check.json`: manufacturer-board clearance and the two USB plug/routing envelopes (when the source-model check is run). `dock_fit_check.json`: assembled case vs cradle clearance. `test_geometry.py`: portrait mount transform, contact alignment, reversed-insertion rejection, screw-head clearance, battery-pouch clearance and separate assembled-part interference. PNGs are rendered from exported STL geometry, with the screen switched off and indicative colour/finish.

These are print-ready files for a **first physical fit build**, not a physically tested production product. Verify your board revision, battery measurements, pigtail body, chosen contacts and spring travel before commissioning final assembly. A successful CAD build does not validate those purchased parts or charging behaviour.
