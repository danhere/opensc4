# Claude ↔ OpenCode handoff

Claude coordinates; OpenCode does the heavy implementation, so Claude usage stays low.

## Who does what

**Claude keeps:** choosing the next piece (from `ROADMAP.md` and the restart notes), writing the brief,
reviewing the diff and parity results, updating plans/restart notes, writing PR text, and opening the PR.

**OpenCode gets:** implementing one piece from its brief and restart notes, building, and running the
parity tests (see `docs/parity.md`). It commits on the branch Claude names; it does not push or open PRs.

## How Claude hands off

From the repo root, non-interactively (uses Dan's background OpenCode service):

```bash
opencode run --title "<piece>" --auto -f <brief.md> "Do the task in the attached brief."
```

- `--auto` approves edits/commands that aren't explicitly denied; leave it off for read-only asks.
- Follow-ups go to the same session: `opencode run -s <session-id> "..."` (or `-c` for the last one).
- Long runs go in the background; Claude reads only the tail of the output.

## The brief (one file, short)

1. Piece name and branch to commit on.
2. Pointer to the restart notes / doc section that defines the piece (don't paste them).
3. Done means: builds clean, named parity tests pass, committed.
4. Out of scope: anything not in the piece; no pushes, no PRs, no edits to plans/restart notes.
5. End with the report format below.

## How results come back

OpenCode ends its run with this block, which is all Claude reads:

```
RESULT: done | blocked
BRANCH: <branch>  COMMITS: <short shas>
BUILD: pass | fail (<first error line>)
PARITY: <n passed>/<n run>, failures: <names or none>
NOTES: <≤5 lines: surprises, guesses made, open questions>
```

Claude then reviews `git diff main...<branch>` and the parity output itself before trusting the
result, fixes or re-briefs if needed, and writes the restart notes and PR.

Verified 2026-10-17: `opencode run` from this folder answered a read-only test correctly
(v2.0.32, background service, default model).
