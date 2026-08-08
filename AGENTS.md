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
