# sweetgl for Pixel 6a

Work in progress: this directory is not yet a buildable or deployable GPU
renderer. The GPU-independent EAGL shim, dispatch generator, layer dumper,
headers and libdrm declarations are reused with permission from joranv1's
[ios-sailfish](https://github.com/joranv1/ios-sailfish), commit
`ce637521bacc06fab5cc29cf144febee2fca9ff8`.

The draft Mali backend translates Panfrost ioctls to the contract in
`kernel/include/SweetGPUABI.h`. Its sanitizer-backed host tests pass using
mock IOKit responses. The kernel SweetGPU service is still pending; these
tests do not establish GPU execution. Prime FD proxies support same-process
imports while the underlying BO is alive; they are not cross-process dma-bufs.

Mesa research checkout: `research/mesa`, upstream commit
`73a62223796ae3231e662436ae53083f529a9c6b`. The G78 model entry is reused from
[m8l8th814n-eng/mesa-gs101](https://github.com/m8l8th814n-eng/mesa-gs101),
commit `b2eee90baf71345690466df19a9a99425f95971d`. `mesa.patch` adds that entry,
iOS BO mapping lifetime handling and the libdrm shim dependency.
`mesa-setup.sh` builds native shader tools and configures the iOS cross-build;
`build.sh` links and ad-hoc signs the library. The `sweetgl-bluejay` CI job
exercises the full build. Device integration and a successful CI artifact
are still pending. See
[GPU bring-up evidence](../../docs/gpu-bringup.md).
