# DINE's built-in drum samples

The sounds the Sample stage plays on a kick, snare or tom strip (`docs/DRUM-SAMPLE-REPLACEMENT-SCOPE.md`).
One folder per family; every `.wav` in it is one sound with one velocity layer, named as the SOUND list shows it.
A sub-folder of `.wav` files is one sound with several layers, softest first by file name. Files are decoded on
the message thread by `app/native/SampleLibrary`, summed to mono, trimmed to their onset and peak-normalised;
44.1 kHz material is played at the right pitch through the player's own rate.

The build copies this folder into the app bundle (`Contents/Resources/Samples`); a developer build and the
snapshot tool read it from here. Extra sounds go in `~/Music/DINE/Samples/<kick|snare|toms>/` and appear in
the same list after the built-in ones.

Licence: the Splice Certificate of Content License beside this file, issued to the author on 24 Sep 2026.
