Design and implement a music translation layer with the following in mind:

* the idea is to morph the music style while keeping the core motifs intact, i.e. each binary fed to cheapbin would still keep its musical DNA, we would be just transforming the style before we output the music through a selected synthesizer chip, but the melodies, rhythms, and overall structure of the music would still be recognizable as the original composition
* for now, by default start the program w/o applying this translation layer
* we should have a CLI argument to force a style and also a UI shortcut to change style in runtime, same as we have for the synthesizer chip selection
* the current style name should be also displayed in the UI, so the user is always aware of the current transformation being applied to the music

to start with you can implement the following styles in this translation layer:

* Synthwave / Outrun: A nostalgic, retro-futuristic 80s sound characterized by driving, pulsating basslines, a rigid dance beat, and lush, echoing synth leads.

* Dungeon Synth / Dark Fantasy RPG: Slow, atmospheric, and eerie. This style relies on dark minor-key melodies and warm, slowly evolving synthetic choirs or pads, reminiscent of early 90s PC fantasy games.

* Baroque / Bach Counterpoint: Highly structured classical music where multiple independent melodies intertwine perfectly to create complex fugues and canons, similar to classic Castlevania soundtracks.

* Acid House / Minimal Techno: High-energy, hypnotic electronic dance music defined by a relentless 4-to-the-floor beat and a signature "squelchy," highly resonant, and gliding bassline.

* Doom / Sludge Metal: Slow, heavy, and oppressive. This translates to deep, down-tuned bass frequencies and aggressive, fuzzy distortion to simulate massive, sludgy guitars.

* Eurobeat / Trance: Extremely fast-paced and high-energy dance music that thrives on driving off-beat basslines, lightning-fast sweeping arpeggios, and an echoing, stadium-sized atmosphere.

* Demoscene Tracker / Keygen Music: The authentic, chaotic sound of 90s software cracktros. It is packed with rapid-fire arpeggios simulating chords, punchy staccato bass, and crisp, syncopated synthetic percussion.

* 8-Bit Ska / Reggae: Bouncy, upbeat, and groove-heavy. This style focuses heavily on off-beat, syncopated rhythmic chords (the "skank") paired with a walking, highly melodic bassline.

* Trap / Lo-Fi Hip Hop: Slow, head-nodding grooves featuring booming, sub-heavy 808-style bass drops layered under rapid, rolling, and intricate hi-hat patterns.

* Progressive / Math Rock: Complex and musically unpredictable. It is characterized by shifting, odd time signatures (like 7/8 or 5/4) and disjointed, highly syncopated melodic phrasing.

Task Execution Phases:

* Thoroughly inspect the existing codebase to identify where the music translation layer can be integrated effectively
* Design the architecture for the music translation layer, review the styles to be implemented in case there are any specific requirements or constraints, ensure that the design allows for easy addition of new styles in the future
* Implement the music translation layer
* Validate that the project compiles and runs correctly
* Update the README.md file, briefly describing the music translation layer, how to use it, and mention some of the styles that are currently implemented


Implement a solid translation layer, make no mistakes, take as long as you need to complete this task