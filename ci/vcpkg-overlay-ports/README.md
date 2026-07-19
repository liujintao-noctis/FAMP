# Windows CI vcpkg overlays

The Windows GitHub Actions job passes this directory through
`VCPKG_OVERLAY_PORTS`.

`libaec` keeps the builtin vcpkg 1.1.6 build recipe but obtains the tagged
source archive from the maintainer organization's GitHub repository. The
builtin GitLab archive endpoint repeatedly returned HTTP 429 to hosted Windows
runners. The archive checksum is fixed in `libaec/portfile.cmake`; update the
version, tag, and SHA-512 together when the builtin vcpkg port changes.
