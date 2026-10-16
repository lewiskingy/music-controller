# Working on Music Controller

## Scope and architecture

This repository owns the ESP32 touchscreen firmware, its companion Music
Assistant gateway, the browser prototype, shared contracts and enclosure design.
Read the relevant README and contracts before changing a component. More specific
AGENTS.md files apply within their directories.

The gateway is a required backend for native firmware. Music Assistant credentials
stay on the gateway; devices use a separate device token. Preserve TLS certificate
verification, bounded requests/downloads and existing authentication boundaries.
Never commit credentials, private NVS images or generated secrets.

Atlas is a separate consumer of versioned gateway releases. Do not reintroduce a
second gateway implementation there. State explicitly when a firmware change
requires a gateway update, contract change or consumer release-pin update.

## Implementation and maintainability

- Encapsulate coherent operations in small, reusable procedures with clear inputs,
  outputs, ownership and failure behavior. Keep rendering, state transitions,
  networking and persistence responsibilities separate.
- Reuse existing procedures and shared controls before creating parallel versions.
  Extract repeated logic when it represents the same behavior; avoid speculative
  abstraction or generic frameworks for a single concrete use.
- Simplify code as you change it. Refactor the affected path when a change would
  otherwise add duplicated branches, deeply nested conditions or mixed concerns.
  Prefer named operations, early returns and explicit state transitions.
- Keep refactoring focused on the requested work. Preserve behavior outside that
  scope, and validate refactors with relevant behavior checks. Do not combine
  unrelated rewrites with a bug fix.
- Keep asynchronous work bounded. Capture the intended target at action time and
  reject stale results after the user changes player, provider, queue or item.
- Preserve compatibility deliberately. Document contract/configuration changes,
  preference migrations, memory costs and required deployment order.

## Product behavior

Keep the controller usable as a small dedicated touch appliance: one primary
screen at a time, large touch targets and no scrolling of the outer screen.
Reuse shared navigation and transport controls. Long lists scroll within their
content pane. Preserve exclusive provider selection and explicit preference Save
behavior unless the task specifically changes them.

## Documentation and validation

Update the relevant README when behavior, configuration, deployment, flashing or
operator instructions change. Keep user instructions there; keep agent-specific
engineering rules here. Prefer links over duplicating large documentation sections.

Run checks appropriate to the changed component. See `.github/workflows/` for the
canonical CI environment and commands. For firmware, also follow
[firmware guidance](firmware/esp32/AGENTS.md). Gateway tests live under `gateway/`;
native UI acceptance lives under `tests/native-ui/`.

Report what changed, the checks actually run, and outstanding physical acceptance.
Never describe simulated checks as proof of hardware behavior or claim CI passed
without checking the relevant commit. Do not merge, tag or deploy merely because
implementation is complete; follow the user's requested scope.
