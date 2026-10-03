# RTR-OS — directives for Claude Code

RTR-OS (Real-time Raspberry Operating System) is a real-time operating system
for the Raspberry Pi 4 Model B, in C, for control and automation. The plan is
`docs/PLANO.md` (Portuguese): read the decisions (section 1) before changing
anything. The virtual bench that probes it lives in its own repository,
`RTR-Bench/` (nested here, ignored by this git).

## Working style

- **Plan before writing code.** Open-ended requests are discussed with the
  user first, one decision at a time; implement only what was agreed.
- **No flexibility that burdens the project.** One way of doing each thing.
- Commit and push only when the user asks. The repository is public
  (Apache-2.0); never commit secrets, personal data or the conversation
  export kept in the root.
- **No attribution trailers in commits**: do not add `Co-Authored-By:` lines
  (or any other AI attribution) to commit messages or pull requests.

## Language

- Product in **English**: web page, console messages, code, comments,
  scripts, README, commit messages.
- Planning documents (`docs/PLANO.md`, `docs/ESTATISTICAS.md`) in Portuguese.
  Conversation in Portuguese.

## Code rules

- Coding rules of decision D9 in `docs/PLANO.md`: `-Wall -Wextra -Werror
  -Wconversion -Wshadow -Wundef -Wvla -Wstrict-prototypes
  -Wmissing-prototypes -Wimplicit-fallthrough`, clang-tidy (`.clang-tidy`),
  no recursion in the kernel. lwIP is third-party and exempt.
- Build with `scripts\build.ps1`; test in the emulator with
  `scripts\run-web.ps1` (qemu-pi4 in WSL) before a commit.
- Every field of the web configuration an app cannot configure is hidden;
  real-time comes from the program header, never from a selectable class.
