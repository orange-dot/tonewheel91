"""Bounded MA3-PERF P0-P2 recipe. --run requires an exclusive host window.
prepare requires the P0 run directory and source-before snapshot already frozen.
"""
import argparse
import csv
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATCHES = ("Tepih", "Lead", "Raster", "Prizma", "Granica")
FLAGS = "-std=c23 -O2 -Wall -Wextra -Wpedantic -ffp-contract=off"


def rows(path):
    return list(csv.DictReader(io.StringIO("\n".join(
        line for line in path.read_text().splitlines() if not line.startswith("#")))))


def invoke(command, log, env=None, cwd=ROOT):
    with log.open("x") as output:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=output,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{command} failed ({result.returncode}); see {log}")


def prepare(run):
    for variant, cc, flags, link in (
        ("release", "/usr/bin/gcc", FLAGS, ""),
        ("clang", "/usr/bin/clang", FLAGS, ""),
        ("sanitize", "/usr/bin/clang", FLAGS.replace("-O2", "-O1 -g")
         + " -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer",
         "-fsanitize=address,undefined,float-cast-overflow"),
        ("profile", "/usr/bin/gcc", FLAGS + " -g -pg", "-pg"),
    ):
        build = run / variant
        command = ["make", "-B", f"CC={cc}", f"BUILD={build}",
                   f"CFLAGS={flags}", f"LDFLAGS={link}", str(build / "bench_ma3_perf")]
        (run / (variant + "-command.json")).write_text(json.dumps(command) + "\n")
        invoke(command, run / (variant + "-build.log"))
        if variant != "profile":
            env = os.environ.copy()
            env["ASAN_OPTIONS"] = "detect_leaks=0"
            invoke(["python3", "test/ma3_perf_cli.py", str(build / "bench_ma3_perf")],
                   run / (variant + "-cli.log"), env)
    objects = [run / "release" / (name + ".o") for name in
               ("ma_voice", "ma_bank", "ma_output", "ma_pool", "ma_effects", "drive")]
    original = run / "original-harness"
    command = ["/usr/bin/gcc", *FLAGS.split(), "-I", str(ROOT / "driver"),
               str(run / "source-before/driver/bench_ma3_perf.c"),
               *map(str, objects), "-o", str(original), "-lm"]
    invoke(command, run / "original-harness-build.log")
    env = {k: v for k, v in os.environ.items() if not k.startswith("MA3_PERF_")}
    env.update(MA3_PERF_LIMIT="32", MA3_PERF_FRAMES="128", MA3_PERF_ARM="3",
               MA3_PERF_WARMUP="2", MA3_PERF_BLOCKS="4")
    paths = []
    for label, binary in (("original", original), ("updated", run / "release/bench_ma3_perf")):
        path = run / (label + "-functional-pcm.csv")
        invoke([str(binary), "--run"], path, env)
        paths.append(path)
    left, right = map(rows, paths)
    fields = ("patch", "bcs", "limit", "frames", "scenario", "arm", "pass",
              "unison", "pcm_hash", "effect_pre_fingerprint", "effect_post_fingerprint")
    assert len(left) == len(right) == 160
    assert [[r[k] for k in fields] for r in left] == [[r[k] for k in fields] for r in right]
    (run / "harness-pcm-comparison.json").write_text(json.dumps({
        "rows": 160, "matching_fields": fields, "warmup": 2, "blocks": 4,
        "status": "all identical", "purpose": "functional equivalence; times are not evidence"}, indent=2) + "\n")
    snapshot = run / "source-measured"
    files = [Path("Makefile"), *Path(ROOT / "src").glob("*.[ch]"),
             Path("driver/bench_ma3_perf.c"), Path("test/ma3_perf_cli.py"),
             Path("test/ma3_perf_measure.py")]
    hashes = {}
    for f in files:
        src = ROOT / f
        relative = src.relative_to(ROOT)
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
        hashes[str(relative)] = hashlib.sha256(src.read_bytes()).hexdigest()
    for variant in ("release", "clang", "sanitize", "profile"):
        for p in (run / variant).iterdir():
            if p.is_file():
                hashes[str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()
    (run / "measured-source-binary-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")


def context(run, label):
    data = {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "processes": subprocess.check_output(["ps", "-eo", "pid,ppid,psr,pcpu,comm"], text=True),
            "affinity_operator": sorted(os.sched_getaffinity(0))}
    values = {}
    for pattern in ("devices/system/cpu/cpu*/cpufreq/scaling_cur_freq", "class/hwmon/hwmon*/temp*_input"):
        for p in Path("/sys").glob(pattern):
            try:
                values[str(p)] = p.read_text().strip()
            except OSError:
                pass
    data["sysfs"] = values
    (run / (label + "-context.json")).write_text(json.dumps(data, indent=2) + "\n")


def measure(run, cpu):
    if cpu not in os.sched_getaffinity(0):
        raise ValueError("CPU outside allowed affinity")
    env = {k: v for k, v in os.environ.items() if not k.startswith("MA3_PERF_")}
    env.update(MA3_PERF_LIMIT="32", MA3_PERF_FRAMES="128", MA3_PERF_ARM="3",
               MA3_PERF_WARMUP="64", MA3_PERF_BLOCKS="256", MA3_PERF_RUN_ID=run.name)
    binary = run / "release/bench_ma3_perf"
    context(run, "measurement-start")
    invoke(["taskset", "-c", str(cpu), str(binary), "--overhead"], run / "empty-bracket.csv", env)
    recipe = []
    for epoch in (1, 2):
        # Reverse full/sparse order in the second independent process epoch.
        for scenario in ((3, 1) if epoch == 1 else (1, 3)):
            label = f"epoch{epoch}-scenario{scenario}"
            selected = dict(env, MA3_PERF_SCENARIO=str(scenario), MA3_PERF_RAW=str(run / (label + "-raw.csv")))
            command = ["taskset", "-c", str(cpu), str(binary), "--run"]
            recipe.append({"label": label, "command": command, "environment": {k: v for k, v in selected.items() if k.startswith("MA3_PERF_")}})
            (run / "executed-recipe.json").write_text(json.dumps({"status": "in progress", "cpu": cpu, "commands_attempted": recipe}, indent=2) + "\n")
            invoke(command, run / (label + "-summary.csv"), selected)
            assert len(rows(run / (label + "-summary.csv"))) == 20
            assert len(rows(run / (label + "-raw.csv"))) == 5120
            print(f"completed {label}", flush=True)
    full = [r for p in run.glob("epoch*-scenario3-summary.csv") for r in rows(p)]
    cells = [(p, bcs) for p in PATCHES for bcs in (0, 1)]
    worst_cell = max(cells, key=lambda cell: statistics.mean(float(r["cpu_mean_us"]) for r in full if r["patch"] == cell[0] and int(r["bcs"]) == cell[1]))
    worst = worst_cell[0]
    profile_patches = list(dict.fromkeys(("Tepih", "Raster", worst)))
    for patch in profile_patches:
        for bcs in (0, 1):
            profile(run, cpu, env, patch, bcs, 3, recipe)
    for bcs in (0, 1):
        profile(run, cpu, env, "Tepih", bcs, 1, recipe)
    context(run, "measurement-end")
    (run / "executed-recipe.json").write_text(json.dumps({"cpu": cpu, "worst_patch_bcs_by_mean_across_epochs_and_passes": worst_cell, "status": "complete",
        "recipe": recipe, "limits": "64 warmup/256 measured onset-to-held windows; wet tails and thermal convergence not established; two passes per process are determinism repeats, two epochs are independent processes; gprof profiles whole process including init/warmup/hash/I/O; profile times are not release evidence"}, indent=2) + "\n")


def profile(run, cpu, env, patch, bcs, scenario, recipe):
    label = f"profile-{patch}-bcs{bcs}-scenario{scenario}"
    directory = run / label
    directory.mkdir()
    binary = run / "profile/bench_ma3_perf"
    selected = dict(env, MA3_PERF_PATCH=patch, MA3_PERF_BCS=str(bcs), MA3_PERF_SCENARIO=str(scenario))
    command = ["taskset", "-c", str(cpu), str(binary), "--run"]
    recipe.append({"label": label, "command": command, "environment": {k: v for k, v in selected.items() if k.startswith("MA3_PERF_")}})
    (run / "executed-recipe.json").write_text(json.dumps({"status": "in progress", "cpu": cpu, "commands_attempted": recipe}, indent=2) + "\n")
    invoke(command, directory / "instrumented-summary.csv", selected, directory)
    invoke(["gprof", "-b", str(binary), str(directory / "gmon.out")], directory / "gprof.txt")
    print(f"completed {label}", flush=True)


def summarize(run):
    all_rows = []
    for p in sorted(run.glob("epoch*-summary.csv")):
        for r in rows(p):
            all_rows.append(dict(epoch=p.name.split("-")[0], **r))
    output = run / "release-case-table.csv"
    with output.open("w") as file:
        writer = csv.DictWriter(file, fieldnames=list(all_rows[0]))
        writer.writeheader()
        writer.writerows(all_rows)
    lines = ["P2 bounded screen; not final readiness or steady-state evidence.", "", "| Patch | BCS | Scenario | CPU mean range us | CPU p99 range us | Wall p99 range us | Wall misses / blocks |", "|---|---:|---:|---:|---:|---:|---:|"]
    for scenario in (3, 1):
        for patch in PATCHES:
            for bcs in (0, 1):
                selected = [r for r in all_rows if r["patch"] == patch and int(r["bcs"]) == bcs and int(r["scenario"]) == scenario]
                ranges = []
                for key in ("cpu_mean_us", "cpu_p99_us", "wall_p99_us"):
                    values = [float(r[key]) for r in selected]
                    ranges.append(f"{min(values):.1f}–{max(values):.1f}")
                misses = sum(int(r["wall_misses"]) for r in selected)
                blocks = sum(int(r["blocks"]) for r in selected)
                hashes = {r["pcm_hash"] for r in selected}
                assert len(hashes) == 1
                lines.append(f"| {patch} | {bcs} | {scenario} | " + " | ".join(ranges) + f" | {misses}/{blocks} |")
    (run / "release-summary.md").write_text("\n".join(lines) + "\n")
    overhead = rows(run / "empty-bracket.csv")
    (run / "empty-bracket-summary.json").write_text(json.dumps({key: {
        "min": min(float(r[key]) for r in overhead), "mean": statistics.mean(float(r[key]) for r in overhead),
        "max": max(float(r[key]) for r in overhead)} for key in ("cpu_us", "wall_us")}, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("action", choices=("prepare", "run", "summarize"))
    parser.add_argument("--directory", default="build/ma3-perf-20261002-01")
    parser.add_argument("--cpu", type=int, default=0)
    args = parser.parse_args()
    directory = (ROOT / args.directory).resolve()
    if args.action == "prepare":
        prepare(directory)
    elif args.action == "run":
        measure(directory, args.cpu)
    else:
        summarize(directory)
