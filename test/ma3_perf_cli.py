"""Functional benchmark contract checks; short renders are not performance evidence."""
import csv
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BENCH = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]


def rows(text):
    parsed = list(csv.DictReader(io.StringIO("\n".join(
        line for line in text.splitlines() if not line.startswith("#")))))
    for row in parsed:  # every row has exactly the header's field count
        assert None not in row and None not in row.values(), row
    return parsed


class PerfCLI(unittest.TestCase):
    def invoke(self, options, mode="--list", code=0):
        env = {k: v for k, v in os.environ.items() if not k.startswith("MA3_PERF_")}
        env.update({"MA3_PERF_" + k: str(v) for k, v in options.items()})
        result = subprocess.run([str(BENCH), mode], env=env, text=True,
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def test_matrix_count(self):
        self.assertIn("cases=3840 rows=7680", self.invoke({}).stdout)
        self.assertIn("cases=960 rows=1920", self.invoke({"LIMIT": 32}).stdout)
        self.assertIn("cases=960 rows=1920", self.invoke({"LIMIT": 8}).stdout)
        self.assertIn("cases=10 rows=20", self.invoke(
            {"LIMIT": 32, "FRAMES": 128, "SCENARIO": 3, "ARM": 3}).stdout)

    def test_strict_filters_and_counts(self):
        invalid = {"PATCH": ["", "Unknown", "tepih"], "BCS": ["", "x", "-1", "2", "1x"],
                   "LIMIT": ["0", "4", "32x", " 32", "+32"], "FRAMES": ["129", "0"],
                   "SCENARIO": ["8", "3.0"], "ARM": ["4"],
                   "BLOCKS": ["0", "-1", "32x", "4097", "999999999999999999999"],
                   "WARMUP": ["", "-1", "8x", "4097"], "PMU": ["", "on", "AUTO"],
                   "ENGINE": ["", "vector", "SCALAR"]}
        with tempfile.TemporaryDirectory() as temp:
            raw = Path(temp) / "raw.csv"
            raw.write_text("preserve")
            for key, values in invalid.items():
                for value in values:
                    with self.subTest(key=key, value=value):
                        result = self.invoke({key: value, "RAW": raw}, "--run", 2)
                        self.assertIn("MA3_PERF_", result.stderr)
                        self.assertEqual(raw.read_text(), "preserve")

    def test_explicit_all_and_zero_warmup(self):
        result = self.invoke({"PATCH": "all", "BCS": "all", "WARMUP": 0})
        self.assertIn("cases=3840 rows=7680", result.stdout)

    def test_raw_samples_and_summary(self):
        with tempfile.TemporaryDirectory() as temp:
            raw = Path(temp) / "raw.csv"
            result = self.invoke({"PATCH": "Tepih", "BCS": 0, "LIMIT": 32,
                "FRAMES": 128, "SCENARIO": 4, "ARM": 3, "WARMUP": 1,
                "BLOCKS": 4, "RAW": raw, "RUN_ID": "cli-check"}, "--run")
            summary = rows(result.stdout)
            samples = rows(raw.read_text())
            self.assertEqual(len(summary), 2)
            self.assertEqual(len(samples), 8)
            self.assertEqual(summary[0]["pcm_hash"], summary[1]["pcm_hash"])
            self.assertIn("run_id=cli-check", result.stdout)
            for index, row in enumerate(samples):
                self.assertEqual(int(row["block"]), index % 4)
                self.assertEqual(row["phase"], "sustain" if index % 4 < 2 else "release")
                self.assertGreaterEqual(float(row["wall_us"]), float(row["cpu_us"]))
                self.assertEqual(row["active_before"], "32")
                self.assertEqual(row["sustained_before"], "32" if index % 4 < 2 else "0")
                self.assertEqual(row["released_before"], "0" if index % 4 < 2 else "32")
            for row in summary:
                case = [s for s in samples if s["pass"] == row["pass"]]
                self.assertEqual(row["blocks"], "4")
                self.assertEqual(row["warmup"], "1")
                self.assertEqual(int(row["wall_misses"]), sum(int(s["wall_miss"]) for s in case))
                self.assertEqual(int(row["cpu_misses"]), sum(int(s["cpu_miss"]) for s in case))
                self.assertEqual(row["active_min"], "32")
                self.assertEqual(row["sustained_max"], "32")
                self.assertEqual(row["released_max"], "32")
                ordered = sorted(float(s["cpu_us"]) for s in case)
                self.assertEqual(float(row["cpu_p50_us"]), ordered[1])
                self.assertEqual(float(row["cpu_p99_us"]), ordered[3])
            self.invoke({"PATCH": "Tepih", "RAW": raw}, "--run", 1)
            self.assertEqual(len(rows(raw.read_text())), 8)

    def test_empty_occupancy(self):
        result = self.invoke({"PATCH": "Raster", "BCS": 1, "LIMIT": 32,
            "FRAMES": 128, "SCENARIO": 0, "ARM": 3, "WARMUP": 0, "BLOCKS": 1}, "--run")
        for row in rows(result.stdout):
            for field in ("active_min", "active_max", "held_max", "released_max", "sustained_max"):
                self.assertEqual(row[field], "0")

    def test_unison_and_sparse_slot_counts(self):
        for scenario, expected, unison in ((1, 1, 1), (7, 32, 8)):
            result = self.invoke({"PATCH": "Raster", "BCS": 0, "LIMIT": 32,
                "FRAMES": 128, "SCENARIO": scenario, "ARM": 3, "WARMUP": 0,
                "BLOCKS": 1}, "--run")
            for row in rows(result.stdout):
                self.assertEqual(int(row["held_max"]), expected)
                self.assertEqual(int(row["active_min"]), expected)
                self.assertEqual(int(row["unison"]), unison)

    def test_eight_slot_limit(self):
        result = self.invoke({"PATCH": "Lead", "BCS": 1, "LIMIT": 8, "FRAMES": 128,
            "SCENARIO": 3, "ARM": 3, "WARMUP": 0, "BLOCKS": 1}, "--run")
        for row in rows(result.stdout):
            self.assertEqual(row["limit"], "8")
            self.assertEqual(row["active_min"], "8")
            self.assertEqual(row["held_max"], "8")

    def test_pmu_off_leaves_counter_fields_empty(self):
        with tempfile.TemporaryDirectory() as temp:
            raw = Path(temp) / "raw.csv"
            result = self.invoke({"PATCH": "Tepih", "BCS": 0, "LIMIT": 8, "FRAMES": 128,
                "SCENARIO": 3, "ARM": 3, "WARMUP": 1, "BLOCKS": 2, "RAW": raw,
                "PMU": "off"}, "--run")
            self.assertIn("# pmu=off;", result.stdout)
            for row in rows(result.stdout):
                for field in ("cycles_mean", "cycles_p50", "cycles_p99", "cycles_max",
                              "instructions_mean", "instructions_min", "instructions_max",
                              "pmu_multiplexed_blocks"):
                    self.assertEqual(row[field], "")
            for row in rows(raw.read_text()):
                self.assertEqual((row["cycles"], row["instructions"], row["pmu_multiplexed"]),
                                 ("", "", ""))
        for row in rows(self.invoke({"BLOCKS": 2, "PMU": "off"}, "--overhead").stdout):
            self.assertEqual((row["cycles"], row["instructions"]), ("", ""))

    def test_pmu_counters_match_raw_samples(self):
        probe = self.invoke({"BLOCKS": 2}, "--overhead")
        if "# pmu=unavailable" in probe.stdout:
            # The host refuses counters: required mode must fail loudly, auto must not.
            self.invoke({"BLOCKS": 1, "PMU": "required"}, "--overhead", 2)
            self.skipTest("hardware counters unavailable on this host")
        for row in rows(probe.stdout):
            self.assertGreater(int(row["instructions"]), 0)
        with tempfile.TemporaryDirectory() as temp:
            raw = Path(temp) / "raw.csv"
            result = self.invoke({"PATCH": "Tepih", "BCS": 0, "LIMIT": 8, "FRAMES": 128,
                "SCENARIO": 3, "ARM": 3, "WARMUP": 1, "BLOCKS": 4, "RAW": raw,
                "PMU": "required"}, "--run")
            self.assertIn("# pmu=cycles+instructions scope=user_thread;", result.stdout)
            summary, samples = rows(result.stdout), rows(raw.read_text())
            self.assertEqual(summary[0]["pcm_hash"], summary[1]["pcm_hash"])
            for row in summary:
                case = [s for s in samples if s["pass"] == row["pass"]]
                cycles = sorted(int(s["cycles"]) for s in case)
                instructions = sorted(int(s["instructions"]) for s in case)
                self.assertGreater(cycles[0], 0)
                self.assertEqual(int(row["cycles_p50"]), cycles[1])
                self.assertEqual(int(row["cycles_p99"]), cycles[3])
                self.assertEqual(int(row["cycles_max"]), cycles[3])
                self.assertAlmostEqual(float(row["cycles_mean"]), sum(cycles) / 4, delta=0.1)
                self.assertEqual(int(row["instructions_min"]), instructions[0])
                self.assertEqual(int(row["instructions_max"]), instructions[3])
                self.assertEqual(int(row["pmu_multiplexed_blocks"]),
                                 sum(int(s["pmu_multiplexed"]) for s in case))
            # Identical work retires near-identical user instructions in both passes;
            # interrupts can add a few counts, so allow 0.01 percent.
            for first, second in zip([s for s in samples if s["pass"] == "1"],
                                     [s for s in samples if s["pass"] == "2"]):
                a, b = int(first["instructions"]), int(second["instructions"])
                self.assertLessEqual(abs(a - b), max(a, b) // 10000)

    def test_engines_render_identical_pcm(self):
        hashes = {}
        for engine in ("auto", "scalar"):
            result = self.invoke({"PATCH": "Granica", "BCS": 1, "LIMIT": 8, "FRAMES": 128,
                "SCENARIO": 4, "ARM": 3, "WARMUP": 2, "BLOCKS": 4, "ENGINE": engine}, "--run")
            self.assertIn("# engine=%s;" % engine, result.stdout)
            hashes[engine] = [(r["pcm_hash"], r["effect_post_fingerprint"], r["sleeps"],
                               r["ladder_resets"]) for r in rows(result.stdout)]
        self.assertEqual(hashes["auto"], hashes["scalar"])

    def test_empty_bracket_shape(self):
        result = self.invoke({"BLOCKS": 3}, "--overhead")
        values = rows(result.stdout)
        self.assertEqual(len(values), 3)
        for row in values:
            self.assertGreaterEqual(float(row["wall_us"]), float(row["cpu_us"]))


if __name__ == "__main__":
    unittest.main()
