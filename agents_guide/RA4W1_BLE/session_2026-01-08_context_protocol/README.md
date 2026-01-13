# Session 2026-01-08 - Context retention and "empty message" loop prevention

## Problem observed
We repeatedly fell into a loop where the agent claimed the user sent an "empty" message and re-asked the same clarification question many times.

## Likely root causes (not blaming the user)
1. **Transport/UI artifact:** sometimes a message arrives as metadata-only (e.g. supervisor block) or with no visible user text.
2. **Agent behavior bug:** treating "empty input" as "no prior context" and restarting discovery.
3. **No pinned decisions:** the conversation lacked a single, explicit "Current Goal" + "Decision Log", so the agent kept asking.

## Hard rules for the agent (must-do)
1. **Never restart discovery on empty input.**
   - If the latest message is empty/metadata-only, assume artifact.
   - Ask for resend *once*, but continue using the last confirmed decisions.
2. **Keep a pinned status block in every response:**
   - Current Goal
   - Confirmed Decisions (board, API choice, minimal target)
   - Open Questions (max 1-2)
3. **Do not re-ask a question already answered.**
   - If uncertain, quote the last stored decision and ask for correction, not repetition.
4. **Record rejected options** (so they are not suggested again).

## Lightweight communication protocol (recommended)
### Message format from user
- `GOAL:` one sentence
- `DECISIONS:` bullet list (board, variant)
- `NEXT:` what to do now

Example:
- GOAL: Advertising + connect event on EK_RA4W1
- DECISIONS: Use renesas_ble module, legacy advertising
- NEXT: Implement R_BLE_Execute pump and start advertising

### Agent response format
- **Status:** what is true right now (code + runtime)
- **Plan:** next 1-3 steps
- **Need from user:** at most 1 question

## Recovery scenarios
### Scenario A: Message appears empty
Agent action:
1. Say: "I received a message without user text; might be transport artifact."
2. Continue with last known Goal/Decisions.
3. Ask: "Please resend only the missing line: GOAL/DECISIONS/NEXT".

### Scenario B: User answers briefly (e.g., "EK_RA4W1")
Agent action:
1. Store it into the Decision Log.
2. Ask only the *next* missing field (one question).

### Scenario C: Contradictory instructions
Agent action:
1. Quote the conflict.
2. Ask which one wins.
3. Do not proceed with risky actions until resolved.

## Where to keep the single source of truth
- Full chat transcript (everything from the chat):
  - `C:\msys64\home\teodor\agents_guide\RA4W1_BLE\RA4W1 chat history ble project.txt`
- Decision Log (single source of truth for Current Goal / Decisions / Rejected / Need from user):
  - `C:\msys64\home\teodor\agents_guide\RA4W1_BLE\DECISIONS.md`
- Repo-local protocol note (this file):
  - `renesas_micropython\agents_guide\RA4W1_BLE\session_2026-01-08_context_protocol\README.md`

When a decision changes, update `DECISIONS.md` first, then act.

## Real current repo status (as of this note)
- RA4W1 BLE scaffolding exists (event queue, wrapper, renesas_ble module).
- BLE wrapper is still largely stubbed; runtime functionality requires FSP calls + event pump integration.
- Build integration documents exist under `agents_guide/RA4W1_BLE/` (see BUILD_SUCCESS.md, BLE_LIBRARY_STATUS.md).

