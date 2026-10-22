# sweetgl for Pixel 6a

Experimental GPU renderer: the arm64 iOS library builds and signs in CI;
physical rendering remains unverified. The GPU-independent EAGL shim, dispatch generator, layer dumper,
headers and libdrm declarations are reused with permission from joranv1's
[ios-sailfish](https://github.com/joranv1/ios-sailfish), commit
`ce637521bacc06fab5cc29cf144febee2fca9ff8`.

The draft Mali backend translates Panfrost ioctls to the contract in
`kernel/include/SweetGPUABI.h`. Its sanitizer-backed host tests pass using
mock IOKit responses. The opt-in kernel service is in the tracked XNU patch
and requires `sweetgpu=4` plus a successful hardware WRITE_VALUE self-test.
Host checks do not establish GPU execution. Prime FD proxies support same-process
imports while the underlying BO is alive; they are not cross-process dma-bufs.

Mesa research checkout: `research/mesa`, upstream commit
`73a62223796ae3231e662436ae53083f529a9c6b`. The G78 model entry is reused from
[m8l8th814n-eng/mesa-gs101](https://github.com/m8l8th814n-eng/mesa-gs101),
commit `b2eee90baf71345690466df19a9a99425f95971d`. `mesa.patch` adds that entry,
iOS BO mapping lifetime handling and the libdrm shim dependency.
`mesa-setup.sh` builds native shader tools and configures the iOS cross-build;
`build.sh` links and ad-hoc signs the library. The `sweetgl-bluejay` CI job
exercises the full build. CI run `39736044323` produced the signed library, SHA256
`4b5f1cc84a2c93a31850dfbd04f5b70f769f9d0ee4a39cb84963f613c67e9947`.
Device deployment and rendering remain pending. See
[GPU bring-up evidence](../../docs/gpu-bringup.md).
