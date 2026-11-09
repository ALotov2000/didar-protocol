---
name: plan-format
description: Required format for implementation plans Claude writes in this repo, stored as 01-sdlc/NNNN-<slug>/plan.md. Use whenever creating or updating a plan, including in plan mode.
---

# Plan format

- **Location:** `01-sdlc/NNNN-<slug>/plan.md`, where `NNNN` is the next 4-digit number.
- **Sections:** Context, Scope, Execution order, Layout (if files are added), Code diffs, Test cases, Verification.
- **Code diffs:** every change to main code (sources, headers, build files, configs, skills) as complete unified diffs (`diff --git`, new files from `/dev/null`). No elisions such as `...`.
- **Interface first:** the library's public header (`01-code/include/didar.h`) is always part of the diffs when it changes; it is our code's interface.
- **Requirement test cases:** one table per requirement in `00-docs/requirements.md` that the plan touches, with one case per scenario: every path and error branch in its sequence diagram. Columns: `ID` (`<requirement>.<n>`, e.g. `FR-1.2`), `Scenario`, `Steps`, `Expected`. Requirements the plan does not implement are listed as deferred with the reason.
- **Requirement test code:** diffs for the requirement tests (`01-code/tests/`), one function per test case ID. They use only the interface: `didar.h`, the documented wire format, and the built programs. Never internal `src/` headers.
- **No unit tests:** never include unit tests of internal functions in a plan.
- **Order:** tests are written first and must fail before code is written (test-driven skill).
