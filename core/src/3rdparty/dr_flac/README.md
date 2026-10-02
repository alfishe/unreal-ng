# dr_flac

Single-header FLAC decoder by David Reid, https://github.com/mackron/dr_libs, public domain
(Unlicense) or MIT No Attribution, at the reader's choice (the license text is at the end of
`dr_flac.h`); both are compatible with the project's GPLv3. Vendored unmodified from dr_libs
`master` at commit `dfe8377631000664666519fdb83da193fd8037f4` (2026-10-02, "dr_flac v0.13.4 - TBD":
v0.13.3 plus its seeking fixes).

Used by the audio-CD-from-a-folder builder (`core/src/emulator/io/storage/cd/audiofiledecoder.cpp`,
which compiles the implementation with `DR_FLAC_NO_STDIO` and `DR_FLAC_NO_OGG`: the file is read
into memory by the caller, so UTF-8 paths work on every host). The CHD `flac` codec keeps its own
decoder (`chd/chdflac.*`): it reads bare 16-bit stereo frames only, while host FLAC files are any
bit depth and channel count.
