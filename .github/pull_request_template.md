## What

<!-- What this pull request changes, in a few sentences. -->

## Why

<!-- The problem it solves, or the task or issue it implements (Closes #<n>). -->

## Consumer impact

<!-- For each consumer: what it must change when it moves its pin to a release with this, or
"none". Grep their sources before changing a public signature. A breaking change also goes under
**Breaking** in CHANGELOG.md, with a migration note. -->

- contour (vendored):
- endo:
- fastcached:
- tuidu:
- Lightweight `dbtool`:
- morph (including its WebAssembly build):

## Tests

<!-- The tests that cover this, and that they were seen to fail without the change. Which
presets were run locally; for a subset module, the emscripten job. -->

- [ ] CHANGELOG entry under `[Unreleased]` (or not user-visible)
- [ ] Documentation updated (or nothing it describes changed)
