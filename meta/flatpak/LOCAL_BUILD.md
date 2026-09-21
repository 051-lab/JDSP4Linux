# Local Flatpak build

Build the application first using the normal qmake workspace. Then create a
repeatable user-local bundle with:

```sh
./meta/flatpak/build-local-bundle.sh
```

The script uses the installed `org.kde.Platform//6.10` runtime and extracts
the small dependency set carried by the official stable JamesDSP package. It
exports the result under the `custom-liveprog` branch and does not remove or
modify the system Flathub installation.

Install the resulting bundle explicitly:

```sh
flatpak install --user --assumeyes ../../build/jamesdsp/jamesdsp-liveprog-custom-liveprog.flatpak
```

Override inputs with `BUILD_ROOT`, `JAMESDSP_BINARY`, `FLATPAK_RUNTIME`,
`FLATPAK_BRANCH`, or an output path argument when needed.

For development scripts stored outside the Flatpak sandbox, grant only the
required host directory when building the local bundle:

```sh
FLATPAK_EXTRA_FILESYSTEMS='/home/soloarch/Workspace:rw' \
  ./meta/flatpak/build-local-bundle.sh
```
