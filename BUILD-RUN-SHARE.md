# Build, run, and share DLIVE

Every command in one place: what to type to get the app on screen, and what to send someone
who is not sitting at this machine. Nothing here is a wrapper you have to learn — the scripts are
six short files in `scripts/`, and each one prints its own usage with `-h`.

The short version:

| I want to | Type |
| --- | --- |
| See the app | `scripts/dlive.sh` |
| Build it without opening it | `scripts/dlive.sh --build` |
| Know it still works | `scripts/dlive.sh --tests` |
| Look at every screen as a PNG | `scripts/dlive.sh --shots` |
| Give it to someone else | `scripts/package.sh` |
| Build the plug-ins too | `scripts/build.sh` |
| Start from nothing on a new Mac | `scripts/bootstrap.sh` |

## Before the first build

Xcode command line tools, CMake >= 3.22 and Ninja. On this machine CMake and Ninja were installed
with `uv tool install cmake ninja`, which puts them in `~/.local/bin`; **every script exports that
onto `PATH` itself**, so there is nothing to set up in your shell first.

```sh
scripts/bootstrap.sh
```

Clones JUCE 8.0.8 into `external/JUCE` (shallow, pinned, git-ignored) and configures `build/`. Run it
once per clone of the repo. Everything below assumes it has been run.

## Run the app

`scripts/dlive.sh` is the fast loop. It builds **only DLIVE**, not the six plug-in products, which is
the difference between a few seconds and a few minutes.

```sh
scripts/dlive.sh                # build (Release) and open DLIVE.app
scripts/dlive.sh --build        # build only, do not open
scripts/dlive.sh --debug        # the same, from build-debug/
scripts/dlive.sh --tests        # build and run dlive_app_tests, then livemix_tests
scripts/dlive.sh --shots [dir]  # build and render every workspace to PNG (default: build/app-snapshots)
```

The bundle it opens is `build/app/DLive_artefacts/Release/DLIVE.app`. You can `open` that path yourself,
or drag the bundle to /Applications — it runs from either place, and sessions go to `~/Music/DLIVE/`
regardless.

No band and no console? **Import a multitrack...** on the first page (or File > Import Multitrack Folder...)
turns a folder of stems into tracks and clips you can mix, tune and export, exactly as if they had been
recorded live. The church multitracks on this machine are in `~/Downloads/stems recording`.

### Iterating on the engine alone

`src/` is JUCE-free, so the engine and its tests build without the app at all. This is the quickest loop
there is when the change is DSP, tune, planner or profile work:

```sh
cmake -S . -B build-engine -G Ninja -DLIVEMIX_BUILD_PLUGIN=OFF
cmake --build build-engine
build-engine/tests/livemix_tests            # optionally: livemix_tests <name-filter>
```

## Build everything

`scripts/build.sh` configures with Ninja and builds the whole repo — the app, the six plug-in products,
Dine FX, and the tests.

```sh
scripts/build.sh                     # Release, into build/
scripts/build.sh Debug               # into build-debug/
scripts/build.sh Release dlive_app_tests   # one target
```

The AUs land in `build/modules/<Product>/LiveMix<Product>_artefacts/Release/` and are copied to
`~/Library/Audio/Plug-Ins/Components/Dine <Product>.component` after each build
(`-DLIVEMIX_COPY_PLUGIN_AFTER_BUILD=OFF` if you would rather they were not).

## Check it

```sh
scripts/test.sh                                     # ctest (unit + plugin integration) then the benchmark
build/app/dlive_app_tests                           # DLIVE: controller, transport, recorder, timeline, documents
build/tests/livemix_tests                           # engine unit tests
build/app/dlive_ui_snapshots out/                   # every workspace and state as PNGs — look at them
build/app/dlive_mix_stems "<stems folder>" 30 out/  # TUNE MIX on a real multitrack; exit 0 = a re-tune changed nothing
build/app/dlive_device_check 3                      # a real CoreAudio device
scripts/validate_au.sh                              # auval over every Dine AU
```

A UI change is verified by rendering the snapshots and **looking at the PNGs**, not by reasoning about the
layout code. `scripts/dlive.sh --shots` does the build and the render in one step.

## Share a test build

`scripts/package.sh` is what another Mac gets.

```sh
scripts/package.sh              # build (Release) and zip for this Mac's architecture
scripts/package.sh --universal  # arm64 + x86_64, so an Intel Mac can run it as well
scripts/package.sh --no-build   # zip whatever is already built
scripts/package.sh --out <dir>  # where the zip goes (default: dist/)
```

It builds the `DLive` target, replaces the partial signature the linker leaves behind with a complete
ad-hoc one over the whole bundle, verifies it, and writes two files into `dist/` (git-ignored):

```
dist/DLIVE-<version>-<yyyymmdd>.zip
dist/NOTES.txt
```

**Send both.** NOTES.txt is the two-line version of everything below, written for someone who has never
seen this repo.

`--universal` uses its own `build-universal/` directory rather than flipping the architectures in `build/`,
which would make every other target stale and force a full rebuild the next time anything else is touched.
It is slow — JUCE compiles twice — so it is worth it only when you know an Intel Mac is on the other end.

The zip is made with `ditto`, not `zip`: an .app is full of symlinks, a plain zip flattens them, and a
flattened bundle is one of the ways an app arrives on the other Mac as "damaged".

### What the tester will see, and what to tell them

There is no Developer ID certificate on this machine, so the app is signed **ad-hoc**. That is a complete,
valid signature — the Info.plist is bound and the resources are sealed — it is simply not one Apple has
vouched for. The distinction matters, because it is the difference between two very different messages:

- *"unverified developer — open it anyway?"* — an ad-hoc signed app. There is a way through.
- *"damaged — move to Trash"* — an unsigned or linker-only-signed bundle. There appears not to be.

So the tester has to let it through once:

1. Unzip, drag DLIVE.app to Applications.
2. **The first time only:** right-click (or Control-click) the app and choose **Open**, then Open again in
   the dialog. Double-clicking instead says macOS cannot check it for malicious software and offers no way
   through.
3. If macOS insists the app is damaged, it is not — that is the same block, worded differently. Once in
   Terminal, then open it normally:

   ```sh
   xattr -dr com.apple.quarantine /Applications/DLIVE.app
   ```

4. It asks for microphone access the first time it opens an audio device. That is DLIVE reading the inputs
   of their interface or console; without it there is nothing to mix. System Settings > Privacy & Security >
   Microphone if it is ever refused by accident.

macOS 11 or later. Sessions are written to `~/Music/DLIVE/`.

For a build anyone can double-click with no warning at all, the app needs a **Developer ID Application**
certificate and a trip through `notarytool`. That is a real distribution step, not a testing one, and
nothing in `scripts/` does it today.

## Where the build directories come from

| Directory | Made by | Holds |
| --- | --- | --- |
| `build/` | `bootstrap.sh`, `build.sh`, `dlive.sh` | the Release build of everything |
| `build-debug/` | `build.sh Debug`, `dlive.sh --debug` | the Debug build |
| `build-engine/` | by hand, `-DLIVEMIX_BUILD_PLUGIN=OFF` | the JUCE-free engine and its tests |
| `build-universal/` | `package.sh --universal` | the arm64 + x86_64 app |
| `dist/` | `package.sh` | the zip and NOTES.txt to send |

All of them are git-ignored.
