# nio-core-apps

Core FujiNet-NIO command-line utilities live here.

This repository owns the portable C implementations of the user-facing `F*`
utilities:

- `fapp`
- `fboot`
- `fdrive`
- `fhost`
- `fin`
- `fls`
- `fmount`
- `fout`
- `ftestnio` (Atari only): a two-screen hardware test; every line is a real call to the FujiNet

Supported standalone targets are `msdos`, `atari`, `linux`, and `amiga`.

```sh
make TARGET=msdos FUJINET_NIO_LIB=../fujinet-nio-lib
make TARGET=atari FUJINET_NIO_LIB=../fujinet-nio-lib
make TARGET=linux FUJINET_NIO_LIB=../fujinet-nio-lib
make amiga                         # builds wb13, wb31, and wb32
make amiga AMIGA_PROFILE=wb13      # one explicit profile
```

The Atari boot disk starts the FujiNet NIO config application (`repos/nio-config`, build it first),
staged as `autorun.xex`: PicoBoot runs the first directory entry and `dir2atr` sorts by name (see
`boot-disk/manifests/atari.yaml`). `boot-disk/manifests/atari-ftestnio.yaml` builds the old
hardware bring-up disk that starts `ftestnio`.

Boot disks are repo-owned:

```sh
make TARGET=msdos boot-disk FUJINET_NIO_LIB=../fujinet-nio-lib
make TARGET=atari boot-disk FUJINET_NIO_LIB=../fujinet-nio-lib
```

BBC core utilities are currently generated from `fn-rom`, because the BBC uses
small ASM transient utilities rather than these portable C programs.

The Amiga target uses `fujinet-nio.device` through `fujinet-nio-lib` and persists
the `fnctl` host/path and unit mappings through FujiNet's app-store service.
