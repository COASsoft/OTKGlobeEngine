# Security policy

## Reporting a vulnerability

Email **grangedevgroup@gmail.com** with "Security" in the subject. Say what is affected (product, version and platform), how to reproduce it, and what an attacker could do with it. Please keep the details private until a fix is released.

You will get a reply within three working days. Anything that puts user data, accounts or devices at risk is fixed and released as soon as possible; other confirmed issues are fixed in the next release. Reporters are credited once the fix is out, if they want to be.

## Supported versions

OpenTAKEngine has no tagged releases yet. Fixes land on `main`; build from the latest commit.

## How this repository is checked

- Secrets: Betterleaks checks every commit before it is made and scans the full history weekly.
- Code: Opengrep scans nightly with the community rule set.
- C++: cppcheck runs weekly.
