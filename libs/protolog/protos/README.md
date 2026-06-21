# Generating Custom Perfetto C API Protos (.pzc.h) for ProtoLog

This document explains how to use the Perfetto `gen_c_protos.py` script found within the Perfetto
source tree (`external/perfetto`) to generate custom C API header files (`.pzc.h`). These headers
are necessary for writing messages using the Perfetto C API.

Since these ProtoLog message definitions are specific to our project, we generate these files
locally using Perfetto's script but to not check them into Perfetto and instead just copy them into
our project to be checked in there.

## Steps

### 1. Navigate to Perfetto Directory

Change to the Perfetto directory within your Android source tree:

```bash
cd $ANDROID_BUILD_TOP/external/perfetto
```

### 2. Initial Perfetto Setup (If First Time)

If you haven't built Perfetto locally in this tree, you might need to install dependencies and
generate default build files:

```bash
# Install build dependencies (if needed)
./tools/install-build-deps

# Generate Ninja build files for the release configuration
./tools/gen_gn_out args out/linux_clang_release
```

### 3. Modify `tools/gen_c_protos.py`

Edit the script `tools/gen_c_protos.py`. Locate the `SOURCE_FILES` list. You need to add your
specific `.proto` files to the `files` list within the *first* dictionary entry.

**Add the following lines** to the `files` list in the first dictionary:

```python
            'protos/perfetto/trace/android/protolog.proto',
            'protos/perfetto/common/protolog_common.proto',
            'protos/perfetto/trace/profiling/profile_common.proto',
```

**Example of where to add the lines:**

```python
SOURCE_FILES = [
    {
        'files': [
            'protos/perfetto/common/builtin_clock.proto',
            # ... other existing perfetto protos ...
            'protos/perfetto/trace/track_event/track_event.proto',

            # ADD YOUR CUSTOM PROTOLOG PROTO FILES HERE \/ \/ \/:
            'protos/perfetto/trace/android/protolog.proto',
            'protos/perfetto/common/protolog_common.proto',
            'protos/perfetto/trace/profiling/profile_common.proto',
        ],
        'guard_strip_prefix': 'PROTOS_PERFETTO_',
        'guard_add_prefix': 'INCLUDE_PERFETTO_PUBLIC_PROTOS_',
        'path_strip_prefix': 'protos/perfetto',
        'path_add_prefix': 'perfetto/public/protos',
        'include_prefix': 'include/',
    },
    {
        # ... Second dictionary, for protozero tests - DO NOT ADD HERE ...
    },
]
```

### 4. Build Perfetto Tools

Ensure the necessary Perfetto components are built, as the script might depend on build artifacts:

```bash
./tools/ninja -C out/linux_clang_release
```
This command builds Perfetto in the specified output directory.

### 5. Run the Generation Script

Execute the `gen_c_protos.py` script, pointing it to the build output directory:

```bash
python3 ./tools/gen_c_protos.py ./out/linux_clang_release
```

### 6. Locate and Copy Generated Files

The generated `.pzc.h` files will be placed under the `include/perfetto/public/protos/` directory
relative to the `external/perfetto` root, mirroring the path structure from `protos/perfetto/`.

Based on the configuration in the script:
-   `protos/perfetto/trace/android/protolog.proto` -> `include/perfetto/public/protos/trace/android/protolog.pzc.h`
-   `protos/perfetto/common/protolog_common.proto` -> `include/perfetto/public/protos/common/protolog_common.pzc.h`
-   `protos/perfetto/trace/profiling/profile_common.proto` -> `include/perfetto/public/protos/trace/profiling/profile_common.pzc.h`

Copy these generated files from `external/perfetto/include/perfetto/public/protos/...` to the
appropriate include directory, here under `$ANDROID_BUILD_TOP/frameworks/native/libs/protolog/protos`.

**Example Copy Command (adjust paths as needed):**

```bash
cp $ANDROID_BUILD_TOP/external/perfetto/include/perfetto/public/protos/trace/android/protolog.pzc.h $ANDROID_BUILD_TOP/frameworks/native/libs/protolog/protos/trace/android/protolog.pzc.h
cp $ANDROID_BUILD_TOP/external/perfetto/include/perfetto/public/protos/common/protolog_common.pzc.h $ANDROID_BUILD_TOP/frameworks/native/libs/protolog/protos/common/protolog_common.pzc.h
cp $ANDROID_BUILD_TOP/external/perfetto/include/perfetto/public/protos/trace/profiling/profile_common.pzc.h $ANDROID_BUILD_TOP/frameworks/native/libs/protolog/protos/trace/profiling/profile_common.pzc.h
```

### 7. Using the Generated Headers

You can now `#include` these `.pzc.h` files in your C/C++ code to interact with the Perfetto C API
using your custom ProtoLog message types.
