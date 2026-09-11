# Changelog

## v0.2.17 — Initial experimental public release

- Publish the exact physically validated BroadcomVTD.kext 0.2.17; no cosmetic
  version rebuild. Local publication preparation does not itself publish a tag.
- Automatic native/verified-AppleVTD mode selection, zero required positive
  BroadcomVTD arguments.
- General private-TX reset-lifetime correction includes the native bulk-reset
  family: caller PC is telemetry, not ownership authority; same-geometry
  invalidation preserves observed revision but invalidates generation/tickets.
- Repeated Sleep/Wake and controlled wireless validation passed on the
  documented X299 / BCM943602CDP / D11 rev49 Tahoe configuration.
- Preserve mapper-backed RX, golden chained/private TX, native packet
  continuation, normal completion, non-latching NoCredit and fail-closed checks.
- Add public source provenance, portable build tooling, host/static CI and
  release/tag identity checks. CI outputs are not physically validated releases.

The reset/DISABLED/300-us rev49 premise remains C/C+, EXPERIMENTAL.
Historical experiments and internal raw captures are intentionally not imported.
