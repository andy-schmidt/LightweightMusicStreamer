# LightweightMusicStreamer
A simple Windows music streamer that does not use a lot of memory. This is a traditional Windows Desktop application utlitizing User32-style windows for the GUI, and Windows Runtime [Windows.Media.Playback.MediaPlayer](https://learn.microsoft.com/en-us/uwp/api/windows.media.playback.mediaplayer?view=winrt-22621) to play internet music streams. This was created in attempt to listen to streaming sources without consuming the memory that Spotify or a web browser requires.

The initial 64-bit implementation consumes ~4 MB when idle and ~9 MB during music playback. The memory required does vary with the streaming source. Most of the memory consumption is by the instance of Windows.Media.Playback.MediaPlayer.

This is a hobby project.

## Lanaguage and runtime
C++ 20 with Visual Studio and C++/WinRT implementing a traditional Windows Desktop application. However, Windows 10 is required by the dependency on Windows.Media.Playback.MediaPlayer.

## Streaming source
The streaming source is statically set to [DeepInradio](https://www.deepinradio.com/). The Jazz station [KCSM](https://kcsm.org/) has also been used.
