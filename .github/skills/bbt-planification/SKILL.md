---
name: bbt-planification
description: "Planification workflow: always ask for details before planning, then produce clear tasks and sub-tasks with scope, constraints, and deliverables."
argument-hint: "Plan <feature/change> with tasks and sub-tasks"
---

# BBT Planification

## When to Use

- Creating a plan before implementing a feature or refactor
- Breaking down a task into clear work items
- Aligning scope, constraints, and expected outputs

## Procedure

1. Ask for missing details before planning:
   - Goal and success criteria
   - Scope boundaries (in/out)
   - Constraints (no dynamic allocation, files to avoid)
   - Inputs/outputs and interfaces
   - Timeline or priority
2. Confirm assumptions and get approval to proceed.
3. Produce a plan with:
   - High-level goal
   - Tasks (numbered)
   - Sub-tasks per task (numbered)
   - Dependencies and ordering
   - Risks and open questions
4. Ensure each task has a clear owner action and a tangible outcome.

## Decision Points

- If requirements are missing or ambiguous, pause and ask before planning.
- If scope is too large, propose a phased plan.
- If constraints conflict, surface the conflict and request a choice.

## Quality Checks

- Plan starts only after required details are confirmed.
- Every task has at least one sub-task with a concrete outcome.
- Tasks are ordered and dependencies are explicit.
- Risks and open questions are documented.
