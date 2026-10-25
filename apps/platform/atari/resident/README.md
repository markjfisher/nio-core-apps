# Atari resident N: handler

`fnresident_asm.s` is the production resident: one hand-written CA65 object,
linked by LD65 as relocatable o65. It links no C, library archive, runtime,
heap, software stack or external resident workspace. The C module and its
assembly gateway remain the functional reference (`fnresident-module`).

Build from the workspace after sourcing `scripts/env.sh`:

```sh
make -C repos/nio-core-apps -f makefiles/build.mk TARGET=atari fnresident-asm
make -C integration-tests/atari-sio/boot-app test-resident-asm resident-o65-mydos-basic
```

The build runs `tools/check_atari_resident_ram.py` on the o65 header and link
map. It rejects additional linked objects, implicit stacks and total RAM
above 3,072 bytes. The production installer independently rejects nonzero
software-stack metadata and an allocation plus its 26-byte ZP bank above
that limit.

Current permanent footprint:

| Component | Bytes |
| --- | ---: |
| Code | 1,491 |
| Initialized CIO table and ownership marker | 20 |
| Two owner records | 32 |
| Two 128-byte caches | 256 |
| Shared packet and retained-response drain buffer | 512 |
| Gateway and wire state | 23 |
| Conservatively charged zero-page bank | 26 |
| Dedicated stack | 0 |
| **Total** | **2,360** |

MEMLO advances by 2,334 bytes. The two used ZP bytes are borrowed/restored
inside the foreground gateway; all 26 bytes declared to the existing loader
are counted conservatively. JSR/PHA use the existing OS hardware stack only
while a call runs and return at the original depth. The transient installer,
source file and relocation records are not retained. The reference C image
requires 14,244 bytes including its 256-byte software stack and 26-byte ZP
bank.

The Atari owns CIO dispatch and errors, two IOCB owners, handles, 32-bit
stream offsets, byte buffering, EOF, and the terminal write commit. Firmware
continues to own URI routing and networking. Requests use the existing `$7f`
W/P/R transport and NetworkDevice OPEN/READ/WRITE/CLOSE/INFO commands. No
firmware adaptation or public API change is needed.

OPEN accepts `N1:` input mode 4 (GET), output mode 8 (PUT), or update
mode 12 (POST), and up to 128 URI bytes. Both output methods set the
unknown-body flag.
Each owner has a 128-byte cache. A full output cache flushes immediately;
STATUS/CLOSE and the first POST response read flush a partial cache and send
exactly one zero-length commit. POST response reads start at offset zero.
Repeated STATUS and CLOSE never repeat that commit. A valid short write
acknowledgement advances the offset and sends only the remaining suffix.
An uncertain exchange faults the output owner: no body or commit replay.
CLOSE attempts remote cleanup and always retires local ownership.

One 512-byte buffer serves requests, responses and retained-response drains.
Normal responses are bounded at 160 bytes; recovery can drain up to 512.
After an uncertain W/P/R completion, later requests must first drain with P/R
or observe idle. Recovery never repeats W. Waiting is bounded at 60 polls. A nonempty short reply is drained, then
rejected as an invalid packet; a zero-length ready reply remains outstanding
because there is no data to retire with R.
Wire device/command, length, checksum, service status, handle and byte-count
checks prevent malformed responses from publishing data or advancing offsets.

The existing streamed DOS loader checks HATABS before copying a payload,
allocates from live MEMLO and publishes only after relocation and initialization.
The assembly marker `FNA1` distinguishes it from the prototype's `FNR1`;
installing over a prototype is rejected as foreign. The disk wrapper remains
FNR1 and the o65 relocation format is unchanged. Repeat installation preserves
every existing resident byte. The MyDOS ATR uses the assembly image by default.

Workspace acceptance retains the original OPEN/GET/PUT controls and adds
fault/partial-acknowledgement cases, streamed install/repeat/foreign/bounds
checks, build-budget boundary tests and a disjoint staged-test layout check.
The genuine MyDOS BASIC FNREAD/FNWRITE fixtures remain unchanged. Their bridge
runner checks loaded tokenized programs, executed results, screen/CPU state,
HATABS/MEMLO and unchanged resident code against production NetSIO services.
