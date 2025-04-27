# fastipc - high-throughput interprocess mailbox

*fastipc* is a C++23 Linux-only\* library (with a C++17 interface) with an accompanying deamon, *fastipcd*,
enabling together performant data exchange between applications which only care about the most recent data.

\* We use Linux-specific APIs and only test on Linux, however FreeBSD should work just as well thanks to its compatibility layers.

*fastipc* is distributed under the terms of the Apache 2.0 licence.

## Usage

Pull this repository as a CMake ExternalProject, and add the `fastipc` library as a dependency to your client binary.
Build the `fastipcd` target to obtain the deamon. 

## Development

Git aliases are provided to format, build, lint, and test.
Those can be enabled by running `git config include.path ../.gitconfig` from the repository root.

CMake checks require the *cmakelang* pip-package to be installed, e.g. `pip install cmakelang`

- Format: `git format`
- Build: `git build`
- Test: `git test`
- Lint: `git lint`
- All of the above: `git pre-push`
