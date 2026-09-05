# PlayStation 4 App to Merge PKGs (Hardened Fork)

A hardened utility to merge multi-part split PKG files directly on a PlayStation 4 console. Designed for setups with FAT32 / 32 GB flash drives or slow local network transfers where moving multi-gigabyte packages in full single files is impractical.

![Screenshot](https://github.com/user-attachments/assets/b2c619ee-6c5e-4d0e-bd11-3263f3ccb30a)

> [!NOTE]
> **Provenance & Attribution**: This repository is a hardened fork of [`VityaSchel/ps4-app-merge-pkgs`](https://github.com/VityaSchel/ps4-app-merge-pkgs) (upstream: [`git.hloth.dev/hloth/ps4-app-merge-pkgs`](https://git.hloth.dev/hloth/ps4-app-merge-pkgs)). It introduces end-to-end data integrity manifests, on-the-fly streaming SHA-256 verification, strict input validation, disk-space pre-flight guards, and atomic file publication.

---

## Key Hardening Enhancements

- **Input Safety & Multi-Package Guard (Fixes #1–#4)**:
  - Strict `.pkgpart` format validation requiring `<basename>_<3+digits>.pkgpart`.
  - Hard refusal if multiple distinct package base names exist in `/data/pkg_merger`.
  - Contiguity verification starting at `_001` with zero missing, out-of-order, or duplicate parts.
- **Output Safety & Pre-Flight Space Check (Fixes #5–#7)**:
  - Explicit interactive overwrite confirmation prompt before modifying existing output PKGs.
  - Pre-flight free disk space verification requiring 2× package size available on `/data/pkg`.
  - Safe temporary staging (`<name>.pkg.tmp.merging`) with atomic replacement (`rename()`) and automatic stale temp file cleanup on retry.
- **Splitter Reliability & Atomic Manifests (Fixes #8–#11)**:
  - Exact EOF boundary handling eliminating trailing zero-byte part files.
  - Overwrite guard with `--force` flag and automatic obsolete part cleanup.
  - Atomic generation of `<basename>.manifest.json` recording original file size, chunk geometry, and SHA-256 digest.
- **End-to-End On-the-Fly Verification & Retained Failures (Fix #12)**:
  - Single-pass streaming SHA-256 verification computed during merging (no costly second read pass).
  - Corrupt/mismatched outputs are safely retained as `<name>.pkg.checksum-failed(.N)` for inspection rather than leaving broken PKGs at destination.
- **Security & Supply Chain (Fixes #13–#15)**:
  - Safe bounded string formatting (`vsnprintf`) across message dialogs.
  - Standardized release checksum automation and documented binary provenance.

---

## How to Use It

### 1. Split PKG on PC

Download the `splitter` binary for your operating system from Releases (or build from source):

```bash
# Split with default 15 GB (15,000 MB) chunk size
./splitter /path/to/Game.pkg

# Or specify custom chunk size in megabytes (e.g. 4000 MB for FAT32 flash drives)
./splitter -c 4000 /path/to/Game.pkg
```

This generates:
- Split parts: `Game_001.pkgpart`, `Game_002.pkgpart`, ...
- Integrity manifest: `Game.manifest.json`

### 2. Transfer Files to PS4 (`/data/pkg_merger`)

Transfer all `.pkgpart` files **and** the accompanying `Game.manifest.json` to `/data/pkg_merger` on the PS4.

> [!IMPORTANT]
> **Clean Directory Requirement**: `/data/pkg_merger` must contain part files for **only one package at a time**. The merger will refuse to proceed if files from multiple packages are mixed together.

**Transfer Methods:**
- **Via Direct Network FTP**: Connect from your PC to the PS4 FTP server (e.g., GoldHEN FTP on port 2121 or 1337) and upload the `.pkgpart` files and `.manifest.json` directly into `/data/pkg_merger`.
- **Via USB Drive & PS4-Xplorer 2.0**:
  1. Copy the `.pkgpart` files and `.manifest.json` to your FAT32/exFAT USB flash drive.
  2. Plug the USB drive into your PS4.
  3. Open **PS4-Xplorer 2.0** on the console, navigate to `/mnt/usb0`, and copy/move the files to `/data/pkg_merger`.

### 3. Merge on PS4

1. Open **PKG Merger** on your PS4.
2. The app scans `/data/pkg_merger`, verifies parts and the manifest, and displays the detected package name, part count, total size, and estimated merge time.
3. Press any button on the controller to initiate the merge.
4. If an existing PKG exists at `/data/pkg/<name>.pkg`, you will be prompted to confirm overwrite.
5. The console will merge the parts into `/data/pkg/<name>.pkg` while computing the SHA-256 digest in a single streaming pass.
6. Upon completion, the hash is verified against `Game.manifest.json`.
7. Once successfully verified, install the PKG via GoldHEN Package Installer (`Debug Settings` $\rightarrow$ `Package Installer`), and clean up the parts in `/data/pkg_merger`.

---

## Compiling from Source

### 1. Splitter (Desktop — macOS, Linux)

Prerequisites: CMake 3.13+ and a C++17 compiler (`clang++` or `g++`).

```bash
cd splitter
cmake -B build -S .
cmake --build build
```

### 2. Running the Desktop Test Suite

The core merger and splitter logic is fully decoupled from Orbis OS APIs, allowing native verification on macOS and Linux:

```bash
clang++ -std=c++17 -Icommon -Imerger-app -Isplitter -Itests \
  tests/test_runner.cpp tests/test_crypto_manifest.cpp \
  common/*.cpp merger-app/merger_core.cpp splitter/splitter_core.cpp \
  -o tests/test_runner

./tests/test_runner
```

### 3. PS4 Merger App (Homebrew PKG)

Prerequisites:
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) (v0.5.2 or newer, e.g. v0.5.4)
- LLVM / Clang configured for `x86_64-pc-freebsd12-elf` target with `ld.lld` (Note: Upstream/Homebrew LLVM is required on macOS; Apple Clang is explicitly unsuitable for targeting FreeBSD/Orbis ELF binaries)
- .NET Runtime (e.g., .NET 6.0+ SDK/Runtime) required for OpenOrbis packaging tools ([LibOrbisPkg](https://github.com/OpenOrbis/LibOrbisPkg))
- OpenOrbis helper tools in PATH: `create-fself`, `create-gp4`, `PkgTool.Core`

```bash
# Export the OpenOrbis toolchain directory
export OO_PS4_TOOLCHAIN=/opt/OpenOrbis/PS4-Toolchain

# Build the PS4 homebrew PKG
make
```

---

## Supply Chain & Binary Provenance

- **Bundled Binaries & Provenance**:
  - `sce_module/libSceFios2.prx` & `sce_module/libc.prx`: Inherited verbatim from the upstream repository's initial commit (`c9dbaf6`). While they serve as runtime stub modules for dynamic linking on Orbis OS, their original compiler/SDK toolchain provenance is unrecorded in source control.
  - `sce_sys/about/right.sprx`: Inherited verbatim from upstream's initial commit (`c9dbaf6`) for application metadata/licensing display; its exact build provenance is similarly unrecorded.
- **Release Verification**:
  - Official release packages and binaries are accompanied by `SHA256SUMS.txt`.
  - Checksums can be automatically generated before release using:
    ```bash
    ./scripts/generate_release_checksums.sh <dist_directory>
    ```

---

## Troubleshooting

- **Error: "Found multiple package base names"**:
  - Delete or move unrelated `.pkgpart` files from `/data/pkg_merger`. The app requires a clean single-game directory.
- **Error: "Insufficient disk space"**:
  - Ensure `/data/pkg` has at least 2× the size of the unmerged PKG in free space before starting.
- **Error: "Checksum verification failed"**:
  - The merged file's SHA-256 did not match `Game.manifest.json`. The damaged output is retained at `/data/pkg/<name>.pkg.checksum-failed` for inspection. Re-split or re-transfer the corrupted parts.
- **CE-38603-0 on Package Installation**:
  - Go to `Notifications` $\rightarrow$ `Downloads`, delete the stuck/failed installation entry, and retry installing from GoldHEN Package Installer.

---

## License

This project is licensed under the [GNU General Public License v3.0](./LICENSE).
