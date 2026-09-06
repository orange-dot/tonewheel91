# MA2 Blade Runner Main Titles exhibit

The hosted exhibit reads `/home/dev/Downloads/VANGELIS.Blade runner.MID` by
default and renders a MA2 reinterpretation to
`build/ma_blade_runner_main_titles.wav`.

```sh
make exhibit-ma-blade-runner-main-titles
build/exhibit_ma_blade_runner_main_titles -i /path/to/other.mid -o build/other.wav
```

The source channels are grouped into four fixed five-card banks: pad/string,
bass, synth motif, and harp/bell/percussion texture. Note ownership, repeated
notes, release, sustain, panic, pitch bend, channel pressure, and poly pressure
are handled at the MA2 bank boundary. The render is performed twice and the
reported FNV64 must match before the WAV is accepted.
