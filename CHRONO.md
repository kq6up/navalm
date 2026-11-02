# Chrono — watch and clock characterization (5.6.0)

The native **Chrono** tab sits just before Annual book. It keeps named clocks and
dated comparisons, with an optional correction for manual sight times. The
browser and CLI continue to use UTC directly; Chrono's logging UI is native.

1. Add a watch/clock name. Both date/time pairs use the **UTC scale**, including
   the watch reading. Convert a watch set to local time to UTC before entering it.
2. For **Manual WWV**, record the watch reading at an identified UTC signal and
   enter that signal's UTC date/time. This version does not decode WWV audio.
   **Other UTC reference** permits another trusted comparison source.
3. **Computer UTC now** freezes the system's current UTC, including milliseconds,
   and marks the source as Computer clock. Pair it with the watch reading at that
   instant, then choose **Add comparison**. This uses the computer's clock; it
   does not independently query a network time server or verify synchronization.
   Reading/reaction delay remains part of the comparison error.
4. Repeat over time. The tab shows estimated watch-minus-UTC error now, drift in
   seconds/day, measurement span and RMS residual scatter. **Positive error means
   fast; positive drift means gaining.** One comparison gives offset only.

The model is an ordinary least-squares straight line fitted to enabled readings
in the selected clock's current series. It is evaluated at reference UTC. Watch
readings are converted using the inverse of that line, rather than simply
subtracting an error evaluated at the watch time. Two distinct instants establish
a rate; measurements spread over longer intervals help distinguish rate from
reading noise. RMS is observed scatter, not a guaranteed accuracy bound. A model
cannot predict temperature effects, a stopped watch or a later change in rate.
Predictions outside the measured span are identified as extrapolations.

**Clock reset / new series** keeps the earlier comparisons but starts a fresh fit
after setting the watch, a battery change or another discontinuity. Select a bad
entry and choose **Exclude / restore selected** to omit it from the fit while
keeping its history. Log rows show the series, reference instant, signed error,
reference source and note; `x` marks an excluded entry. **Export log** writes all
clocks and series to CSV, including original watch readings and exclusions.

## Apply a clock to a sight

On **Sight reduction**, enter the separate **Watch date** and **Watch time** on the
UTC scale, select the clock, and check **Use derived correction from Chrono**.
Reduction calculates UTC from that watch reading, including date rollover and
fractional seconds, fills the UTC fields, then performs the usual astronomy and
sight reduction. The worked report has a watch-time line, the signed correction,
clock name and corrected UTC. With the checkbox off, the UTC fields are used
directly and the watch fields are ignored. Correction is off by default.

The Chrono tab can enable the selected clock for new manual sights. Their time
starts blank so computer UTC is not accidentally treated as a watch reading.
Almanac/Planetarium handoffs and the Sun example already use UTC and turn Chrono
off. Defaults saving stores instrument/DR settings without applying a clock.

A saved sight stores **corrected UTC plus a fixed audit record** of its watch
reading and correction. Reopening it shows UTC with Chrono off. Later clock
comparisons do not change that sight's time or its reduction. If you explicitly
want to recalculate from a new clock model, select the appropriate clock and
enable Chrono; the original watch reading is restored in its separate fields.
Editing the UTC directly clears the earlier clock audit.
The CLI can re-reduce these saved sights using their corrected UTC and print the
clock audit without loading the Chrono model.

## Files and validation

The native log is `.local/chrono.log` beside the application workspace (next to
the native defaults file), with `NAVALM_CHRONO` available as a path override.
It is independent of browser storage and saved sights. Keep a backup along with
the workspace when moving the app. Writes use a temporary file and replacement;
an unreadable or malformed log disables changes until it is corrected/reloaded,
and is not silently overwritten. Use one app instance to edit a given log.

Invalid dates, nonfinite numbers, duplicate active reference instants and errors
over seven days are rejected. Rates exceeding an hour/day are treated as unusable
models; the readings remain available for exclusion or a new series. There is no
automatic outlier removal. Leap seconds are not represented as `:60`; use an
ordinary UTC second when recording a comparison.
