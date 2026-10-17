# Contributing to Music Controller

Bug reports, documentation fixes, firmware/gateway improvements and hardware testing are welcome. For general background, see [How to contribute to open source](https://opensource.guide/how-to-contribute/).

**AI coding agents: read [AGENTS.md — required repository instructions](AGENTS.md) before making changes. Read additional AGENTS.md files in every directory you modify, including [firmware guidance](firmware/esp32/AGENTS.md) when working on firmware.** Human contributors should also understand these architecture and validation rules.

## Report a problem

Search [existing issues](https://github.com/lewiskingy/music-controller/issues) first. Include the component, release tag/commit, exact board model, Music Assistant version, expected/actual behavior and reproduction steps. Attach relevant redacted logs or screenshots. For installer failures include Windows version, mode (new setup/update) and the error; for display/power issues include actual hardware observations.

Never attach `.env`, Wi-Fi passwords, device tokens, Music Assistant credentials, exported gateway-key files or private `controller-nvs.bin` images. Redact logs before posting. If you find a credential exposure, do not repost the secret; notify the maintainer without including it in a public issue.

## Propose a change

Discuss substantial features, protocol changes and new board support in an issue before extensive implementation. Explain the user problem and intended behavior. Other ESP32 screens require an explicit firmware/profile port and physical validation, not just an installer label.

## Submit a pull request

1. Fork/branch from current `main`. Keep the change focused.
2. Follow the relevant README, [architecture](docs/architecture.md) and [protocol contract](contracts/music-assistant.md). Reuse existing controls and procedures.
3. Update user documentation whenever behavior, configuration, installation or compatibility changes. Keep the README and Getting Started consistent; move investigation history into developer notes.
4. Run the relevant checks from [development and CI](docs/development.md), component guides and workflows. Documentation-only changes need link/command review rather than a hardware build.
5. Describe the problem, resulting behavior, checks actually run and any physical validation still outstanding. Include screenshots for visible changes where practical.

State when gateway and firmware must be updated together, which version/deployment order is needed, and whether consumers need a release-pin/configuration update. Preserve credentials/preferences across ordinary updates. Never claim a simulated test proves real display timing, battery runtime or Music Assistant interoperability.

Maintainers review and release changes. Opening a PR does not authorize merging, tagging or deploying to consumers. Atlas is a separate deployment consumer of this repository's gateway, not a prerequisite for contributors or users.
