# TODOS

## Inspector

### Support Inspector Attach To External BzPeri Processes

**What:** Let `inspect --live` attach to BzPeri processes outside the `bzp-standalone` managed demo path via an explicit registration and heartbeat contract.

**Why:** The Phase 1 inspector is useful for the bundled workflow, but this is the step that turns it into a real product debugging tool for downstream apps.

**Context:** This review intentionally reduced Phase 1 scope to BzPeri-managed sessions only. That keeps the first version honest and shippable, but it leaves a clear expansion point for real application processes. Start with a small registration channel, heartbeat, stale-session cleanup rule, and attach failure semantics instead of process discovery magic.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 inspector session contract and event aggregator stabilization

## Distribution

### Add Raspberry Pi APT Packaging For Workflow Tools

**What:** Extend the existing package publishing flow so Raspberry Pi users can install the workflow-enabled `bzp-standalone` surface with `apt install`, not just from source.

**Why:** The target environment is often an SSH session on Ubuntu or Raspberry Pi, so shipping only the source-build path leaves one of the most important real-world setups as a second-class experience.

**Context:** This review kept Raspberry Pi APT distribution out of Phase 1 to preserve scope. The current repo already has Ubuntu-oriented APT publishing and arm builds in CI, so this is not starting from zero, but it still needs packaging verification for the new subcommand workflow and docs that treat Pi as a first-class install path.

**Effort:** M
**Priority:** P2
**Depends on:** Phase 1 CLI contract and packaged workflow stabilization

## Design

### Promote Terminal Workflow Rules Into DESIGN.md Before Adding New Surfaces

**What:** Create a repo-level `DESIGN.md` that captures the terminal workflow vocabulary, state language, status semantics, and next-step formatting before adding a TUI or browser mirror.

**Why:** Phase 1 can live inside the approved plan, but the moment BzPeri grows a second presentation surface, the product language can drift unless the rules are centralized.

**Context:** The design review intentionally kept Phase 1 lightweight by embedding terminal design rules directly in the plan instead of creating a full design system first. That is the right tradeoff now. It stops being enough once TUI layouts, browser mirrors, or richer exports exist. Start by lifting the stable parts of the Phase 1 contract: `PASS/WARN/FAIL` semantics, output framing, next-step line formatting, truncation disclosure, and tone guidelines.

**Effort:** S
**Priority:** P2
**Depends on:** Phase 1 terminal workflow implementation and observed stable output patterns

## Completed
