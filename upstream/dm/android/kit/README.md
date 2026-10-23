# D2R Android port - package kit

This folder is for **your computer**. It contains no game content and ships none.

| What is inside | What it is for |
| --- | --- |
| `.claude/skills/d2r-package/` | The skill you hand to an AI agent |
| `dm/`, `docs/` | The tools and documentation that build the data package (Python, hactool, .NET 10; the skill has the prerequisites table) |
| `D2R-Android-port.apk` | The phone app (no game code; the game data comes from your package) |

## How to use it

1. Open the extracted folder in an AI agent (Claude Code or similar) and say: **"use the d2r-package skill to build my package and install it on my phone"**.
2. The agent reads `.claude/skills/d2r-package/SKILL.md` and walks you through preparing:
   - your own game files (`.nsz` / `.nsp` / `.xci`, base and update)
   - your own `prod.keys`
   - whatever tools are missing (the venv packages, hactool, the .NET 10 SDK)
3. The agent runs the packing pipeline and produces `d2r-package.zip` (about an hour; `--reuse` makes later runs a few minutes).
4. Following the skill's "install straight from adb" section, the agent installs the APK and pushes the package to your phone; you finish with two taps on the phone to import it.

## Boundaries

- **Keys and game files stay on your computer.** This kit contains no game content and uploads none; the package is produced locally from your own files.
- The imported package lives in the app's private storage, which other apps cannot read.
- The subgroup size defaults to 64 (the verified configuration). A different GPU needs the agent to check the device's real capabilities as the skill explains; do not copy the number blindly.
