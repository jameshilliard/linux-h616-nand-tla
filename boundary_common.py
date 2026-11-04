# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared extraction/build support for the cross-boundary source checks."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from run_core import extract

HERE = Path(__file__).resolve().parent


def sources(candidate_source=None):
    pin = json.loads((HERE / "source.json").read_text())
    extra = json.loads((HERE / "reference/boundaries.json").read_text())
    expected = {name: info["sha256"] for name, info in pin["core_sources"].items()}
    expected["sunxi_nand.c"] = pin["driver_sha256"]
    expected.update(extra["files"])
    result = {}
    for name, digest in expected.items():
        data = (HERE / "reference" / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("reference hash mismatch: " + name)
        result[name] = data.decode()
    if candidate_source:
        result["sunxi_nand.c"] = candidate_source.read_text()
    return result


def bodies(src, functions):
    output, prototypes, hashes = [], [], {}
    for filename, names in functions.items():
        for name in names:
            body = extract(src[filename], name)
            line = src[filename][:src[filename].index(body)].count("\n") + 1
            prototypes.append(body[:body.index("\n{")] + ";")
            output.append(f'#line {line} "{filename}"\n{body}')
            hashes[filename + ":" + name] = hashlib.sha256(body.encode()).hexdigest()
    return "\n".join(output), "\n".join(prototypes), hashes


def check(name, units, hashes, *, cc="gcc", args=(), expected="ALL CHECKS PASSED",
          limits=(), flags=(), coverage=None, require_all_called=True, artifacts=()):
    results = HERE / "results"
    results.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix=name + "-", dir=results))
    for filename, text in units.items():
        (out / filename).write_text(text)
    command = [cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
               "-fno-strict-aliasing", "-fno-strict-overflow", "-funsigned-char",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-fno-omit-frame-pointer", "-no-pie", *flags,
               *[n for n in units if n.endswith(".c")], "-o", "check"]
    build = subprocess.run(command, cwd=out, text=True, capture_output=True, timeout=120)
    (out / "build.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError(str(out) + "\n" + build.stdout + build.stderr)
    run = subprocess.run(["./check", *args], cwd=out, text=True, capture_output=True, timeout=180)
    (out / "check.log").write_text(run.stdout + run.stderr)
    passed = run.returncode == 0 and expected in run.stdout and not run.stderr
    observed = {n: (out / n).read_bytes() for n in artifacts} if passed else {}
    cov = None
    if passed and coverage:
        from source_coverage import build_and_collect
        cov = build_and_collect(out, command, *coverage)
        if any((out / n).read_bytes() != value for n, value in observed.items()):
            raise RuntimeError("sanitizer/coverage observations differ: " + name)
        if require_all_called and any(not v["calls"] for v in cov["functions"].values()):
            passed = False
    (out / "summary.json").write_text(json.dumps(dict(
        passed=passed, functions=hashes, output=run.stdout, diagnostics=run.stderr,
        generated_sha256={n: hashlib.sha256(t.encode()).hexdigest() for n, t in units.items()},
        compiler_command=command,
        compiler_version=subprocess.check_output([cc, "--version"], text=True).splitlines()[0],
        limits=list(limits), coverage=cov,
        artifacts_sha256={n: hashlib.sha256(value).hexdigest() for n, value in observed.items()},
        source_sha256={n: hashlib.sha256(s.encode()).hexdigest()
                       for n, s in coverage[0].items()} if coverage else {},
        driver_sha256=hashlib.sha256(coverage[0]["sunxi_nand.c"].encode()).hexdigest()
                      if coverage and "sunxi_nand.c" in coverage[0] else None), indent=2) + "\n")
    print(run.stdout + run.stderr, end="")
    print("Logs:", out, flush=True)
    if not passed:
        raise RuntimeError("boundary validation failed: " + name)
    return out
