# Releasing Procedural City on the Godot Asset Store

Checked 17 September 2026. The [new Asset Store](https://store.godotengine.org/)
is accepting free assets; paid sales are still planned. The old Asset Library
remains available for older editors. See the [Store FAQ](https://store.godotengine.org/help/)
and [roadmap](https://store.godotengine.org/roadmap/).

## What this repository still needs

The source checkout is not an installable release archive yet:

| Finding | Before publishing |
| --- | --- |
| `.gitignore` excludes native binaries. | Assemble a ZIP containing the compiled libraries, not GitHub's automatic source ZIP. |
| The descriptor names Windows x86-64, Linux x86-64 and macOS debug/release libraries. Only the two Linux binaries are present locally. | Supply every listed library, or remove unsupported platform entries from the packaged descriptor and state the supported platforms. The Store explicitly requires every referenced shared library to be present. |
| CI builds Linux debug/release and macOS release, with no Windows or macOS debug job. | Complete and test the intended platform matrix before claiming those platforms. For macOS, verify the architectures in the framework match the release claim. |
| `godot-cpp` and `cppdisplacementx` are build-time submodules. CI describes the latter as private. | End users should need neither SCons nor a submodule checkout to run the asset. Provide the complete corresponding source and build instructions alongside binaries; a private dependency or an empty submodule in an automatic source ZIP is insufficient. |
| `LICENSE` contains GPL-3.0, but only the license document's FSF copyright notice and example placeholders. | Add the actual project's copyright holder and year(s), and copy the license into the downloadable addon. Preserve dependency notices. Do not change the listing to MIT while the code remains GPL. |
| The extension declares Godot 4.4 minimum; the demo declares 4.6. | Choose the minimum version you have actually tested. This environment pass was validated on Godot 4.7.2, Linux x86-64, Vulkan Forward+. That does not verify 4.4, other platforms, or Compatibility rendering. |

The binary, folder, license and thumbnail requirements above come from the
[current Store guidelines](https://store.godotengine.org/guidelines/).
For the source distribution requirements, read section 6 of the repository's
[GPL-3.0 license](../LICENSE); keep the source tied to the exact release build.

## Recommended deliverables

Publish the reusable extension as an **Addon**, with a separate demo download
on the matching GitHub release. If publishing only the runnable showcase,
choose **Full Project** instead and put `project.godot` at the ZIP root.

```text
procedural-city-<version>-addon.zip
└── addons/procedural_city/
    ├── procedural_city.gdextension
    ├── procedural_city.gdextension.uid
    ├── bin/                       # all libraries named in the descriptor
    ├── sprites/
    ├── README.md                  # installation, platforms, Godot version, use
    ├── LICENSE
    └── THIRD_PARTY_NOTICES.md      # applicable dependency notices

procedural-city-<version>-demo.zip
├── project.godot                 # contents of demo/, not an extra demo/ wrapper
├── addons/procedural_city/       # same complete addon as above
├── scenes/
├── environment/                 # shaders, both baked noise volumes, notices
├── README.md
└── LICENSE
```

The demo environment belongs to the demo; it is not a runtime dependency of
`ProcCityGenerator`. Its noise textures are already baked and require no access
to The Beehive. Preserve `environment/CLOUD_LICENSE.md` when distributing it.

Build both editor and export variants for each advertised platform. For example:

```bash
scons platform=linux target=template_debug arch=x86_64
scons platform=linux target=template_release arch=x86_64
```

Build Linux artifacts on an appropriate distribution baseline and inspect
shared-library dependencies. A library working on the developer's Arch system
does not establish compatibility with older distributions. For other platforms,
test on their actual target systems, including an exported project using the
release library. Optional gRPC builds can add further dependency notices and
runtime requirements; keep the default native generation path self-contained.

Exclude `.godot/`, `.git/`, `.worktrees/`, compiler output other than the intended
libraries, local overrides, logs, development tests and duplicate captures from
the user ZIP. The complete source archive is a separate deliverable.

## Upload steps

1. Create an account, select **Upload Asset**, and create your publisher and
   asset name/slug.
2. In **Settings**, enter the description, tags, Addon/Full Project type,
   matching license and source link. Disclose AI assistance accurately,
   including the demo code and scene work from this change.
3. In **Media**, add a **16:9** thumbnail and screenshots. The refreshed
   `docs/media/sample-city-*.png` are 1280×720 demo captures.
4. In **Versions**, upload the tested ZIP, version name, changelog and tested
   engine range. The limit is 1 GB per version.
5. Use **Overview → Submit** to send the listing for review.

Sources: [submission walkthrough](https://docs.godotengine.org/en/latest/community/asset_store/submitting_to_asset_store.html),
[Store guidelines](https://store.godotengine.org/guidelines/) and
[AI disclosure FAQ](https://store.godotengine.org/help/).

## Suggested listing copy

**Name:** Procedural City — Native 3D Generator

**Summary:** Generate textured 3D block cities from deterministic procedural
displacement fields, in the editor or at runtime.

**Description:** Procedural City is a C++ GDExtension for Godot. Add a
ProcCityGenerator node to generate a displacement field, block geometry and
matching albedo and normal maps. Adjust seeds, palettes, grid density, height
and geometry settings in the inspector or from GDScript. The native CPU and GPU
backends run inside the extension. Optional collision and height queries make
the generated geometry usable in a game. A separate Forward+ demo presents
four seeded cities, each with its own palette and sky, rising out of
reflective water under animated clouds. See the included README for installation and the tested platform and
Godot version matrix.

Suggested existing tags: **3D**, **Procedural Generation**, **GDExtension**,
**Tools**. Select tags that exist in the Store rather than adding duplicate
spellings. Fill in actual supported platforms before submitting; do not copy
the source README's broad version claim without validating it.

## Final release check

Extract the exact candidate ZIP into an empty directory. Open it with each
advertised Godot version on each advertised platform, with no sibling source
repositories or developer CLI binaries available. Verify generation, the demo,
and an exported project. Inspect engine logs as well as exit codes. Then tag the
matching source revision, attach the archives and submit the Store version.

No Store submission, upload, tag or GitHub release has been made by this change.
