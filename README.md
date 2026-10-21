# FFmpegMovieWriter

Video recording block using FFmpeg for Cinder. Capable of recording video
with audio channel.

Cinder rewrite of Tim Scaffidi's [ofxVideoRecorder](https://github.com/timscaffidi/ofxVideoRecorder).

FFmpegMovieWriter relies on the command line FFmpeg application for A/V
encoding/decoding. You must have FFmpeg installed on your system. By default
`ffmpeg` is looked up on the `PATH`; use `Format::pathFFmpeg()` to point at a
specific executable.

On macOS and Linux frames are sent to ffmpeg through fifos, on Windows through
named pipes. `hasFailed()` reports if ffmpeg could not be started or exited
early; frames added after that are dropped.

Tested in macOS and Linux. Windows support is tested on Windows 11; the macOS / Linux
code paths were touched by that change (failure handling, startup) and need a retest.
