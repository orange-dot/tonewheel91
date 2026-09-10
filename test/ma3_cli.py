"""Exercise the actual renderer executable against independent direct-pool PCM."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


RENDER = Path(sys.argv[1]).resolve()
FIXTURE = Path(sys.argv[2]).resolve()
sys.argv = sys.argv[:1]


def pcm(path):
    data = path.read_bytes()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    pos = 12
    while pos + 8 <= len(data):
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if data[pos:pos + 4] == b"data":
            return data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    raise AssertionError("No WAV data chunk")


def midi(track):
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, 1000)
            + b"MTrk" + struct.pack(">I", len(track)) + track)


class RenderCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="ma3-1-cli-")
        cls.root = Path(cls.temp.name)
        result = subprocess.run([str(FIXTURE), "--fixture", str(cls.root / "twin")],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_render(self, options, source="twin.mid", code=0):
        out = self.root / "output.wav"
        result = subprocess.run([str(RENDER), *options, "-o", str(out),
                                 str(self.root / source)], capture_output=True, text=True)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return out, result.stdout

    def test_smf_direct_twin_and_blocks(self):
        expected = (self.root / "twin.f32").read_bytes()
        for block in (1, 17, 128, 256, 4096):
            with self.subTest(block=block):
                path, log = self.run_render(["-I", "mamutanalog", "--voice-limit", "8",
                    "--unison", "2", "--block-frames", str(block), "-t", ".02"])
                self.assertEqual(pcm(path), expected)
                self.assertIn("two runs identical", log)
                self.assertIn("3 note-ons, 3 note-offs", log)
                self.assertIn("allocation failures 0", log)

    def test_factory_patches_and_unison(self):
        for patch in ("Tepih", "Lead", "Dubina", "Raster", "Prizma", "Granica"):
            with self.subTest(patch=patch):
                path, log = self.run_render(["-I", "mamutanalog", "--patch", patch,
                    "--voice-limit", "8", "--unison", "8", "-t", ".002"])
                values = struct.unpack("<" + "f" * (len(pcm(path)) // 4), pcm(path))
                self.assertTrue(all(-1 <= value <= 1 for value in values))
                self.assertIn("two runs identical", log)
                self.assertIn("peak 8", log)

    def test_effects_cli_and_cc_twins(self):
        # Half-second note and one-second tail exercise every delay tap.
        note = bytes([0, 0x90, 60, 100, 0x87, 0x68, 0x80, 60, 0, 0, 0xff, 0x2f, 0])
        (self.root / "fx-dry.mid").write_bytes(midi(note))
        cc = bytes([0, 0xb0, 107, 50, 0, 0xb0, 108, 60])
        (self.root / "fx-wet.mid").write_bytes(midi(cc + note))
        base = ["-I", "mamutanalog", "--voice-limit", "8", "-t", "1"]
        path, _ = self.run_render(base, "fx-dry.mid")
        dry = pcm(path)
        path, _ = self.run_render(base + ["--chorus-mix", "0", "--reverb-mix", "0"], "fx-dry.mid")
        self.assertEqual(pcm(path), dry)
        path, _ = self.run_render(base, "fx-wet.mid")
        wet = pcm(path)
        self.assertNotEqual(dry, wet)
        for block in (1, 17, 512, 4096):
            path, log = self.run_render(base + ["--block-frames", str(block),
                "--chorus-mix", str(50/127), "--reverb-mix", str(60/127)], "fx-dry.mid")
            self.assertEqual(pcm(path), wet)
            self.assertIn("two runs identical", log)
        path, _ = self.run_render(base + ["--chorus-mix", "1", "--chorus-depth", "1",
            "--chorus-rate", "20", "--reverb-mix", "1", "--reverb-size", "1",
            "--reverb-damping", ".99"], "fx-dry.mid")
        values = struct.unpack("<" + "f" * (len(pcm(path)) // 4), pcm(path))
        self.assertTrue(all(-1 <= v <= 1 for v in values))

    def test_channels_rates_and_seed(self):
        for rate in (44100, 48000, 96000, 192000):
            path, log = self.run_render(["-I", "mamutanalog", "-c", "1",
                "-r", str(rate), "--voice-limit", "16", "-t", ".002"])
            self.assertIn("1 note-ons, 1 note-offs", log)
            self.assertIn("two runs identical", log)
        path, _ = self.run_render(["-I", "mamutanalog", "--seed", "0", "-t", ".002"])
        first = pcm(path)
        path, _ = self.run_render(["-I", "mamutanalog", "--seed", "18446744073709551615",
                                   "--detune", "100", "-t", ".002"])
        self.assertNotEqual(first, pcm(path))

    def test_zero_tail_final_event_and_empty(self):
        (self.root / "one.mid").write_bytes(midi(bytes([0, 0x90, 60, 100, 0, 0xff, 0x2f, 0])))
        path, log = self.run_render(["-I", "mamutanalog", "-t", "0"], source="one.mid")
        self.assertEqual(len(pcm(path)), 8)
        self.assertIn("1 note-ons", log)
        (self.root / "empty.mid").write_bytes(midi(bytes([0, 0xff, 0x2f, 0])))
        path, log = self.run_render(["-I", "mamutanalog", "-t", "0"], source="empty.mid")
        self.assertEqual(pcm(path), b"")
        self.assertIn("0 note-ons", log)

    def test_invalid_options_do_not_touch_output(self):
        invalid = [
            ["--voice-limit", "0"], ["--voice-limit", "65"], ["--voice-limit", "32x"],
            ["--voice-limit", "3", "--unison", "2"], ["--unison", "3"],
            ["--unison", "0"], ["--unison", "16"], ["--seed", "-1"],
            ["--seed", "18446744073709551616"], ["--seed", "7x"],
            ["--detune", "nan"], ["--detune", "inf"], ["--detune", "-1"],
            ["--detune", "101"], ["--block-frames", "0"], ["--block-frames", "4097"],
            ["--patch", "Unknown"], ["-r", "nan"], ["-c", "16"], ["-c", "1,"],
            ["-f"], ["-R", "888000000"], ["-D", ".5"], ["-C", ".5"],
            ["--chorus-mix", "nan"], ["--reverb-mix", "inf"],
            ["--chorus-mix", "1.01"], ["--reverb-size", "-1"],
            ["--chorus-depth", "2"], ["--chorus-rate", ".029"],
            ["--chorus-rate", "20.1"], ["--reverb-damping", "1"],
        ]
        out = self.root / "output.wav"
        for options in invalid:
            with self.subTest(options=options):
                out.write_bytes(b"preserve")
                self.run_render(["-I", "mamutanalog", *options], code=2)
                self.assertEqual(out.read_bytes(), b"preserve")
        self.run_render(["-I", "organ", "--voice-limit", "16"], code=2)
        self.run_render(["-I", "ep73", "--patch", "Lead"], code=2)
        self.run_render(["-I", "organ", "--chorus-mix", "0"], code=2)
        self.run_render(["-I", "ep73", "--reverb-size", ".5"], code=2)

    def test_legacy_instrument_selection(self):
        path, _ = self.run_render(["-t", ".02"])
        default = pcm(path)
        # Captured from the pre-MA3-1 renderer at commit 56eb8fd.
        self.assertEqual(hashlib.sha256(default).hexdigest(),
                         "8aae1ee6d6b08cedc92fb36d9e03c1299b23bb30edbef4406191063e037da4f7")
        path, _ = self.run_render(["-I", "organ", "-t", ".02"])
        self.assertEqual(default, pcm(path))
        path, _ = self.run_render(["-I", "ep73", "-I", "organ", "-t", ".02"])
        self.assertEqual(default, pcm(path))
        path, _ = self.run_render(["-I", "ep73", "-t", ".02"])
        self.assertNotEqual(default, pcm(path))
        self.assertEqual(hashlib.sha256(pcm(path)).hexdigest(),
                         "d0e458f77e05f256d00dedd10d1761c98cda203fbef79590b4bd69624639c4e9")


if __name__ == "__main__":
    unittest.main()
