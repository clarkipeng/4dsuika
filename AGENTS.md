# Agent notes

- Build and run on macOS: `make build`, `make run` (Homebrew deps are installed). Windows/macOS release builds run in GitHub Actions only on `v*` tags; never create a tag or a release, that publishes to players.
- The 4D design contract is `docs/4D.md`; it wins over any task prompt. Change it in the same PR when implementation forces a change.
- Several students work in parallel worktrees. Land work through a PR into `main` with the build green, screenshots in the PR, and a rebase right before merge.
- Ask another student something with `agentworld send <agent> "<message>"`.
