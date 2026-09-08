# MA2-6 — integrated MA2 evidence and closure

Run date: 2026-09-08. Status: **done**.

MA2-6 is an integration and evidence slice. It adds no synthesis behavior and
does not change the MA core. This run combines the already landed allocator,
full-compass, character, stereo/body, cost and listening gates.

## Run boundary and artifacts

The canonical run commands are:

```sh
env CCACHE_DIR=/tmp/tonewheel91-ma26-ccache make CC=/usr/bin/gcc test bench-ma > build/ma2-6-gcc.log 2>&1
env CCACHE_DIR=/tmp/tonewheel91-ma26-clang-ccache make CC=/usr/bin/clang BUILD=build/ma2-6-clang test > build/ma2-6-clang.log 2>&1
env CCACHE_DIR=/tmp/tonewheel91-ma26-sanitize-ccache ASAN_OPTIONS=detect_leaks=0 make CC=/usr/bin/clang BUILD=build/ma2-6-sanitize TEST_EXTRA= CFLAGS='-std=c23 -O1 -g -Wall -Wextra -Wpedantic -ffp-contract=off -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined,float-cast-overflow' test > build/ma2-6-sanitize.log 2>&1
env CCACHE_DIR=/tmp/tonewheel91-ma26-listen-ccache make CC=/usr/bin/gcc audition-ma2-4 audition-ma2-5 > build/ma2-6-listening.log 2>&1
```

The only supported build and runtime target is this x86-64 Linux host
(`Linux 7.1.8-200.fc44.x86_64`). The raw logs are
ignored build artifacts and their SHA-256 values are:

| Artifact | SHA-256 |
| --- | --- |
| `build/ma2-6-gcc.log` | `d640a412ca1bb2a4d629e074ebd10ca9af589254a1900b3a897b6494697a0a20` |
| `build/ma2-6-clang.log` | `80364483adf3ca8946ad881416ac78bfec87e15c4644d2e218c4f2916e3d9f87` |
| `build/ma2-6-sanitize.log` | `d178b8c3a9c941d837bcb1d56ae2a5681a6d662945f1d40ca8f0248fcf728560` |
| `build/ma2-6-listening.log` | `3b857f0aebb7c3e5808bd439545693ee1b9f209bbae36ce3180a37f652fcd593` |

The listening log is retained as a local run artifact and is already hashed
above.

## Results

All three compiler modes pass the same suite:

| Suite | Checks | Failures |
| --- | ---: | ---: |
| Core | 111378 | 0 |
| Hosted | 116 | 0 |
| MIDI map | 22 | 0 |
| MA architecture | 227 | 0 |
| MA2 character | 8700 | 0 |
| MA2 stereo | 2611 | 0 |

The listening exhibit repeats all six MA2-4 character files and all eight
MA2-5 stereo/body files. Every repeat is exact. The pinned MA2-5 hashes remain
unchanged, including `1a254c3750cc209f`, `6b4ddec5f3916af7` and
`028a8d6205d46ad5`.

The GCC benchmark repeats the twelve pinned PCM hashes. State remains fixed at
1808 bytes per `ma_synth` and 9328 bytes per `ma_card_bank`. The hard host
cases remain `granica-character-100` and `granica-stereo`; their observed p99
CPU times exceed the 1333.33 µs half-deadline target. This confirms the
existing warning rather than hiding it behind a host average.

## Gate status

- allocator, compass, character, stereo/body and deterministic listening: **pass**;
- GCC, Clang, ASan/UBSan/float-cast-overflow and symbol/test gates: **pass**;
- host cost characterization: **pass with recorded budget warning**;
- Raspberry Pi 3B-class live/cost soak: **removed from target scope**;
- operator listening verdict for public MA2 closure: **accepted by operator**.

MA2-6 is closed. The host budget warning remains recorded for future
optimization work, but it is no longer a release blocker because the Pi-class
target was removed from the product scope. No C source or core behavior
changed in this slice; this document and the ignored local logs are the
evidence record.
