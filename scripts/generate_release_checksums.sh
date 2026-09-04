#!/usr/bin/env bash
set -euo pipefail

# Release Checksum Generation Script (Fix #15)
# Generates SHA256SUMS for all build artifacts in a specified directory.

TARGET_DIR="${1:-dist}"
OUTPUT_FILE="${2:-${TARGET_DIR}/SHA256SUMS.txt}"

if [ ! -d "${TARGET_DIR}" ]; then
    echo "[!] Target directory '${TARGET_DIR}' does not exist."
    echo "Usage: $0 [target_directory] [output_file]"
    exit 1
fi

echo "[+] Generating SHA-256 checksums for artifacts in '${TARGET_DIR}'..."

mkdir -p "$(dirname "${OUTPUT_FILE}")"
TARGET_DIR_ABS=$(cd "${TARGET_DIR}" && pwd)
OUTPUT_DIR_ABS=$(cd "$(dirname "${OUTPUT_FILE}")" && pwd)
OUTPUT_FILE_ABS="${OUTPUT_DIR_ABS}/$(basename "${OUTPUT_FILE}")"
temp_sums=$(mktemp)

# Determine sha256 tool
if command -v sha256sum >/dev/null 2>&1; then
    SHA_CMD="sha256sum"
elif command -v shasum >/dev/null 2>&1; then
    SHA_CMD="shasum -a 256"
else
    echo "[-] Error: neither 'sha256sum' nor 'shasum' is installed." >&2
    exit 1
fi

# Find regular files excluding default checksum patterns and the explicit OUTPUT_FILE
find "${TARGET_DIR}" -maxdepth 1 -type f ! -name "SHA256SUMS*" ! -name "*.sha256" | sort | while read -r file; do
    file_abs="$(cd "$(dirname "$file")" && pwd)/$(basename "$file")"
    if [ "${file_abs}" = "${OUTPUT_FILE_ABS}" ]; then
        continue
    fi
    fname=$(basename "$file")
    (cd "${TARGET_DIR}" && ${SHA_CMD} "${fname}") >> "${temp_sums}"
done

if [ ! -s "${temp_sums}" ]; then
    echo "[!] No files found in '${TARGET_DIR}' to hash."
    rm -f "${temp_sums}"
    exit 1
fi

mv "${temp_sums}" "${OUTPUT_FILE}"
echo "[+] Checksums successfully written to '${OUTPUT_FILE}':"
cat "${OUTPUT_FILE}"
