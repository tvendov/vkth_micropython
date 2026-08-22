---
name: ra6m3-manual-lookup
description: "Ground answers about the Renesas RA6M3 hardware manual and this MicroPython renesas-ra project in local sources. Use for RA6M3 pin mux and package questions, peripheral capability, register and bit semantics, clocks, resets, low-power modes, interrupts, DMAC/DTC/ELC paths, timers, communications, analog blocks, memory, display/camera blocks, electrical constraints, exact manual sections, and comparisons between device documentation, generated FSP configuration, VK_RA6M3 board configuration, and port code."
---

# RA6M3 Manual Lookup

## Establish Sources

Resolve the repository root with `git rev-parse --show-toplevel`, then use these repository-relative sources:

- Searchable manual: `ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.md`
- Authoritative manual layout: `ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.pdf`
- Board configuration: `ports/renesas-ra/boards/VK_RA6M3/`
- Shared RA implementation: `ports/renesas-ra/ra/`
- FSP and device headers: `lib/fsp/`

Use the canonical Markdown filename above. Ignore `r01uh0886ej0120-ra6m3 (1).md` if it exists; it is a duplicate, not a second source.

Read `references/section-map.md` for chapter routing and search anchors.

## Lookup Workflow

1. Extract exact identifiers from the question: register, bit field, pin, signal, peripheral instance, event, mode, or error symptom.
2. Route the topic to a chapter using `references/section-map.md`.
3. Search the Markdown with literal, case-sensitive identifiers first. Use broader case-insensitive terms only when exact search fails.
4. Open narrow context around each relevant match. Distinguish table-of-contents hits from body text; body chapter titles do not always have Markdown heading markers.
5. Follow references such as notes, usage constraints, initialization procedures, and module-stop requirements before concluding.
6. Cross-check the source that owns the claim, following the rules below.
7. State the conclusion, evidence, constraints, and any inference separately. Include local file and line references when answering about code.

Start with commands such as:

```powershell
rg -n -F -C 8 -- "IELSRn" ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.md
rg -n -F -C 8 -- "P613" ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.md
rg -n '^(#{1,6} )?34\. Serial Communications Interface \(SCI\)$' ports/renesas-ra/boards/VK_RA6M3/r01uh0886ej0120-ra6m3.md
```

Do not treat the first match as sufficient. Register names commonly occur in the contents, register summary, bit description, operation, and usage-note sections.

## Cross-Check Rules

For pin or alternate-function questions, check all applicable sources:

- Manual package/pin and PFS tables
- `ports/renesas-ra/boards/VK_RA6M3/ra6m3_af.csv`
- `ports/renesas-ra/boards/VK_RA6M3/pins.csv`
- `ports/renesas-ra/boards/VK_RA6M3/ra_gen/pin_data.c`
- `ports/renesas-ra/boards/VK_RA6M3/mpconfigboard.h`

For clock, reset, interrupt, or event-routing questions, cross-check the manual with `ra_gen/bsp_clock_cfg.h`, `ra_gen/vector_data.*`, `ra_config.h`, generated FSP data, and the relevant file under `ports/renesas-ra/ra/`.

For peripheral availability, separate these questions:

- Does the RA6M3 silicon support it?
- Is it bonded out on the selected package?
- Is it configured for VK_RA6M3?
- Does the current MicroPython port implement and expose it?

Do not infer one answer from another.

## PDF Verification

Treat the Markdown as a high-quality search index and prose source, not as a replacement for the PDF. Verify against the PDF whenever the answer depends on:

- Table row/column association or merged cells
- Register diagrams, reserved bits, reset values, or field widths
- Pin-function matrices and package variants
- Timing diagrams, formulas, figures, footnotes, or usage-note scope
- Electrical limits, test conditions, units, or min/typ/max columns
- Exact wording or page references
- Text whose order is ambiguous after extraction

Render or inspect the complete relevant PDF page, including the table title, notes, continuation pages, and footnotes. Never reconstruct a table relationship from flattened Markdown text alone.

## Evidence Rules

Use the hardware manual as authority for device behavior and constraints. Use generated FSP configuration and board files as authority for this board's selected setup. Use shared port code as authority for current MicroPython behavior.

Label conclusions as inference when they combine multiple sources. Report conflicting evidence instead of silently choosing one source. If a required package variant, signal direction, or operating condition is unknown, name the missing condition.

Keep quotations short. Prefer a concise technical explanation with exact identifiers and source locations.
