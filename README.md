# R2sampler

Real-time re-sampler

# How to build

## Requirement

* [CMake](https://cmake.org) >= 3.15

## Build wav resampler tool

```bash
git clone https://github.com/aikiriao/R2sampler.git
cd R2sampler/tools/rsampler
cmake -B build
cmake --build build
```

# Usage

## Wav resampler

```bash
./rsampler -r OUTPUT_SAMPLING_RATE -q QUALITY INPUT.wav OUTPUT.wav
```

Example command for resampling input Wav to 48000 Hz.

```bash
./rsampler -r 48000 INPUT.wav OUTPUT.wav
```

# TODO

- [ ] Implement other filter design algorithms

    - [ ] Custom allocator API
    - [ ] Least square method
    - [ ] Remez exchange method

# License

MIT
