#!/usr/bin/env python
import os

# Build the GDExtension by reusing godot-cpp's build environment.
# Requires the godot-cpp submodule checked out on a branch matching the engine
# (4.4): `git submodule update --init --recursive`.
env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

libbase = "demo/addons/procedural_city/bin/libprocedural_city"

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "{}.{}.{}.framework/{}.{}.{}".format(
            libbase,
            env["platform"],
            env["target"],
            os.path.basename(libbase),
            env["platform"],
            env["target"],
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "{}{}{}".format(libbase, env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
