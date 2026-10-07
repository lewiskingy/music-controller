# Hardware direction

Target: Waveshare ESP32-S3 4.3-inch 800×480 capacitive touch display (standard board, not the industrial 4.3B variant), with 8 MB PSRAM and 16 MB flash.

- ESP-IDF / FreeRTOS, LVGL and touch/display board support package.
- Single-cell protected 3.7 V LiPo with verified connector polarity and board-specific charging compatibility.
- USB-C access for initial flashing and recovery.
- Future enclosure with battery retention, rounded edges and serviceable back.
- Future angled magnetic dock with pogo contacts delivering regulated 5 V to a **verified supported power input**, never directly to the battery.
- Assess power path, USB-C VBUS injection/backfeed protection and dock-contact safety on the actual PCB before wiring.
- Measure current consumption, battery reporting and wake latency before setting runtime targets.
- Optional physical encoder only after touch UX is validated.

The exact enclosure dimensions, pinouts, dock contact arrangement and OTA partition sizes remain open until hardware inspection.
