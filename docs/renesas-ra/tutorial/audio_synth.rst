.. _renesas-ra_audio_synth:

Audio and Retro Synth on VK-RA4M2
=================================

This page describes the current timed-DAC audio support on ``VK_RA4M2`` and
shows how to use it for retro game sound effects and simple music playback.
The companion example files live in
``ports/renesas-ra/boards/VK_RA4M2/examples/``.

Overview
--------

The current ``VK_RA4M2`` firmware supports timed DAC playback through the
``machine.DAC`` class.  The sample path is hardware-driven:

- ``DAC.write_timed(...)`` sets up playback
- an ``AGT`` timer provides the sample clock
- ``DMAC`` or ``DTC`` transfers samples from RAM to the DAC data register
- the DAC converts those values to the analog output

This is a good fit for:

- chiptune-style tones
- small wavetable loops
- jump / coin / laser / explosion style sound effects
- simple melodies and arpeggios

It is not intended for long hi-fi PCM playback.

Pins
----

On ``VK_RA4M2``:

- ``P014`` is ``DA0`` on ``J19-5`` and Arduino ``A4``
- ``P015`` is ``DA1`` on ``J19-6`` and Arduino ``A5``

.. note::

   ``J16`` is the boot-mode jumper.  It is not a DAC output.

Basic DAC use
-------------

Use the DAC as a normal analog output::

    from machine import DAC, Pin

    dac = DAC(Pin("P014"))
    dac.write(2048)      # about mid-scale
    print(dac.read())
    dac.write_mv(1650)   # about half of 3.3 V
    print(dac.read_mv())

Timed Playback
--------------

Timed playback uses a 16-bit aligned buffer of 12-bit samples in the range
``0`` to ``4095``.  ``array('H', ...)`` is the recommended buffer type::

    from array import array
    from machine import DAC, Pin
    import time

    wave = array("H", [2048, 3072, 4095, 3072, 2048, 1024, 0, 1024])
    dac = DAC(Pin("P014"))

    dac.write_timed(
        wave,
        440 * len(wave),
        mode=DAC.CIRCULAR,
        transfer=DAC.TRANSFER_DTC,
    )
    time.sleep_ms(500)
    dac.stop()

Playback modes:

- ``DAC.NORMAL`` runs the buffer once
- ``DAC.CIRCULAR`` loops the buffer

Transfer backends:

- ``DAC.TRANSFER_AUTO`` chooses the default backend
- ``DAC.TRANSFER_DMAC`` selects ``DMAC``
- ``DAC.TRANSFER_DTC`` selects ``DTC``

On the current ``VK_RA4M2`` implementation:

- ``DAC.NORMAL`` supports ``DMAC`` and ``DTC``
- ``DAC.CIRCULAR`` uses ``DTC``
- circular mode currently supports up to ``256`` samples
- ``timer=<n>`` can be used to request a specific ``AGT`` channel

Why 256 samples is enough
-------------------------

For retro audio the buffer is usually one waveform period, not a long recorded
sample.  The tone frequency comes from the playback rate:

``sample_rate = note_frequency * table_length``

Examples with a ``256``-sample table:

- ``220 Hz`` -> ``56320 Hz``
- ``440 Hz`` -> ``112640 Hz``
- ``880 Hz`` -> ``225280 Hz``

This is enough for:

- square and pulse waves
- triangle and saw waves
- short looping wavetables
- synthetic game sound effects

Hardware-Verified Status
------------------------

The current firmware on ``VK_RA4M2`` has been verified on real hardware for:

- ``DTC`` circular playback
- ``DTC`` one-shot playback
- ``DMAC`` one-shot playback

The ``DMAC`` path required a TrustZone-related fix so that the selected DMAC
channel uses the correct security attribution before playback starts.

Example Files
-------------

The ``VK_RA4M2`` board example directory includes:

- ``retro_synth.py``: reusable helper for notes, sweeps, arpeggios, and SFX
- ``dac_retro_sfx.py``: ready-to-run coin / jump / laser / explosion demo
- ``dac_retro_music.py``: ready-to-run melody plus arpeggio demo
- ``dac_firmware_probe.py``: direct firmware validation for timed DAC paths
- ``dac_dmac_diag.py``: extra DMAC register diagnostics if you need to debug
  the one-shot transfer path

RetroSynth Helper
-----------------

The board examples include a reusable helper module named ``retro_synth.py``.
It provides:

- waveform tables: ``square``, ``pulse50``, ``pulse25``, ``pulse12``, ``triangle``, ``saw``
- note names like ``"A4"`` and ``"C#5"``
- note playback
- pitch sweep
- arpeggio playback
- simple melody playback
- ready-made sound effects
- automatic ``DMAC`` then ``DTC`` fallback for the noise-burst helper

Example::

    from retro_synth import RetroSynth

    synth = RetroSynth("P014")
    synth.play_note("A4", 120, "pulse25")
    synth.play_arpeggio("A3", (0, 4, 7, 12), step_ms=60, cycles=4, wave="triangle")
    synth.coin()

Built-in sound effects:

- ``coin()``
- ``jump()``
- ``laser()``
- ``explosion()``

Ready Examples
--------------

``dac_retro_sfx.py`` plays a short sound-effects demo:

.. literalinclude:: ../../../ports/renesas-ra/boards/VK_RA4M2/examples/dac_retro_sfx.py
   :language: python

``dac_retro_music.py`` plays a simple melody and an arpeggio bed:

.. literalinclude:: ../../../ports/renesas-ra/boards/VK_RA4M2/examples/dac_retro_music.py
   :language: python

Firmware Probe
--------------

If you want to validate the firmware itself, not just the examples, run the
dedicated probe:

.. literalinclude:: ../../../ports/renesas-ra/boards/VK_RA4M2/examples/dac_firmware_probe.py
   :language: python
   :lines: 1-124

The probe checks:

- ``DTC`` circular playback
- ``DTC`` one-shot playback
- ``DMAC`` one-shot playback

Expected successful output ends with ``ALL PASS``.

Practical Tips
--------------

- Use ``P014`` unless you explicitly want the second DAC channel on ``P015``.
- Use ``array('H', ...)`` for sample buffers.
- Keep sample values within ``0`` to ``4095``.
- Use ``DTC`` circular mode for looping synth tones.
- Use ``DMAC`` one-shot mode for burst effects such as noise or impact sounds.
- For retro sound design, think in terms of synthesis and control-rate changes,
  not long audio clips.

Suggested next steps:

- add more wavetable shapes
- add envelopes and vibrato
- layer multiple effects in Python control code
- build a small game-audio API on top of ``RetroSynth``
