# Load-order analysis

## Conclusion

An in-process dyld add-image callback registered before `discord_krisp.node`
loads is early enough to observe a newly mapped image before its Mach-O
initializers in the controlled fixture. The callback runs inside the loading
process and can identify the image path, Mach header, slide, region
protections, and COW sharing mode.

That ordering does **not** make the Discord design viable by itself. No
acceptable way to admit and start such an observer in the current Renderer has
been identified, and Krisp is loaded during Renderer startup rather than on
Mic Test.

## Discord/Node load sequence

The installed `discord_krisp/index.js` executes:

```js
const KrispModule = require('./discord_krisp.node');
// prepare log parameters
KrispModule._initialize(initializationParams);
```

Node native addons are Mach-O bundles. Electron supports native Node modules,
and Node's addon loader ultimately uses the platform dynamic loader. On macOS
that is a `dlopen`-class image load. Static evidence in the current bundle is
consistent with this chain:

```text
Renderer JavaScript require("./discord_krisp.node")
  -> Node native-addon loader / process.dlopen
  -> libuv uv_dlopen
  -> macOS dlopen/dyld maps the MH_BUNDLE
  -> dyld image-add notifications
  -> Mach-O __mod_init_func initializers
  -> Node-API registration (_napi_register_module_v1 at 0x34ad0)
  -> require returns the exports object
  -> JavaScript calls KrispModule._initialize(...)
```

Primary references:

- [Electron native Node modules](https://www.electronjs.org/docs/latest/tutorial/using-native-node-modules)
- [Apple dyld(3) image notification API](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dyld.3.html)
- [Apple dynamic-loader event descriptions](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/LoggingDynamicLoaderEvents.html)

The current x86_64 slice has 43 pointers in
`__DATA_CONST,__mod_init_func` (size `0x158`). They include Krisp C++ global
initializers, OpenSSL CPU setup, and `___cpu_indicator_init`. None of the 16
located direct calls to `_mkl_serv_intel_cpu_true` is itself an initializer
entry. This lowers, but does not eliminate, the possibility of a transitive
call from an initializer. Dynamic tracing of the predicate was not performed.

## Controlled fixture

Sources are in `Tools/LoadOrderFixture`. The fixture contains:

- a host that registers `_dyld_register_func_for_add_image` before `dlopen`;
- a dynamically loaded library with a constructor;
- a test predicate that the constructor calls immediately; and
- a monotonic event log.

No executable byte is modified. The callback makes only read-only region
queries.

Command:

```sh
Tools/LoadOrderFixture/run.sh
```

Observed order on macOS 15.7.7 x86_64:

```text
01 host-start           registering image observer
02 before-dlopen        .../libFixture.dylib
03 image-add-callback   ... prot=r-x max=rwx shared=no share-mode=COW
04 initializer          library constructor entered
05 initializer-before-predicate calling predicate from constructor
06 predicate            test predicate executed
07 after-dlopen         dlopen returned
08 before-predicate     calling test predicate
09 predicate            test predicate executed
10 after-predicate      result=7
```

This verifies in the fixture:

1. the image is mapped when the callback runs;
2. its path, header and ASLR slide are knowable;
3. its mapping is accessible to the process for read-only VM inspection;
4. the callback precedes even a predicate call made by the library
   constructor; and
5. the executable region is reported COW (`SM_COW`) with current RX and
   maximum RWX protections.

It does not verify Discord's actual region mode, nor does maximum RWX prove a
future protection change will be permitted under Discord's Hardened Runtime.

## Earliest verified points

| Point | Fixture | Current Discord |
|---|---|---|
| Image mapped | image-add callback | Known in principle; not observed live |
| Identity knowable | callback had canonical path/header/slide | Requires path plus code-signature/CDHash validation in callback |
| Private/COW mapping accessible | callback observed `SM_COW` | Not measured |
| Before any initializer/predicate | verified by event order | Callback semantics strongly support it, but observer admission is absent and Krisp-specific predicate trace is missing |

The earliest **verified general mechanism** is the in-process dyld image-add
callback. The earliest **verified Discord point** remains unresolved because
no observer can currently be admitted without a prohibited or unsupported
mechanism.

## Mic Test timing

Krisp loads at Renderer startup. Historical logs repeatedly show SDK
initialisation immediately after renderer/native-module startup, with
`KrispNCSetup` later when audio activity or Mic Test begins. Polling on Mic
Test is therefore too late for a pre-first-execution guarantee.

Polling for a module mapping is rejected as an architecture: Node calls
`_initialize` synchronously immediately after `require`, so there is no proven
inter-process scheduling window between mapping and first relevant execution.

## Remaining ordering experiment

After the unsafe kext is disabled and the original file restored, the smallest
read-only Discord experiment is to take two snapshots with
`Tools/ReadOnlyKrispProbe/run.sh /restored/absolute/path/to/discord_krisp.node`:
one as early as possible after normal launch and one after Mic Test. This can
establish mapping/residency evidence but cannot prove callback ordering. A
Krisp-specific proof of first predicate
execution would require an admitted in-process observer or debugger, neither
currently allowed by the target signing policy.
