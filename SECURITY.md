# Security Policy

## Supported Versions

The project is research software and currently tracks `main` as the supported
line. Security fixes are applied on `main` and should be consumed by updating
to the latest commit.

## Reporting a Vulnerability

Please report suspected vulnerabilities privately.

1. Open a private security advisory in GitHub if available for this repository.
2. If private advisories are not enabled, open a minimal public issue that
   requests a private contact channel and do not disclose exploit details.
3. Include reproduction steps, affected files, and expected impact.

You can expect:

- Initial acknowledgement within 5 business days
- A triage update after impact assessment
- Coordinated disclosure once a fix is available

## Scope and Notes

This repository is a CPU microbenchmark and analysis toolkit, not a
network-facing production service. Typical risk areas are:

- Unsafe shell command execution patterns in scripts
- Untrusted file parsing in Python post-processing scripts
- Dependency supply-chain concerns in build and CI tooling

High-precision performance numbers are not a security boundary. However,
integrity and reproducibility of benchmark artifacts are treated as critical
for research quality.

