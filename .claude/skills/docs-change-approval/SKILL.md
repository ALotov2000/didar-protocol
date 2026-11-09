---
name: docs-change-approval
description: Mandatory human-approval gate for ANY change to files under 00-docs/ (create, edit, rename, delete). Use whenever you are about to modify the project docs, even a typo fix. The change is left uncommitted so it shows in the git change log, a person must confirm it, then it is committed immediately; if they decline it is reverted.
---

# Docs change approval

Every change under `00-docs/` needs explicit confirmation from a person. No exceptions, no batching with unrelated work.

## Procedure

1. **Start clean.** Run `git status --porcelain -- 00-docs`. If there are uncommitted doc changes you did not make, stop and ask the user what to do with them first.
2. **Make the change** in the working tree only. Follow the `docs-template` skill. Do not stage or commit yet.
3. **Pass the checks.** The PostToolUse hook runs `.claude/hooks/check-docs.sh` (markdownlint + mermaid rendering). Fix every failure before asking for approval. You can also run `npm run lint:docs`.
4. **Show the change.** The change is now visible in the git change log (Source Control panel / `git status`). Also print `git diff -- 00-docs` and list untracked new files (`git status --porcelain -- 00-docs`) so the person sees exactly what changed.
5. **Ask for confirmation** with `AskUserQuestion`: options "Confirm and commit" and "Reject and revert". Name the changed files in the question.
6. **On confirm, commit immediately**, only the doc files you changed:

   ```bash
   git add -- <changed doc files>
   git commit -m "docs: <what changed>"
   ```

   Use the `commit-message` skill for the message if available.
7. **On reject (or no clear confirmation), undo it:**

   ```bash
   git restore --staged --worktree -- <changed tracked files>
   git clean -f -- <new untracked files you created>
   ```

   Then confirm `git status --porcelain -- 00-docs` is empty and tell the user the change was discarded.

## Rules

- Silence, "maybe", or an unrelated reply is **not** a confirmation. Treat it as reject.
- One approval covers one shown diff. If you change the docs again after approval, repeat the procedure.
- Never use `--no-verify` and never weaken `.markdownlint-cli2.jsonc` to get a change through.
