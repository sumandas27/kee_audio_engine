# kee_audio_engine
C++ audio engine in OpenAL for my rhythm game 'kee'

Check out my video on this audio engine! https://www.youtube.com/watch?v=qnPQy5GQvks 

## Functionality

* Getting/setting audio engine volume.
* Playing a sound effect
* Pausing/unpausing/stopping all active sound effects
* Getting the duration of an audio file

Access to 4 music players
* Setting a music player with some music file / unsetting a music player
* Playing/pausing a music player
* Getting the play/pause state of a music player
* Setting the playback time of a music player

Only `.wav` files supported for now :(

## Dependencies
* OpenAL Soft - https://github.com/kcat/openal-soft

This is a single-header library. Once you've linked OpenAL to your project, include the `.hpp` and `.cpp` file to your project and you're good to go. You can also compile the source code into a static library if you prefer.
