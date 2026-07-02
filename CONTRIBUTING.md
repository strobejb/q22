# Contributing to q22

Thanks for considering a contribution to q22.

q22 is maintained as a Qt 6 Catch22 hex editor and successor to the original
Catch22 HexEdit. The project values focused, practical changes that preserve the
editor's reliability and direct binary-editing workflow.

## Before Sending Changes

- Keep changes scoped to one clear bug fix or feature.
- Follow the existing C++ and Qt style in the surrounding files.
- Avoid unrelated formatting churn.
- Build and test locally when possible:

```sh
cmake --build build/Desktop_Qt_6_10_2-Debug
```

If you cannot run the build or tests, mention that in the pull request.

## Licensing of Contributions

By submitting a contribution, you confirm that:

- You wrote the contribution yourself, or you have the right to submit it.
- You have the right to license the contribution to this project.
- Your contribution may be distributed under the license that applies to the
  file or component you are changing.

The project may license different components differently. In particular,
`src/HexView` may later be released as a reusable library under LGPL-3.0-or-later
or another GPL-compatible open-source license, even if the wider q22 application
is released under GPL-3.0-or-later.

By contributing to `src/HexView`, you agree that your contribution may be
distributed under:

- GPL-3.0-or-later, as part of the q22 application; and
- LGPL-3.0-or-later, if `HexView` is released as a separately reusable
  component.

If you do not want your contribution to be available under those terms, say so
before submitting it.

## Third-Party Code and Assets

Do not add third-party code, icons, fonts, generated files, or other assets
unless their license is compatible with the project and their origin is clearly
documented. Include the upstream source and license in the pull request.

## Pull Requests

In the pull request description, include:

- What changed.
- Why the change is needed.
- How it was tested.
- Any platform-specific behavior affected, especially Windows, KDE/KWin, or
  GNOME/Mutter window chrome.
