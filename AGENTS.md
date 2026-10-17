# ESP-Mosaico multi-app repository

Read and follow [the shared engineering and device rules](firmware-workspace/AGENTS.md)
and its task-relevant skills. The following layout overrides apply to this repository:

- The repository root is the Mosaico workspace. Run `python3 mosaico.py` here.
- The existing app stays in `tuantuan/`. New independent apps are created in
  `apps/<name>/` because the pinned initializer requires a dedicated projects
  directory. Never nest a new app inside another app.
- Shared upstream tools, submodules, skills and guides live in `firmware-workspace/`.
  Treat its exported source revision as a pinned dependency snapshot, not this
  repository's own Git HEAD.
- Firmware, UI, simulator, server and release artifacts belonging to an app stay
  in that app's directory. Apply [application rules](tuantuan/AGENTS.md) to firmware
  modules in all apps; copy those rules into newly created app directories and update their parent
  link to `../../AGENTS.md`.
- Before actually creating a new app, query the official workspace's latest tag
  and compare it with `firmware-workspace/SOURCE_REVISIONS.json`. Recommend an
  update if needed and obtain confirmation before changing pinned versions.
- Use explicit `--project tuantuan` (or the selected `apps/<name>` app) for device work.
  Moving directories does not authorize switching devices, erasing credentials,
  changing partition contracts or upgrading upstream dependencies.
- Keep real ENV files, data, models, builds and device evidence out of Git.
