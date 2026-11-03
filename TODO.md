# Navigation development backlog

Requested 2 November 2026; lunar requirements clarified 3 November 2026.
These are planned features, not additions to 5.6.1.

## Moon altitude corrections — priority

- [ ] Replace the coarse Moon parallax/component page with a practical combined
  Moon altitude-correction section closely following the Nautical Almanac's
  lookup arrangement and workflow. Confirm details against a reference edition
  before designing the pages; generate the numerical values with our own code.
- [ ] Cover the required lower/upper limb corrections, horizontal parallax,
  semidiameter/augmentation and correction order, with explicit instructions
  for the inputs and any separately applied dip/index corrections.
- [ ] Choose finer altitude/HP spacing from measured interpolation errors,
  especially where corrections change rapidly. Print corrections to 0.1 minute
  and document interpolation and the measured error budget.
- [ ] Compare table lookups/interpolations against the calculation engine over
  the supported range, including edge cases, then check against an independent
  reference. Inspect the rendered PDF and LaTeX layouts for pencil use.

Current status: `navbook.cpp::corrections()` produces a shared dip/refraction/
parallax component page. Its parallax grid uses altitude steps of 5 degrees and
HP columns of 54, 57, 60 and 63 minutes. This is not the complete official-style
combined Moon limb table. The coarse grid remains an unfinished item.

## Actual NAO Concise sight-reduction tables

- [ ] Add generated NAO Concise lookup tables as an independent Annual book
  option, alongside compact Ageton and the calculator guide. Allow the user to
  include either method, both, or neither without removing other book sections.
- [ ] Use the Nautical Almanac method's traditional variables, sign and quadrant
  rules, with clear handling of the branch cases. Check against a reference
  edition before fixing table ranges, spacing and printed instructions.
- [ ] Provide a compact, plain-language algorithm that can be followed on scratch
  paper or memorized. A pencil worksheet is optional, not a requirement.
- [ ] Validate table-based Hc/Zn against direct calculations across hemispheres,
  LHA ranges and difficult geometries. Document interpolation and rounding.
- [ ] Support PDF and LaTeX output, optional row shading, worked examples and
  practical page layout consistent with the rest of the book.

Current status: the section titled "Concise calculator sight reduction" is
NavAlm's direct formula guide. It is not the NAO Concise lookup-table section.
Compact Ageton is already implemented and should remain available independently.

## Lunar distances — future feature

- [ ] Plan lunar-distance observations and reductions ("lunars"). Define the
  observing workflow, required inputs, outputs and reference examples before
  implementation; decide separately whether predicted distances belong in the
  generated annual book.
- [ ] Give the Moon altitude and other-body altitude their own **Measured Hs**
  checkbox. Checked means a real observation; unchecked means a synthetic
  altitude calculated from the AP. Support a measured/synthetic mixture and
  label calculated values clearly. Preserve entered readings when switching
  modes, but exclude them from the observation constraints while synthetic.
- [ ] Give each real altitude its own recorded watch date/time, separate from
  the lunar-distance timestamp. Allow simultaneous readings as a convenience,
  while supporting different epochs and midnight crossings. The observer need
  not already know the corrected UTC of any shot.
- [ ] Preserve the raw Hs, recorded watch timestamps, limb and correction
  settings. Define the solved correction explicitly as **UTC minus watch
  time**. Apply that common trial correction to every real observation at its
  own epoch; display the derived UTC separately without overwriting the
  recorded watch reading. Account for existing Chrono/manual corrections
  exactly once and retain a readable calculation audit. Label this a clock or
  UTC correction, not astronomical delta T (TT minus UT1).
- [ ] In synthetic mode, hold the supplied AP/DR position model fixed and
  regenerate the altitude/correction geometry at the corrected lunar-distance
  epoch on every trial. Include parallax, refraction and the selected limb,
  index and horizon conventions when displaying synthetic Hs. Synthetic
  altitudes are auxiliary geometry, never independent measurements that move
  the AP, create a second altitude-circle fix, or increase apparent certainty.
- [ ] With enough real observations, optionally refine position and clock
  correction together, or iterate lunar time and the real altitude fix until
  both stabilize and angle residuals agree with observation uncertainties.
  Model each shot at its own corrected epoch, including vessel motion between
  readings when supplied. A lunar plus only one real altitude still needs a
  positional constraint; do not report a fully determined position/time fix
  from an underdetermined mixture.
- [ ] When real altitude observations admit two position intersections, use DR
  proximity to identify the plausible branch and show the alternative rather
  than silently selecting it. Fixed-AP, synthetic-altitude mode must solve the
  distance/time problem without introducing artificial position branches.
- [ ] Assess how lunar-derived time comparisons could be recorded in Chrono,
  while retaining their source and observational uncertainty.
- [ ] Establish independent numerical checks and clear operating limits before
  presenting the feature as usable. Cover synthetic versus measured modes,
  separate epochs, midnight rollover, common clock-offset recovery, mixed
  inputs, position ambiguity, and prevention of duplicate clock corrections.

## Design preference

Keep the familiar almanac-to-reduction-to-plot sequence easy to follow. Favor
compact instructions and traditional notation that experienced users can
memorize, while making sign/branch decisions explicit for a rusty beginner.
Forms should help with pencil arithmetic and checks, without becoming mandatory.
