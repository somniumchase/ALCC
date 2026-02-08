# Contributing to ALCC

We love your input! We want to make contributing to ALCC as easy and transparent as possible, whether it's:

- Reporting a bug
- Discussing the current state of the code
- Submitting a fix
- Proposing new features
- Becoming a maintainer

## Development Process

We use GitHub to host code, to track issues and feature requests, and to accept pull requests.

1.  **Fork the repo** and create your branch from `main`.
2.  **Make commits** of logical units.
3.  **Run Tests:** Before pushing, ensure all tests pass.
    ```bash
    cd ALCC
    ./verify_v2.sh
    ```
    This script runs the verification suite to ensure no regressions.

4.  **Push** to your fork and submit a pull request.

## Pull Request Process

1.  Update the `README.md` with details of changes to the interface, this includes new environment variables, exposed ports, useful file locations and container parameters.
2.  Increase the version numbers in any examples files and the README.md to the new version that this Pull Request would represent. The versioning scheme we use is [SemVer](http://semver.org/).
3.  You may merge the Pull Request in once you have the sign-off of two other developers, or if you do not have permission to do that, you may request the second reviewer to merge it for you.

## Coding Style

- **Language:** C++17.
- **Formatting:** Please follow the existing code style (indentation, bracket placement).
- **Documentation:** All public functions should be documented.

## Any questions?

If you have questions, feel free to open an issue or contact the maintainers.

## License

By contributing, you agree that your contributions will be licensed under its AGPL-3.0 License.
