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
- Pick the agent with `--agent <name>` (default is `build`); see "Which agent" below.
- Follow-ups go to the same session: `opencode run -s <session-id> "..."` (or `-c` for the last one).
- Long runs go in the background; Claude reads only the tail of the output.
- The session runs inside the background service (`opencode serve --service`), so stopping the `opencode run`
  command does not stop the work: it carried on running tests and editing files (2026-10-25). To stop a session,
  stop the service process and any processes under the piece's own build/test dirs, then start the service again.

## Which agent

Agents live in Dan's user config, `~/.config/opencode/opencode.json` (not in the repo; the OpenRouter
key stays in OpenCode's own `auth.json`). All models go through OpenRouter. Prices are per million
tokens, input/output, as of 2026-10-17.

| Agent | Model | Price | Use it for |
|---|---|---|---|
| `build` (default) | `deepseek/deepseek-v4.1-flash` | $0.06 / $0.14 | Routine pieces from a clear brief, builds, parity runs, follow-up fixes. |
| `deep` | `deepseek/deepseek-v4-pro` | $0.66 / $1.98 | Harder pieces: a new subsystem, tricky reverse-engineered layouts, or a piece `build` came back blocked on twice. |
| `auto` (dropped) | `openrouter/pareto-code` | varies | Don't use. Dropped 2026-10-25: the router picked Claude Fable and spent about a dollar on a few minutes of reading. |
| `debug` | `anthropic/claude-sonnet-5.5` | $2 / $10 | Hard debugging only: parity mismatches or crashes `deep` couldn't crack. Brief it with the failing test and what was already tried. |

**Cheap models only (Dan, 2026-10-25):** use `build` or `deep` (DeepSeek); Kimi or GLM via `-m openrouter/<model>` are fine too. No router and no Fable or Opus; the `debug` agent stays for hard debugging only.

Start with `build`; step up one level only after a blocked or wrong result, and say so in the brief.
Subagents (`explore`, `general`) and housekeeping (titles, summaries, compaction) use the cheap models.
A one-off model override is `-m openrouter/<model>`.

## The brief (one file, short)

1. Piece name and branch to commit on.
2. Pointer to the restart notes / doc section that defines the piece (don't paste them).
3. Done means: builds clean, named parity tests pass, committed, and the part meets the rules in
   [CONTRIBUTING.md](../CONTRIBUTING.md): "How replacement code is written" (the game's own helpers, calls
   through `addr::` entries, the shape of the doc section, and no bare addresses in code) and "Pull
   requests" (what a part's doc records, and results from the PR's own head); and its `source` fields follow
   the [address README](../data/addresses/README.md).
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
(v2.0.32, background service, default model). The background service reloads
`opencode.json` on its own when it changes; no restart is needed.
