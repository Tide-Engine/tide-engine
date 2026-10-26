"""Regenerate the bundled Newton kernels; Python is an authoring dependency only.

Run in an environment with the pinned Warp wheel and Newton source importable:
    python export.py --newton-root PATH --warp-root PATH
Game builds compile the checked-in C++ and never run this script.
"""

import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile

NEWTON_REVISION = "ea4899864046dcd5981f8ba0bc3d8c25e65dcb96"
WARP_REVISION = "b346b941a4c0cd19c510db67d6506d781464ed5b"


def strip_unused_scalar_tile_guards(source):
    """Remove only Warp's unused scalar-dispatch tile scratch guards.

    The guard constructor touches shared function-static Warp storage even
    when no tile operation is emitted. Accept the exact pinned dispatcher
    prologue, and fail closed if a future export actually needs tile support.
    """
    if re.findall(r"(?m)^#define WP_TILE_BLOCK_DIM (.+)$", source) != ["1"]:
        raise RuntimeError("tile guard removal requires the scalar block_dim=1 profile")
    dispatcher = (
        r"(?m)(^static WP_FORCEINLINE void \w+_cpu_forward_dispatch\("
        r"[^{};]*\)\n\{\n)"
    )
    guard = (
        "    wp::tile_shared_storage_t tile_mem;\n"
        "#if defined(WP_ENABLE_TILES_IN_STACK_MEMORY)\n"
        "    wp::shared_tile_storage = &tile_mem;\n"
        "#endif\n"
    )
    dispatchers = len(re.findall(dispatcher, source))
    stripped, removed = re.subn(dispatcher + re.escape(guard), r"\1", source)
    if not dispatchers or removed != dispatchers:
        raise RuntimeError("unused tile guard no longer matches every scalar CPU dispatcher")
    # Check identifiers in code, not comments or string/character literals.
    code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  " ", stripped, flags=re.DOTALL)
    tile_identifiers = sorted({
        token for token in re.findall(r"\b[A-Za-z_]\w*\b", code)
        if re.search(r"(?:^|_)tiles?(?:_|$)", token, re.IGNORECASE)
        and token != "WP_TILE_BLOCK_DIM"
    })
    if tile_identifiers:
        raise RuntimeError("scalar CPU export contains unsupported tile usage: " +
                           ", ".join(tile_identifiers))
    return stripped


def revision(folder):
    return subprocess.check_output(
        ["git", "-C", str(folder), "rev-parse", "HEAD"], text=True
    ).strip()


