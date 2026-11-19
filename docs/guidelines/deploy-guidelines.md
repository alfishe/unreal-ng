# Deploy Guidelines

Rules for putting a build on a target device for a user to test (Steam Deck and other handhelds). How-to steps live
in the recipes: [Steam Deck](../../.recipe/testing/steamdeck.md), installer details in
[install/steamdeck/README.md](../../install/steamdeck/README.md).

## A deploy is a full install

- Deploy the packaged bundle (`install/steamdeck/package.sh <build>`, then the bundle's `install.sh`), built from the
  commit the user is to test. Never hand the user a device where binaries or an unpacked AppDir were copied over an
  install: the Steam entry, the launcher, and data made at packaging time (the configs' disk images extracted by
  `cmake/ExtractConfigImages.cmake`) go missing, and the app reports config errors.
- Quick binary copies for your own iteration are fine; finish with a full bundle install before the user tests.
- After the install, start the app the way the user does (Game Mode: `steam://rungameid/<id>`) and read its log for
  errors (missing configs, ROMs, images) before reporting the deploy done.

## Every deploy re-registers the app

- Each install re-registers the Steam library entry: Steam stopped, entry and artwork rewritten with the stable app id,
  Steam started again, entry checked. The installer does this by itself; a deploy whose output lacks
  `Steam library: Unreal NG registered` failed and is not reported as done.
- It must work in Game Mode too, where the session restarts Steam at once: the installer masks the Steam unit while it
  writes and always restores it. Do not ask the user to switch to Desktop Mode or to add the game by hand.
- A running game blocks the change: say so, do not kill the game.

## Use the deploy tools, keep them generic

- Build, install and check with [tools/deploy/steamdeck](../../tools/deploy/steamdeck/README.md) (`remote-build.sh`,
  `deploy.sh`, `check_machine_picker.py`), not with scripts of your own in `scratch/`: a step that works for you
  belongs there, so the next session has it.
- A tool takes hosts, folders and keys from its arguments, the `UNREAL_DEPLOY_*` variables and `~/.ssh/config`; it
  never carries them. A new variable gets its row in
  [environment variables](../emulator/environment-variables.md).
- On a build host you work in a checkout of your own, named after the task; never in another session's.

## Check the build you installed, not the user's session

- After the install, check what the user will touch (the handheld menu, a game they asked for) on the Deck itself,
  in an instance of your own: offscreen, own ports and settings folders, windowless (`check_machine_picker.py` is the
  pattern). Stop it afterwards.
- Never drive the user's running app on the Deck to test: switching its machine or resetting it restarts their
  session. Closing it is the installer's job, at an install the user asked for.

## No local data in the repository

Docs, recipes, scripts and commit messages name hosts, users and accounts with placeholders (`deck@<host>`,
`userdata/<account>`): no IP addresses, host aliases from your SSH config, Steam account ids, user names or key files.
Keep those in your own environment.
