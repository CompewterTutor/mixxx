# Netmix — Master Todo Index

Top-level index for the rollback-network-mixing feature. The Ralph loop reads
task lines from the versioned files below via `all_todo_lines()`. Do not add
task lines directly here — edit the versioned files instead.

Plan: [plan.md](plan.md) · Spec: `.ctutor_docs/ctutor-rollback.md`

## Milestone Index

| Milestone | File | Description | Status |
|-----------|------|-------------|--------|
| v1 | [todo-v1.md](todo-v1.md) | Core rollback sync, ownership, track transfer, UI | ⏳ Pending |
| v2 | [todo-v2.md](todo-v2.md) | Rotating-key cache encryption, voice chat | ⏳ Planned |

## Ralph Conventions

- Tasks: `- [ ] \`X.Y.Z\`` — checked off by ralph on merge
- Trunk: `feat/rollback-network-mixing` (upstream `master` untouched)
- Each minor version maps to one `release/X.Y` branch cut from the trunk
- Each task maps to one `task-X.Y.Z` branch off the release branch
- Phase complete → review → merge back into the trunk
- Major versions (X.0) require human sign-off (none currently planned)
