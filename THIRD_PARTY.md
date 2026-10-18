# Third-party materials and dependencies

The root [LICENSE](LICENSE) applies to original Music Controller material. It does not replace dependency licences, trademark rights or manufacturer terms. Keep upstream copyright, licence and attribution notices when redistributing their material. A binary's bundled dependencies may impose obligations beyond the licence on our original source.

## Firmware

- [ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.4.2): principally Apache-2.0, with separately licensed third-party components. Preserve the notices for the components included in a firmware build.
- [LVGL 8.3.11](https://github.com/lvgl/lvgl/tree/v8.3.11): MIT.
- [Waveshare BSP 1.0.7](https://components.espressif.com/components/bobscott45/waveshare_esp32_s3_touch_lcd_4_3/versions/1.0.7): MIT; its notice includes Waveshare and BSP maintainer attribution. Transitive managed components retain their own licences.

Exact dependencies are recorded in `firmware/esp32/main/idf_component.yml` and the build's resolved dependency metadata. The project licence does not relicense ESP-IDF or managed components.

## Windows installer

The original installer source is Apache-2.0. The frozen executables also bundle Python and third-party packages from `installer/requirements.txt`, plus transitive dependencies.

In particular, [esptool 4.8.1](https://github.com/espressif/esptool/tree/v4.8.1) is **GPL-2.0-or-later**. It is not relicensed under Apache-2.0 by this repository. Redistributors of the bundled installer must meet applicable GPL requirements, including corresponding-source obligations. Upstream sources and build instructions for our installer are available in this repository and the linked dependency project. Do not describe the complete executable as containing only Apache-licensed software.

[PyInstaller](https://pyinstaller.org/en/stable/license.html) has a bootloader/bundling exception; that exception does not waive the licences of bundled dependencies. Python, pyserial, the NVS generator and other packaged dependencies retain their own notices. Review the actual bundled versions before redistribution.

## Gateway

The gateway communicates with Music Assistant through its API; Music Assistant is a separate service, not bundled server code. [Music Assistant Server](https://github.com/music-assistant/server) is Apache-2.0.

Gateway dependencies are listed in `gateway/requirements.txt`. aiohttp, Pillow, CairoSVG, defusedxml, their transitive dependencies and the container base/system libraries retain their upstream licences. CairoSVG and Cairo include LGPL-licensed components. Distributing a gateway container requires retaining the relevant dependency/system notices and complying with their terms; the root licence covers our original gateway code only.

## Provider logos and artwork

Provider logos in `tests/native-ui/fixtures/` are third-party test assets, not project-owned artwork. Their source repositories, blob identifiers and attribution are recorded in [PROVIDER_ICONS.md](tests/native-ui/fixtures/PROVIDER_ICONS.md). Spotify, BBC Sounds, Navidrome and other service names/logos remain the property of their respective owners. Source-code licensing does not grant trademark permission. Runtime artwork/logos supplied by Music Assistant, providers or user uploads retain their owners' rights.

## Enclosure and manufacturer references

Original CadQuery code, generated case/dock geometry and original renders are Apache-2.0. Manufacturer PCB/glass models, drawings and product names remain Waveshare's material. The manufacturer model is downloaded separately for clearance checks and is not included in the print package. Reference provenance and the pinned archive hash are documented in [enclosure guidance](hardware/enclosure/README.md). No claim is made to relicense Waveshare's source model.

This inventory identifies the main licensing boundaries; it is not a claim that every transitive dependency is Apache-2.0. When changing dependencies or packaging, preserve their notices and review redistribution requirements for the exact shipped versions.
