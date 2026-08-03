#!/usr/bin/env python
import os

# godot-cpp defaults use_static_cpp=True on Windows (/MT). vcpkg's grpc only
# ships /MD builds (x64-windows[-static-md] both use the dynamic CRT), so
# linking them into a /MT extension fails at link time with LNK2038
# RuntimeLibrary mismatches on every grpc/abseil object. Default to /MD here
# instead - still overridable with use_static_cpp=yes on the command line.
ARGUMENTS.setdefault("use_static_cpp", "no")

# Build the GDExtension by reusing godot-cpp's build environment.
# Requires the godot-cpp submodule checked out on a branch matching the engine
# (4.4): `git submodule update --init --recursive`.
env = SConscript("godot-cpp/SConstruct")

# All includes are rooted at src/ (e.g. #include "meshing/packed_surface.h").
env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp") + Glob("src/*/*.cpp")

# gRPC client (talks to godisplacementx/gpudisplacementx `serve-grpc`), built
# against vcpkg's x64-windows triplet (grpc's port only supports static
# linkage regardless of triplet, so this pulls in the full static closure:
# grpc/grpc++/gpr + abseil/c-ares/re2/upb/utf8-range objects it bundles, plus
# protobuf/openssl/zlib). VCPKG_ROOT defaults to a `vcpkg` checkout next to
# this repo; override with the env var if yours lives elsewhere.
# x64-windows-static-md: static library linkage (grpc's only supported mode)
# with the *dynamic* CRT (/MD), matching godot-cpp's own runtime setting -
# plain x64-windows links grpc.lib as /MT and fails at link time with
# LNK2038 RuntimeLibrary mismatches against every godot-cpp/GDExtension object.
vcpkg_root = os.environ.get("VCPKG_ROOT", os.path.join(Dir("#").abspath, "..", "vcpkg"))
vcpkg_triplet = "x64-windows-static-md"
vcpkg_installed = os.path.join(vcpkg_root, "installed", vcpkg_triplet)
proto_gen_dir = "src/rpc_gen"
proto_src = "proto/displacement.proto"

def _first_existing(*candidates):
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    return candidates[0]


if env["platform"] == "windows" and os.path.isdir(vcpkg_installed):
    # protoc/grpc_cpp_plugin are host build tools; some triplets don't
    # re-install their own copy, so fall back to the default x64-windows one.
    protoc = _first_existing(
        os.path.join(vcpkg_installed, "tools", "protobuf", "protoc.exe"),
        os.path.join(vcpkg_root, "installed", "x64-windows", "tools", "protobuf", "protoc.exe"),
    )
    grpc_cpp_plugin = _first_existing(
        os.path.join(vcpkg_installed, "tools", "grpc", "grpc_cpp_plugin.exe"),
        os.path.join(vcpkg_root, "installed", "x64-windows", "tools", "grpc", "grpc_cpp_plugin.exe"),
    )

    proto_gen = env.Command(
        [
            os.path.join(proto_gen_dir, "displacement.pb.cc"),
            os.path.join(proto_gen_dir, "displacement.pb.h"),
            os.path.join(proto_gen_dir, "displacement.grpc.pb.cc"),
            os.path.join(proto_gen_dir, "displacement.grpc.pb.h"),
        ],
        proto_src,
        action=[
            Mkdir(proto_gen_dir) if not os.path.isdir(proto_gen_dir) else "",
            '"{protoc}" -I proto --cpp_out={out} {src}'.format(protoc=protoc, out=proto_gen_dir, src=proto_src),
            '"{protoc}" -I proto --grpc_out={out} --plugin=protoc-gen-grpc="{plugin}" {src}'.format(
                protoc=protoc, plugin=grpc_cpp_plugin, out=proto_gen_dir, src=proto_src
            ),
        ],
    )
    NoCache(proto_gen)

    sources += [
        os.path.join(proto_gen_dir, "displacement.pb.cc"),
        os.path.join(proto_gen_dir, "displacement.grpc.pb.cc"),
    ]

    env.Append(CPPPATH=[proto_gen_dir, os.path.join(vcpkg_installed, "include")])
    lib_dir = os.path.join(vcpkg_installed, "lib")
    env.Append(LIBPATH=[lib_dir])

    # grpc++ alone pulls in ~20 abseil archives plus c-ares/re2/upb/utf8-range
    # (the "18 additional targets not displayed" grpc's own vcpkg usage banner
    # warns about) - rather than hand-maintain that list, link every static
    # lib vcpkg built for this triplet except the alternate variants we never
    # call (unsecure/alts/reflection/error_details, the protoc plugin support
    # libs, and protobuf-lite/protoc which duplicate symbols already in
    # libprotobuf/grpc_plugin_support).
    exclude_libs = {
        "grpc++_unsecure", "grpc_unsecure", "grpc++_alts", "grpc++_error_details",
        "grpc_plugin_support", "grpc_authorization_provider",
        "libprotobuf-lite", "libprotoc",
    }
    grpc_libs = []
    if os.path.isdir(lib_dir):
        for filename in sorted(os.listdir(lib_dir)):
            if filename.endswith(".lib") and filename[:-4] not in exclude_libs:
                grpc_libs.append(filename[:-4])
    env.Append(LIBS=grpc_libs)
    env.Append(LIBS=["ws2_32", "crypt32", "user32"])
    env.Append(CPPDEFINES=["_WIN32_WINNT=0x0A00", "PROC_CITY_HAVE_GRPC"])

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
