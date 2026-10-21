# WebKit white-page investigation

Phone: iOS 16.7.16, WebKit 615.8.1.10.3 (queried from the installed
image's shared cache). Plain HTTP requests from the phone to the Mac return
200, but Safari displays white. User reports the same after disabling GPU
Process DOM Rendering and Canvas Rendering and restarting Safari.

## Evidence and test

The first captured WebKit GPU process creates an uncompressed `b3a8` surface,
1125x1536, two planes. The final backboardd display swaps use BGRA. A final
framebuffer conversion cannot repair content which was never painted.

Matching WebKit source reads `IOSurfaceGetPixelFormat` when selecting its
CoreGraphics context. Deep-color surfaces select a half-float context;
BGRA selects an 8-bit context. The iOS binary confirms that
`CGIOSurfaceContextCreate` delegates to QuartzCore's IOSurface drawable, whose
CPU destination setup supports a restricted format set. CPU fallback exists;
moving the context out of the GPU process does not remove the deep-color
surface request.

Experimental boot argument `sweetweb_bgra=1` normalizes only new, parentless
`w30r`/`b3a8` requests from the exact processes `com.apple.WebKit.WebContent`
and `com.apple.WebKit.GPU` to a complete BGRA surface. Format, element size,
stride, offset, plane count, and allocation size are changed together before
allocation, mapping, or painting. Userspace receives the actual BGRA layout;
no already-painted RGB10 bytes are relabeled. Other clients and child surfaces
retain their requested formats. This is a diagnostic compatibility policy,
off by default, not general support for RGB10 surfaces and not yet a verified
fix for the white page. Rebooting a kernel without the argument disables it.

Separately, `b3a8`'s first plane defaults to four bytes per element when the
caller omits PlaneBytesPerElement, as WebKit does. The alpha plane defaults
to one. Previously both defaulted to one.

Host check: `clang++ -std=c++17 -Wall -Wextra -Werror
-fsanitize=address,undefined tools/surface-format-test.cpp -o /tmp/surface-test`
and run it. Checks include exact process gating, disabled mode, child surfaces,
plane defaults, 1125x1536 geometry, invalid dimensions/alignment, the 256 MiB
limit, and allocation coverage over varied dimensions.

Read-only analysis outputs are under `out/graphics-research/`. The existing
phone data and installed boot are not changed by preparing this test.

Source reference (December 2022):
https://github.com/WebKit/WebKit/blob/ac18fb1c144afb42706b9012347ce37fc47226da/Source/WebCore/platform/graphics/cocoa/IOSurface.mm
The local binary, rather than this nearby source revision, determines the
installed IOSurface user-client ABI.

## Phone result and next diagnostic

The user confirms that the BGRA test makes only the webpage area black;
Safari controls and the rest of the phone still work. Local requests still
return 200, and the kernel log verifies the requested WebKit formats were
normalized. The experiment did not fix rendering. GPU process creation alone
does not prove accelerated hardware drawing or that the preference toggles
failed: other graphics work can still use that process.

`sweetweb_trace=1` adds a read-only diagnostic, independent of BGRA policy.
New surfaces created by the exact WebKit GPU/WebContent processes are tagged.
Every two seconds the existing display timer samples at most 32 retained
surfaces, outside display/surface locks, up to 30 polls per surface. Only
single-plane BGRA/4-byte-element buffers with validated allocation bounds are
sampled. The log contains counts of sampled nonzero pixels, RGB and alpha;
it contains no raw pixel values or text. Sampling cannot prove an entire
surface is blank. Concurrent painting means it is an approximate observation.

Mapping/export/lock/unlock events are capped at 16 per surface. WebKit user
client method signatures are capped at 64 per client; unlike the older global
once-per-selector log, these show the WebKit calls even when SpringBoard
already used the same selector. No missing method is newly advertised as
implemented. The diagnostic is off by default and does not change page data,
rendering formats, or synchronization. Host sanitizer checks cover transparent,
opaque black, colored, padded/offset and short/out-of-bounds buffers.
