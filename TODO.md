# Navigation development backlog

Requested 2 November 2026. These are planned features, not additions to 5.6.1.

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
- [ ] Assess how lunar-derived time comparisons could be recorded in Chrono,
  while retaining their source and observational uncertainty.
- [ ] Establish independent numerical checks and clear operating limits before
  presenting the feature as usable.

## Design preference

Keep the familiar almanac-to-reduction-to-plot sequence easy to follow. Favor
compact instructions and traditional notation that experienced users can
memorize, while making sign/branch decisions explicit for a rusty beginner.
Forms should help with pencil arithmetic and checks, without becoming mandatory.
