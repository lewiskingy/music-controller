# Hardware and buying guide

## Supported board

Current released firmware and the installer support **one board profile**:

| Board | Firmware / case support |
| --- | --- |
| Original [Waveshare ESP32-S3-Touch-LCD-4.3](https://www.waveshare.com/esp32-s3-touch-lcd-4.3.htm), 800×480 capacitive GT911, 16 MB flash / 8 MB PSRAM | Supported firmware; portrait 480×800 UI; matching case designs |
| Waveshare 4.3B/C, industrial variants, 4-inch 480×480 boards or other ESP32 screens | No released profile or matching case promised; require a port, build and hardware validation |

Check the exact product/PCB model before purchase. An ESP32-S3 chip alone does not establish display, touch, pinout or case compatibility. Manufacturer references: [board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3).

## Minimum USB-powered build

- Supported board above.
- Data-capable USB cable matching the board's USB-C connection, for flashing and recovery.
- Suitable regulated USB supply; the enclosure guide recommends **5 V / 2 A** for operating/charging headroom.
- A 2.4 GHz Wi-Fi network with access to the gateway and a flashing computer. The packaged installer supports Windows x64; other systems use the manual firmware guide.

No battery, printed enclosure, soldering or charging-contact hardware is needed to test the controller on USB. Music Assistant, an output player and an always-running gateway host are also required: follow [Getting Started](getting-started.md).

## Battery and enclosure

A battery is **optional** for portable use. The case assumes a protected single-cell **3.7 V 1200 mAh 103440 LiPo**, approximately **40×34×10 mm**, in a **44×38×12 mm** tray cavity. Measure the complete wrapped pack and protection circuit; capacity or model code alone does not guarantee fit.

The board battery connection is described as PH2.0. Verify the actual connector, **positive/negative pin polarity**, battery protection and permitted charging current against your board and pack before connection. The guide specifies 580 mA board charging; the battery must permit it. A plug that fits can still have reversed polarity. Never connect dock 5 V to battery terminals.

The firmware does not reliably detect USB power automatically and runtime has not been measured. Auto uses the battery timeout; select USB/external manually when appropriate. Backlight-off is not deep sleep.

## Printable case and optional dock

The [enclosure guide](../hardware/enclosure/README.md) provides renders, CAD source, downloadable-build instructions and [print/assembly details](../hardware/enclosure/ASSEMBLY.md). The design is a **first physical fit prototype**, checked against the manufacturer's model but not yet physically validated.

| Build | Printed parts / extra work |
| --- | --- |
| USB-only case | `01_front`, `02_rear_extension`, `03_back`, `04_battery_tray`, `06_contact_blank`; USB power/charging remains accessible |
| Case with charging contacts | Use `05_contact_insert` instead of `06_contact_blank`; add separately purchased contacts, wiring and a suitable internal USB pigtail |
| Upright cradle | Add `08_dock`; without electrical parts it is a passive stand |

Download `controller-enclosure-<commit>` from a successful **Enclosure CAD and renders** Actions run, then extract `print-package.zip`. Durable enclosure releases, when published, use separate `enclosure-v…` tags on the [releases page](https://github.com/lewiskingy/music-controller/releases). Actions artifacts expire after 30 days. Assembly STEP files are viewing references; print the individual STL files.

Use the assembly guide's complete specification: PETG, 100% scale in mm, 0.20 mm layers, 4 walls and 25–30% infill, with rear-cover supports. A printing service can supply the parts; printing does not include electrical assembly. Additional case parts include four M3×16 mm screws, soft bearing pads/tape and removable battery retention. The optional charging dock requires separate electrical parts and validation listed in the assembly guide. Both insert choices retain USB access; only 05 has contact holes.
