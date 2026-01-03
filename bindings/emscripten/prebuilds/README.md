This directory is reserved for Node.js native addon prebuilds.

- CI/release should populate `prebuilds/<platform>-<arch>/colibri_native.node` (and the required `libc4` shared library next to it).
- The npm package uses conditional exports so browsers never resolve these files.


