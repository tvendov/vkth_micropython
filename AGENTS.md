# Repository Agent Rules

## Commit Requirements

Every commit created by an agent in this repository must satisfy all of the
following requirements:

- Use a concise imperative subject prefixed by one or more clear scopes, for
  example `[I2C][RA6M3] add SCI Simple IIC support`.
- Include a commit body with the exact sections `Discussion`, `Reason`,
  `Changes`, `Dependencies`, and `Validation`.
- In `Discussion`, record the concrete decisions that led to the commit,
  including board-name aliases, approved scope, rejected alternatives, and
  known limitations when they affected the implementation.
- Describe only changes that are actually part of the commit.
- Record the exact build and test results. Distinguish validation of the
  isolated commit from a later combined-worktree build. Use `Not run` and state
  why when no validation was performed; never imply that a test passed when it
  was not run.
- Create one unique annotated Git tag for every commit. Use a descriptive,
  lowercase, kebab-case name ending in a version, for example
  `renesas-ra-sci-i2c-v1`.
- Make the tag name and annotation identify the concrete decision or hardware
  scope from `Discussion`; broad category-only tags are not acceptable.
- Give the annotated tag a message that identifies the commit purpose and its
  validation status.
- Push an annotated tag only together with the commit it identifies. Do not
  move or replace an already published tag without explicit user approval.

Do not create subject-only commits or lightweight tags.

## Evidence Requirements

All technical analysis, status reports, and implementation decisions for this
repository must contain zero unsupported factual claims.

- Verify every material claim against primary evidence before presenting it as
  fact. Primary evidence includes repository source and configuration files,
  generated FSP files, linker maps and symbol tables, compiler output, test or
  HIL logs, and the official MCU or peripheral documentation.
- Identify the exact evidence for numerical and behavioral conclusions using a
  file and line, symbol and address, build output, test result, or hardware
  observation. Reconcile numerical totals explicitly.
- Do not convert an assumption, recollection, likely explanation, or indirect
  indication into a factual statement. If direct evidence is unavailable,
  report the point only as `UNKNOWN` or `NOT VERIFIED` and state what evidence
  is required to verify it.
- Distinguish source presence, compilation, linker retention, runtime
  initialization, and observed hardware behavior. Evidence for one stage does
  not prove the following stages.
- For actual board behavior, use HIL evidence. A successful build or a matching
  configuration proves neither successful initialization nor correct hardware
  operation.
- Correct and explicitly retract any earlier statement that conflicts with
  newly inspected primary evidence.