def export_source(module, output, filename, wp):
    wp.set_module_options(
        {
            "enable_backward": False,
            "fuse_fp": False,
            "fast_math": False,
            "cpu_compiler_flags": "",
            "block_dim": 1,
        },
        module=module,
    )
    with tempfile.TemporaryDirectory(prefix="tide-newton-") as temporary:
        wp.compile_aot_module(module, device="cpu", module_dir=temporary, strip_hash=True)
        generated = list(pathlib.Path(temporary).glob("*.cpp"))
        if len(generated) != 1:
            raise RuntimeError("expected exactly one generated module: " + filename)
        source = generated[0].read_text(encoding="utf-8")
    source = strip_unused_scalar_tile_guards(source)
    source = re.sub(
        r"(?m)^// .*?((?:newton|warp)[/\\]_src[/\\].*?:\d+)$",
        lambda match: "// " + match[1].replace("\\", "/"),
        source,
    )
    source = source.replace('#include "builtin.h"', '#include "../vendor/warp/builtin.h"')
    source = source.replace('#include "deterministic.h"', '#include "../vendor/warp/deterministic.h"')
    source = re.sub(r"(?m)^WP_API void (\w+_cpu_forward)\(", r"static void \1(", source)
    source = re.sub(r"(?m)^    ((?:wp::[^\n;=]+|bool)) (var_\d+);$", r"    \1 \2{};", source)
    preamble = (
        "// Generated from NVIDIA Newton; see ../UPSTREAM.md.\n"
        "// SPDX-FileCopyrightText: Copyright (c) 2025 The Newton Developers\n"
        "// SPDX-License-Identifier: Apache-2.0\n"
        "#pragma clang diagnostic push\n"
        '#pragma clang diagnostic ignored "-Weverything"\n'
        '#include "../math_bindings.h"\n'
        "#define WP_API\n"
    )
    (output / filename).write_text(
        preamble + source + "\n#pragma clang diagnostic pop\n", encoding="utf-8", newline="\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--newton-root", required=True, type=pathlib.Path)
    parser.add_argument("--warp-root", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if revision(args.newton_root) != NEWTON_REVISION:
        parser.error("Newton source must be at " + NEWTON_REVISION)
    if revision(args.warp_root) != WARP_REVISION:
        parser.error("Warp source must be at " + WARP_REVISION)

    import sys

    sys.path.insert(0, str(args.newton_root.resolve()))
    import warp as wp

    if wp.config.version != "1.19.0.dev20261020":
        parser.error("install warp-lang==1.19.0.dev20261020")
    wp.config.enable_cuda = False
    wp.config.cpu_compiler_flags = ""
    wp.init()
    from newton._src.solvers.featherstone import kernels
    from newton._src.solvers.semi_implicit.kernels_contact import eval_body_contact
    from warp._src.context import Kernel

    package = pathlib.Path(__file__).resolve().parents[1]
    output = package / "kernels"
    output.mkdir(exist_ok=True)
    export_source(kernels, output, "featherstone.inl", wp)
    # Rehome the original entry point unchanged, exporting only its transitive
    # force-function closure. The entire contact module also includes unrelated
    # particle/hash-grid kernels which need Warp runtime services.
    contacts = wp.get_module("tide_newton_rigid_contacts")
    Kernel(eval_body_contact.func, key="eval_body_contact", module=contacts)
    export_source(contacts, output, "contacts.inl", wp)

    native = args.warp_root / "warp" / "native"
    destination = package / "vendor" / "warp"
    pending = [pathlib.Path("builtin.h"), pathlib.Path("deterministic.h")]
    copied = set()
    while pending:
        relative = pending.pop()
        if relative in copied:
            continue
        original = native / relative
        contents = original.read_text(encoding="utf-8")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(original.read_bytes())
        copied.add(relative)
        for included in re.findall(r'^\s*#\s*include\s+"([^"]+)"', contents, re.MULTILINE):
            dependency = relative.parent / included
            if (native / dependency).is_file():
                pending.append(dependency)
            elif "cuda" not in included and "nv" not in included:
                raise RuntimeError("missing native include: " + str(dependency))

    # Tide: wasm has no native _Float16. The upstream fallback is sufficient
    # for these float32 kernels (which never call the half conversion helpers).
    builtin = destination / "builtin.h"
    contents = builtin.read_text(encoding="utf-8")
    old = "#elif defined(__clang__)\n\n// _Float16"
    new = "#elif defined(__clang__) && !defined(__wasm__) // Tide: no wasm _Float16\n\n// _Float16"
    if contents.count(old) != 1:
        raise RuntimeError("half conversion patch no longer matches")
    builtin.write_text(contents.replace(old, new), encoding="utf-8", newline="\n")

    # Tide: an explicit FMA is still fused when contraction is disabled.
    matrix = destination / "mat.h"
    contents = matrix.read_text(encoding="utf-8")
    for scalar, function in (("float", "fmaf"), ("double", "fma")):
        old = f"{scalar} muladd({scalar} a, {scalar} b, {scalar} c) {{ return {function}(a, b, c); }}"
        new = f"{scalar} muladd({scalar} a, {scalar} b, {scalar} c) {{ return c + a * b; }} // Tide: no FMA"
        if contents.count(old) != 1:
            raise RuntimeError("matrix FMA patch no longer matches")
        contents = contents.replace(old, new)
    matrix.write_text(contents, encoding="utf-8", newline="\n")

    # Tide includes the real C integer types before the standalone kernels.
    # Upstream's long-long int64_t typedef conflicts with LP64 system headers.
    crt = destination / "crt.h"
    contents = crt.read_text(encoding="utf-8")
    contents, replacements = re.subn(
        r"/// stdint\.h\n.*?(?=/// math\.h)",
        '/// stdint.h\n// Tide: preserve the system C typedefs on LP64 and wasm32.\n#include <stdint.h>\n\n',
        contents,
        flags=re.DOTALL,
    )
    if replacements != 1:
        raise RuntimeError("standalone CRT integer patch no longer matches")
    crt.write_text(contents, encoding="utf-8", newline="\n")

    notices = package / "vendor"
    shutil.copyfile(args.newton_root / "LICENSE.md", notices / "LICENSE-NEWTON.md")
    shutil.copyfile(args.warp_root / "LICENSE.md", notices / "LICENSE-WARP.md")
    for name in ("nanovdb", "svd", "reproducible_floating_sums", "moller", "windingnumber"):
        shutil.copyfile(args.warp_root / "licenses" / (name + "-LICENSE.txt"), notices / ("LICENSE-" + name + ".txt"))
    files = sorted(output.glob("*.inl")) + sorted(destination.rglob("*"))
    manifest = []
    for path in files:
        if path.is_file():
            manifest.append(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.relative_to(package).as_posix())
    (package / "UPSTREAM.sha256").write_text("\n".join(manifest) + "\n", encoding="utf-8", newline="\n")
    print(f"Exported Featherstone, rigid contacts and {len(copied)} Warp support headers")


if __name__ == "__main__":
    main()
