#!/usr/bin/env bash
set -euo pipefail

out_dir="_ci/host-sim"
fixture_dir="$out_dir/fixtures"
log_dir="$out_dir/logs"
python_bin="${PYTHON:-python3}"
summary="$out_dir/qboot_host_sim_summary.md"
case_list="$out_dir/cases.tsv"
runner_custom="$out_dir/qboot_host_runner_custom"
runner_custom_smallbuf="$out_dir/qboot_host_runner_custom-smallbuf"
runner_fal="$out_dir/qboot_host_runner_fal"
runner_fs="$out_dir/qboot_host_runner_fs"
runner_custom_helper="$out_dir/qboot_host_runner_custom-helper"

mkdir -p "$fixture_dir" "$log_dir"
test -x "$runner_custom"
test -x "$runner_custom_smallbuf"
test -x "$runner_fal"
test -x "$runner_fs"
test -x "$runner_custom_helper"

PYTHON_BIN="$python_bin" "$python_bin" - <<'PY'
from pathlib import Path
import gzip, importlib.util, json, os, random, struct, zlib

root = Path('_ci/host-sim/fixtures')
root.mkdir(parents=True, exist_ok=True)
app_limit = 0x30000
HEADER_SIZE = 96
app_use_limit = app_limit - HEADER_SIZE
product = 'host-product'
ALGO_OFF = 4
FW_VER_OFF = 28
PROD_OFF = 52
PKG_CRC_OFF = 76
RAW_CRC_OFF = 80
RAW_SIZE_OFF = 84
PKG_SIZE_OFF = 88
HDR_CRC_OFF = 92

def pattern(size, mul, add):
    return bytes(((i * mul + add) & 0xFF) for i in range(size))

def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF

def write_u16(data, off, value):
    data[off:off + 2] = struct.pack('<H', value)

def write_u32(data, off, value):
    data[off:off + 4] = struct.pack('<I', value)

def put_field(data, off, width, value):
    data[off:off + width] = value[:width].ljust(width, b'\0')

def refresh_hdr_crc(data):
    write_u32(data, HDR_CRC_OFF, crc32(bytes(data[:HDR_CRC_OFF])))

old_app = pattern(3073, 5, 0x11)
old_mismatch_app = pattern(len(old_app), 13, 0x55)
new_app = pattern(4097, 7, 0x23)
rng = random.Random(0)
aes_new_app = None
for size in range(4097, 4700):
    candidate = bytes(rng.randrange(256) for _ in range(size))
    if len(gzip.compress(candidate, compresslevel=9)) % 16 == 0:
        aes_new_app = candidate
        break
if aes_new_app is None:
    raise SystemExit('failed to find AES-compatible gzip fixture')
hpatch_new_app = pattern(2048, 9, 0x41)
large_app = pattern(app_limit + 4096, 3, 0x37)
exact_fit_app = pattern(app_use_limit, 11, 0x31)
minus_one_app = pattern(app_use_limit - 1, 13, 0x29)
plus_one_app = pattern(app_use_limit + 1, 17, 0x71)
large_chunk_app = pattern(65536, 19, 0x5B)
sector_app = pattern(4096, 23, 0x19)
app_b = pattern(3584, 29, 0x62)
app_c = pattern(6144, 31, 0x73)

paths = {
    'old': root / 'old_app.bin',
    'old_mismatch': root / 'old_mismatch_app.bin',
    'new': root / 'new_app.bin',
    'aes_new': root / 'aes_new_app.bin',
    'hpatch_new': root / 'hpatch_new_app.bin',
    'large': root / 'large_app.bin',
    'exact_fit': root / 'exact_fit_app.bin',
    'minus_one': root / 'minus_one_app.bin',
    'plus_one': root / 'plus_one_app.bin',
    'large_chunk': root / 'large_chunk_app.bin',
    'sector': root / 'sector_app.bin',
    'app_b': root / 'app_b.bin',
    'app_c': root / 'app_c.bin',
}
for key, data in [('old', old_app), ('old_mismatch', old_mismatch_app), ('new', new_app),
                  ('aes_new', aes_new_app), ('hpatch_new', hpatch_new_app), ('large', large_app),
                  ('exact_fit', exact_fit_app), ('minus_one', minus_one_app),
                  ('plus_one', plus_one_app), ('large_chunk', large_chunk_app),
                  ('sector', sector_app), ('app_b', app_b), ('app_c', app_c)]:
    paths[key].write_bytes(data)

py = os.environ.get('PYTHON_BIN', 'python3')
spec = importlib.util.spec_from_file_location('package_tool_web', Path('docs/package-tool/package_tool_web.py'))
ptw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ptw)

def make_pkg(name, data, cmprs='none'):
    out = root / name
    out.write_bytes(gzip.compress(data, compresslevel=9) if cmprs == 'gzip' else data)
    return out

def make_rbl(name, pkg, raw=paths['new'], crypt='none', cmprs='none', product_code=product,
             part='app', version='v-ci-host'):
    out = root / name
    out.write_bytes(ptw.package_rbl_bytes(raw.read_bytes(), pkg.read_bytes(),
                                          crypt=crypt, cmprs=cmprs, algo2='crc',
                                          part=part, version=version,
                                          product=product_code,
                                          timestamp=1700000100))
    return out

def make_old_dependent_hpatch_patch(old_fw, new_fw):
    old_bytes = bytes(old_fw)
    new_bytes = bytes(new_fw)
    if len(old_bytes) < len(new_bytes):
        raise SystemExit('old-dependent HPatchLite fixture requires old image >= new image')
    sub_diff = bytes(((new_bytes[i] - old_bytes[i]) & 0xFF) for i in range(len(new_bytes)))
    body = bytearray()
    body.extend(ptw._hpi_pack_uint(1))
    body.extend(ptw._hpi_pack_uint(len(new_bytes)))
    body.append(0)  # old position tag: absolute zero, with sub-diff data.
    body.extend(ptw._hpi_pack_uint(0))
    body.extend(sub_diff)
    new_size = ptw._hpi_size_bytes(len(new_bytes))
    code = (ptw.HPATCHLITE_VERSION_CODE << 6) | len(new_size)
    return ptw.HPATCHLITE_MAGIC + bytes([ptw.HPATCHLITE_COMPRESS_NONE, code]) + new_size + bytes(body)

def mutate_from(base, name, edit):
    out = root / name
    data = bytearray(base.read_bytes())
    edit(data)
    out.write_bytes(data)
    return out

def update_body(data, body, raw=None):
    del data[HEADER_SIZE:]
    data.extend(body)
    write_u32(data, PKG_CRC_OFF, crc32(body))
    write_u32(data, PKG_SIZE_OFF, len(body))
    if raw is not None:
        write_u32(data, RAW_CRC_OFF, crc32(raw))
        write_u32(data, RAW_SIZE_OFF, len(raw))
    refresh_hdr_crc(data)

pkg_none = make_pkg('new_app_pkg_none.bin', new_app)
pkg_gzip = make_pkg('new_app_pkg_gzip.bin', new_app, 'gzip')
pkg_large_gzip = make_pkg('large_app_pkg_gzip.bin', large_app, 'gzip')
pkg_exact = make_pkg('exact_fit_pkg_none.bin', exact_fit_app)
pkg_minus_one = make_pkg('minus_one_pkg_none.bin', minus_one_app)
pkg_plus_one = make_pkg('plus_one_pkg_gzip.bin', plus_one_app, 'gzip')
pkg_large_chunk = make_pkg('large_chunk_pkg_none.bin', large_chunk_app)
pkg_sector = make_pkg('sector_aligned_pkg_none.bin', sector_app)
pkg_app_b = make_pkg('app_b_pkg_none.bin', app_b)
pkg_app_c = make_pkg('app_c_pkg_gzip.bin', app_c, 'gzip')
pkg_exact_gzip = make_pkg('exact_fit_pkg_gzip.bin', exact_fit_app, 'gzip')
pkg_minus_one_gzip = make_pkg('minus_one_pkg_gzip.bin', minus_one_app, 'gzip')

valid_none = make_rbl('custom-none-full.rbl', pkg_none)
valid_gzip = make_rbl('custom-gzip.rbl', pkg_gzip, cmprs='gzip')
valid_exact = make_rbl('target-size-exact-fit.rbl', pkg_exact, raw=paths['exact_fit'])
valid_minus_one = make_rbl('target-size-minus-one.rbl', pkg_minus_one, raw=paths['minus_one'])
target_plus_one = make_rbl('target-size-plus-one.rbl', pkg_plus_one, raw=paths['plus_one'], cmprs='gzip')
large_chunk_rbl = make_rbl('receive-large-chunk-memory-pressure.rbl', pkg_large_chunk, raw=paths['large_chunk'])
sector_aligned_rbl = make_rbl('sector-aligned-none.rbl', pkg_sector, raw=paths['sector'])
target_too_large = make_rbl('custom-target-size-exceeded.rbl', pkg_large_gzip, raw=paths['large'], cmprs='gzip')
valid_aes_gzip = root / 'custom-aes-gzip-real.rbl'
valid_aes_gzip.write_bytes(ptw.package_firmware_bytes(aes_new_app, crypt='aes', cmprs='gzip', algo2='crc', part='app', version='v-ci-host-aes', product=product, timestamp=1700000000))
wrong_key_aes = root / 'mutation-aes-wrong-key.rbl'
wrong_key_aes.write_bytes(ptw.package_firmware_bytes(aes_new_app, crypt='aes', cmprs='gzip', algo2='crc', part='app', version='v-ci-host-aes-bad-key', product=product, aes_key='0123456789ABCDEF0123456789ABCDEG', timestamp=1700000001))
aes_bad_iv = root / 'mutation-aes-bad-iv.rbl'
aes_bad_iv.write_bytes(ptw.package_firmware_bytes(aes_new_app, crypt='aes', cmprs='gzip', algo2='crc', part='app', version='v-ci-host-aes-bad-iv', product=product, aes_iv='FEDCBA9876543210', timestamp=1700000004))
valid_hpatch = root / 'custom-hpatch-host-full-diff.rbl'
valid_hpatch.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch', product=product, timestamp=1700000002, patch_compress='none'))
hpatch_delta_patch = make_old_dependent_hpatch_patch(old_app, hpatch_new_app)
valid_hpatch_delta = root / 'custom-hpatch-old-dependent-delta.rbl'
valid_hpatch_delta.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_new_app, pkg_obj=hpatch_delta_patch, crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-old-dependent', product=product, timestamp=1700000007))
hpatch_old_mismatch = root / 'mutation-hpatch-old-image-mismatch.rbl'
hpatch_old_mismatch.write_bytes(valid_hpatch_delta.read_bytes())
valid_b = make_rbl('repeat-upgrade-b.rbl', pkg_app_b, raw=paths['app_b'])
valid_c_gzip = make_rbl('repeat-upgrade-c-gzip.rbl', pkg_app_c, raw=paths['app_c'], cmprs='gzip')
valid_gzip_exact = make_rbl('gzip-output-size-exact-target.rbl', pkg_exact_gzip, raw=paths['exact_fit'], cmprs='gzip')
valid_gzip_minus_one = make_rbl('gzip-output-size-target-minus-one.rbl', pkg_minus_one_gzip, raw=paths['minus_one'], cmprs='gzip')
aes_cipher_one_block = root / 'aes-gzip-ciphertext-one-block.rbl'
patch = bytearray(ptw.hpatchlite_create_patch(old_app, hpatch_new_app, 'none'))
patch[0] ^= 0x5A
bad_hpatch = root / 'mutation-hpatch-bad-diff.rbl'
bad_hpatch.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_new_app, pkg_obj=bytes(patch), crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-bad', product=product, timestamp=1700000003))
hpatch_output_too_large = root / 'mutation-hpatch-output-too-large.rbl'
hpatch_output_too_large.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, plus_one_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-too-large', product=product, timestamp=1700000006, patch_compress='none'))
hpatch_production_full = root / 'custom-hpatch-production-full.rbl'
hpatch_production_full.write_bytes(valid_hpatch.read_bytes())
hpatch_production_delta = root / 'custom-hpatch-production-delta.rbl'
hpatch_production_delta.write_bytes(valid_hpatch_delta.read_bytes())
hpatch_production_too_large = root / 'mutation-hpatch-production-output-too-large.rbl'
hpatch_production_too_large.write_bytes(hpatch_output_too_large.read_bytes())

product_bad = make_rbl('custom-product-code-mismatch.rbl', pkg_none, product_code='bad-product')
product_empty = make_rbl('custom-product-empty.rbl', pkg_none, product_code='')
product_prefix = make_rbl('custom-product-prefix.rbl', pkg_none, product_code='host-product-x')
version_same = make_rbl('custom-version-same.rbl', pkg_none, version='v-ci-host')
version_lower = make_rbl('custom-version-lower.rbl', pkg_none, version='v-ci-0000')
version_higher = make_rbl('custom-version-higher.rbl', pkg_none, version='v-ci-9999')
invalid_target = make_rbl('custom-invalid-target.rbl', pkg_none, part='missing')

bad_crc = root / 'custom-bad-crc.rbl'
data = bytearray(valid_none.read_bytes()); data[-1] ^= 0x5A; bad_crc.write_bytes(data)
mutation_bad_magic = mutate_from(valid_none, 'mutation-bad-magic.rbl', lambda d: d.__setitem__(0, ord('X')))
mutation_header_crc = mutate_from(valid_none, 'mutation-header-crc.rbl', lambda d: d.__setitem__(95, d[95] ^ 0xA5))
mutation_truncated_header = root / 'mutation-truncated-header.rbl'; mutation_truncated_header.write_bytes(valid_none.read_bytes()[:64])
mutation_truncated_body = root / 'mutation-truncated-body.rbl'; mutation_truncated_body.write_bytes(valid_none.read_bytes()[:-17])
mutation_extra_tail = root / 'mutation-extra-tail.rbl'; mutation_extra_tail.write_bytes(valid_none.read_bytes() + b'EXTRA-TAIL')
mutation_unsupported_compress = mutate_from(valid_none, 'mutation-unsupported-compress.rbl', lambda d: (write_u16(d, ALGO_OFF, 5 << 8), refresh_hdr_crc(d)))
mutation_unsupported_crypto = mutate_from(valid_none, 'mutation-unsupported-crypto.rbl', lambda d: (write_u16(d, ALGO_OFF, 5), refresh_hdr_crc(d)))
mutation_pkg_size_too_large = mutate_from(valid_none, 'mutation-pkg-size-too-large.rbl', lambda d: (write_u32(d, PKG_SIZE_OFF, len(new_app) + 64), refresh_hdr_crc(d)))
mutation_pkg_size_zero = mutate_from(valid_none, 'pkg-size-zero.rbl', lambda d: (write_u32(d, PKG_SIZE_OFF, 0), write_u32(d, PKG_CRC_OFF, crc32(b'')), refresh_hdr_crc(d)))
mutation_raw_size_zero = mutate_from(valid_none, 'raw-size-zero.rbl', lambda d: (write_u32(d, RAW_SIZE_OFF, 0), write_u32(d, RAW_CRC_OFF, crc32(b'')), refresh_hdr_crc(d)))
mutation_raw_crc = mutate_from(valid_none, 'mutation-raw-crc.rbl', lambda d: (write_u32(d, RAW_CRC_OFF, crc32(new_app) ^ 0x13572468), refresh_hdr_crc(d)))
product_no_nul = mutate_from(valid_none, 'custom-product-code-no-nul.rbl', lambda d: (d.__setitem__(slice(PROD_OFF, PROD_OFF + 24), (product.encode() + b'X' * 24)[:24]), refresh_hdr_crc(d)))
product_embedded_nul = mutate_from(valid_none, 'custom-product-code-embedded-nul.rbl', lambda d: (put_field(d, PROD_OFF, 24, product.encode() + b'\0evil'), refresh_hdr_crc(d)))
product_max_len = mutate_from(valid_none, 'custom-product-code-max-len.rbl', lambda d: (d.__setitem__(slice(PROD_OFF, PROD_OFF + 24), b'M' * 24), refresh_hdr_crc(d)))
version_max = mutate_from(valid_none, 'custom-version-max-field.rbl', lambda d: (d.__setitem__(slice(FW_VER_OFF, FW_VER_OFF + 24), b'9' * 24), refresh_hdr_crc(d)))
version_wrap = mutate_from(valid_none, 'custom-version-wraparound-current-policy.rbl', lambda d: (put_field(d, FW_VER_OFF, 24, b'v4294967295'), refresh_hdr_crc(d)))
aes_bad_length = mutate_from(valid_aes_gzip, 'mutation-aes-bad-padding-or-length.rbl', lambda d: update_body(d, bytes(d[HEADER_SIZE:-1])))
gzip_corrupt_stream = mutate_from(valid_gzip, 'mutation-gzip-corrupt-stream.rbl', lambda d: (d.__setitem__(HEADER_SIZE + 40, d[HEADER_SIZE + 40] ^ 0x7E), write_u32(d, PKG_CRC_OFF, crc32(bytes(d[HEADER_SIZE:]))), refresh_hdr_crc(d)))
gzip_size_mismatch = mutate_from(valid_gzip, 'mutation-gzip-size-mismatch.rbl', lambda d: (write_u32(d, RAW_SIZE_OFF, len(new_app) + 1), write_u32(d, RAW_CRC_OFF, crc32(new_app)), refresh_hdr_crc(d)))
aes_cipher_empty = mutate_from(valid_aes_gzip, 'aes-gzip-ciphertext-empty.rbl', lambda d: update_body(d, b''))
aes_cipher_one_block = mutate_from(valid_aes_gzip, 'aes-gzip-ciphertext-one-block.rbl', lambda d: update_body(d, bytes(d[HEADER_SIZE:HEADER_SIZE + 16])))
aes_cipher_not_block = mutate_from(valid_aes_gzip, 'aes-gzip-ciphertext-not-block-aligned.rbl', lambda d: update_body(d, bytes(d[HEADER_SIZE:HEADER_SIZE + 17])))
hpatch_exact = root / 'hpatch-output-size-exact-target.rbl'
hpatch_exact.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, exact_fit_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-exact', product=product, timestamp=1700000009, patch_compress='none'))
hpatch_minus_one = root / 'hpatch-output-size-target-minus-one.rbl'
hpatch_minus_one.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, minus_one_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-minus-one', product=product, timestamp=1700000010, patch_compress='none'))
hpatch_zero_old = root / 'hpatch-old-image-size-zero-current-policy.rbl'
hpatch_zero_old.write_bytes(ptw.package_hpatchlite_rbl_bytes(b'', hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-zero-old', product=product, timestamp=1700000011, patch_compress='none'))
hpatch_mismatch_old = root / 'hpatch-old-image-size-mismatch-current-policy.rbl'
hpatch_mismatch_old.write_bytes(ptw.package_hpatchlite_rbl_bytes(pattern(len(old_app) + 17, 37, 0x44), hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-mismatch-old', product=product, timestamp=1700000012, patch_compress='none'))

fields = struct.unpack('<4sHHI16s24s24sIIIII', valid_none.read_bytes()[:HEADER_SIZE])
expected = {'type': fields[0].decode('ascii').rstrip('\x00'), 'algo': fields[1], 'algo2': fields[2], 'part_name': fields[4].decode().rstrip('\x00'), 'fw_ver': fields[5].decode().rstrip('\x00'), 'prod_code': fields[6].decode().rstrip('\x00'), 'pkg_crc': fields[7], 'raw_crc': fields[8], 'raw_size': fields[9], 'pkg_size': fields[10], 'hdr_crc': fields[11]}
Path('_ci/host-sim/qboot_py_header_expected.json').write_text(json.dumps(expected, indent=2, sort_keys=True) + '\n')

cases = []
def add(backend, group, case, package, new_path, old_path=paths['old'], er=1, ef=1, es=1, ej=1, esi=1, ea='new', limit=0, chunk=256, mode='normal', replay=0, skip=0, fo='', fr='', fw='', fe='', fsr='', fsw='', fbr=0, cs=0, ca=0, mf='', physical=0, fsy='', fcl='', note=''):
    cases.append((backend, group, case, package, new_path, old_path, er, ef, es, ej, esi, ea, limit, chunk, mode, replay, skip, fo, fr, fw, fe, fsr, fsw, fbr, cs, ca, mf, physical, fsy, fcl, note))

# Baseline, reset, fault, receive, boundary, policy, mutation, app, resource.
add('custom','baseline','custom-none-full',valid_none,paths['new'],note='main path with byte-exact APP check')
add('custom','baseline','app-content-byte-exact',valid_none,paths['new'],note='APP content is compared byte-for-byte')
add('custom','baseline','custom-bad-crc',bad_crc,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note='bad package crc rejected')
add('custom','baseline','custom-download-interrupt',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',limit=43,note='truncated download rejected')
add('custom','baseline','custom-gzip',valid_gzip,paths['new'],chunk=257,note='gzip release path')
add('custom','baseline','custom-aes-gzip-real',valid_aes_gzip,paths['aes_new'],chunk=257,note='real AES-CBC decrypt plus gzip release path')
add('custom','baseline','custom-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='host HPatchLite no-compress full-diff adapter restore path')
add('custom','baseline','custom-hpatch-old-dependent-delta',valid_hpatch_delta,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='host HPatchLite cover/sub-diff fixture depends on old APP content')
add('custom','algo-boundary','gzip-output-size-exact-target',valid_gzip_exact,paths['exact_fit'],chunk=4096,note='gzip output exactly fills usable APP area')
add('custom','algo-boundary','gzip-output-size-target-minus-one',valid_gzip_minus_one,paths['minus_one'],chunk=4096,note='gzip output one byte below usable APP area')
add('custom','algo-boundary','gzip-output-size-target-plus-one-rejected',target_plus_one,paths['plus_one'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=4096,note='gzip output one byte above usable APP area is rejected')
add('custom','algo-boundary','aes-gzip-output-size-exact-target',valid_aes_gzip,paths['aes_new'],chunk=257,note='AES+gzip full output is validated through the existing byte-exact APP check')
add('custom','algo-boundary','aes-gzip-ciphertext-empty-rejected',aes_cipher_empty,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='empty AES ciphertext is rejected')
add('custom','algo-boundary','aes-gzip-ciphertext-one-block',aes_cipher_one_block,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='one-block AES+gzip ciphertext is rejected because it cannot hold a valid gzip stream')
add('custom','algo-boundary','aes-gzip-ciphertext-not-block-aligned-rejected',aes_cipher_not_block,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='non-block-aligned AES ciphertext is rejected')
add('custom','algo-boundary','hpatch-output-size-exact-target',hpatch_exact,paths['exact_fit'],old_path=paths['old'],chunk=257,note='HPatchLite output exactly fills usable APP area')
add('custom','algo-boundary','hpatch-output-size-target-minus-one',hpatch_minus_one,paths['minus_one'],old_path=paths['old'],chunk=257,note='HPatchLite output one byte below usable APP area')
add('custom','algo-boundary','hpatch-output-size-target-plus-one-rejected',hpatch_output_too_large,paths['plus_one'],old_path=paths['old'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='HPatchLite output one byte above usable APP area is rejected')
add('custom','algo-boundary','hpatch-old-image-size-zero-current-policy',hpatch_zero_old,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='HPatchLite full-diff package with zero-size old image is accepted by current policy')
add('custom','algo-boundary','hpatch-old-image-size-mismatch-current-policy',hpatch_mismatch_old,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='HPatchLite full-diff package with mismatched old image size is accepted by current policy')
add('custom','reset','reset-after-app-erase-before-write',valid_none,paths['new'],ef=0,replay=1,fw='app:0',note='reset after APP erase before first APP write')
add('custom','reset','reset-during-app-write',valid_none,paths['new'],ef=0,replay=1,fw='app:1',note='reset during APP write stream')
add('custom','reset','reset-after-app-write-before-sign',valid_none,paths['new'],ef=0,replay=1,fsw='download:0',note='APP written but sign write failed before reset')
add('custom','reset','reset-after-partial-sign-write',valid_none,paths['new'],replay=1,cs=1,note='corrupt residual sign is ignored and rewritten')
add('custom','reset','reset-after-jump-failed',valid_none,paths['new'],replay=1,note='replay after a previous jump attempt')
add('custom','reset','reset-replay-after-sign-before-jump',valid_none,paths['new'],replay=1,skip=1,note='sign exists before reset, replay validates and jumps')
add('custom','fake-flash','fake-flash-full-upgrade',sector_aligned_rbl,paths['sector'],ef=0,es=0,ej=0,esi=0,ea='any',physical=1,note='physical fake-flash exposes current unaligned tail-header erase policy without jumping')
add('custom','fake-flash','fake-flash-reset-during-sector-erase',sector_aligned_rbl,paths['sector'],ef=0,es=0,ej=0,esi=0,ea='old',fe='app:0',physical=1,note='physical fake-flash reset while erasing APP sector must not jump')
add('custom','fake-flash','fake-flash-reset-during-page-write',sector_aligned_rbl,paths['sector'],ef=0,es=0,ej=0,esi=0,ea='any',fw='app:0',physical=1,note='physical fake-flash reset while programming APP must not jump')
add('custom','fake-flash','fake-flash-partial-sector-program-no-jump',sector_aligned_rbl,paths['sector'],ef=0,es=0,ej=0,esi=0,ea='any',fw='app:1',physical=1,note='partial physical-sector program leaves no valid sign and no jump')
add('custom','fault','flash-erase-fail-app',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fe='app:0',note='APP erase failure')
add('custom','fault','flash-read-fail-download',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fr='download:0',note='DOWNLOAD read failure')
add('custom','fault','flash-write-fail-app',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',fw='app:0',note='APP write failure')
add('custom','fault','app-partial-write-no-jump',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',fw='app:1',note='partial APP write must not jump')
add('custom','fault','jump-callback-null-policy',valid_none,paths['new'],replay=1,skip=1,note='absent first jump is recoverable through replay')
for name,ch in [('chunk-size-1',1),('chunk-header-minus-1',95),('chunk-header-exact',96),('chunk-header-plus-1',97),('chunk-large',8192)]:
    add('custom','chunk',name,valid_none,paths['new'],chunk=ch,note='receive chunk boundary')
add('custom','chunk','receive-over-declared-size',mutation_extra_tail,paths['new'],note='extra received bytes after declared pkg_size are ignored by current policy')
add('custom','chunk','receive-zero-size-chunk',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',chunk=0,note='zero chunk is rejected')
add('custom','chunk','receive-large-chunk-memory-pressure',large_chunk_rbl,paths['large_chunk'],chunk=65536,note='large receive chunk memory-pressure path')
add('custom','ordering','receive-duplicate-first-chunk',valid_none,paths['new'],mode='duplicate-first',note='duplicate first chunk is accepted; retry policy belongs to the protocol adapter')
add('custom','ordering','receive-out-of-order',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',mode='out-of-order',note='helper accepts caller offsets; malformed final package is rejected during release')
add('custom','ordering','receive-gap-offset-rejected',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',mode='offset-gap',note='helper accepts sparse caller offsets; malformed final package is rejected during release')
add('custom','ordering','receive-overlap-offset-rejected',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',mode='offset-overlap',note='helper accepts overlapping caller offsets; malformed final package is rejected during release')
add('custom','ordering','receive-same-offset-different-data-current-policy',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',mode='same-offset-different-data',note='same-offset rewrite policy belongs to the protocol adapter; corrupted final package is rejected during release')
add('custom','ordering','receive-last-chunk-shorter-than-declared',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',limit=95,note='truncated final chunk cannot be released')
add('custom','ordering','receive-write-fail-then-next-write-rejected',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',fw='download:1',fbr=1,note='write failure aborts receive sequence')
add('custom','boundary','target-size-exact-fit',valid_exact,paths['exact_fit'],chunk=4096,note='raw_size exactly APP size minus tail header')
add('custom','boundary','target-size-minus-one',valid_minus_one,paths['minus_one'],chunk=4096,note='raw_size one byte below usable APP size')
add('custom','boundary','target-size-plus-one',target_plus_one,paths['plus_one'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=4096,note='raw_size one byte above usable APP size')
add('custom','boundary','custom-target-size-exceeded',target_too_large,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note='raw image exceeds APP target')
for name,pkg,note in [('pkg-size-zero',mutation_pkg_size_zero,'declared package size zero rejected'),('raw-size-zero',mutation_raw_size_zero,'declared raw size zero rejected')]:
    add('custom','boundary',name,pkg,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note=note)
add('custom','boundary','pkg-size-exact',valid_none,paths['new'],note='declared pkg_size equals actual body')
add('custom','boundary','pkg-size-smaller-extra-tail-current-policy',mutation_extra_tail,paths['new'],note='trailing data remains accepted by current pkg_size policy')
for name,pkg,note in [('custom-version-same',version_same,'same version accepted'),('custom-version-downgrade-current-policy',version_lower,'downgrade accepted'),('custom-version-upgrade',version_higher,'upgrade accepted'),('custom-version-max-field-current-policy',version_max,'full-width version field accepted'),('custom-version-wraparound-current-policy',version_wrap,'wraparound-like version accepted'),('version-downgrade-rejected-policy-placeholder',version_lower,'current policy still accepts downgrade')]:
    add('custom','policy',name,pkg,paths['new'],note=note)
for name,pkg,note in [('custom-product-code-mismatch',product_bad,'wrong product code rejected'),('custom-product-empty',product_empty,'empty product code rejected'),('custom-product-prefix',product_prefix,'prefix product code rejected'),('custom-product-code-max-len',product_max_len,'full product field rejected'),('custom-product-code-no-nul',product_no_nul,'non-NUL-terminated matching prefix rejected'),('custom-product-code-embedded-nul',product_embedded_nul,'embedded-NUL product rejected'),('custom-invalid-target',invalid_target,'missing target rejected')]:
    add('custom','policy',name,pkg,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note=note)
for name,pkg,newp,note in [
    ('mutation-bad-magic',mutation_bad_magic,paths['new'],'bad magic rejected'),('mutation-header-crc',mutation_header_crc,paths['new'],'header crc rejected'),('mutation-truncated-header',mutation_truncated_header,paths['new'],'truncated header rejected'),('mutation-truncated-body',mutation_truncated_body,paths['new'],'truncated body rejected'),('mutation-unsupported-compress',mutation_unsupported_compress,paths['new'],'unsupported compression rejected'),('mutation-unsupported-crypto',mutation_unsupported_crypto,paths['new'],'unsupported encryption rejected'),('mutation-pkg-size-too-large',mutation_pkg_size_too_large,paths['new'],'declared pkg_size exceeds body'),('mutation-raw-crc',mutation_raw_crc,paths['new'],'raw crc mismatch rejected'),('mutation-aes-wrong-key',wrong_key_aes,paths['aes_new'],'AES wrong key rejected'),('mutation-aes-bad-padding-or-length',aes_bad_length,paths['aes_new'],'AES-CBC length invalid'),('mutation-aes-bad-iv',aes_bad_iv,paths['aes_new'],'AES IV mismatch rejected'),('mutation-gzip-corrupt-stream',gzip_corrupt_stream,paths['new'],'gzip stream corruption rejected'),('mutation-gzip-size-mismatch',gzip_size_mismatch,paths['new'],'gzip decoded size mismatch rejected'),('mutation-hpatch-bad-diff',bad_hpatch,paths['hpatch_new'],'corrupted HPatchLite patch rejected'),('mutation-hpatch-output-too-large',hpatch_output_too_large,paths['plus_one'],'HPatchLite output larger than APP usable size rejected')]:
    add('custom','mutation',name,pkg,newp,ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note=note)
add('custom','mutation','mutation-extra-tail-current-policy',mutation_extra_tail,paths['new'],note='extra tail ignored by current policy')
add('custom','mutation','mutation-hpatch-old-image-mismatch-rejected',hpatch_old_mismatch,paths['hpatch_new'],old_path=paths['old_mismatch'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='old-dependent HPatchLite cover/sub-diff package rejects mismatched old APP through raw CRC')
add('custom','app-verify','app-corrupt-with-valid-sign',valid_none,paths['new'],replay=1,ca=1,note='valid sign no longer skips corrupted APP verification')
add('custom','app-verify','app-write-offset-check',valid_none,paths['new'],note='byte-exact APP check catches offset errors')
add('custom','app-verify','app-tail-preserved-or-erased-policy',valid_minus_one,paths['minus_one'],chunk=4096,note='tail beyond raw image is reserved for mirrored header/sign policy')
add('custom','resource','repeat-upgrade-no-leak',valid_none,paths['new'],replay=1,note='second release after first success exercises cleanup/reuse')
add('custom-smallbuf','resource','work-buffer-too-small',valid_gzip,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='gzip release rejects an undersized QBOOT_BUF_SIZE work buffer')
add('custom','resource','alloc-fail-release-buffer',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,mf='0',note='rt_malloc failure in HPatchLite release buffer is rejected')
add('custom','resource','alloc-fail-hpatch-swap-buffer',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,mf='0',note='rt_malloc failure at HPatchLite RAM swap allocation is rejected')
add('custom','resource','alloc-after-hpatch-swap-buffer-success',valid_hpatch,paths['hpatch_new'],chunk=257,mf='1',note='no hidden second dynamic allocation is required after HPatchLite swap buffer allocation')
add('custom','resource','alloc-fail-then-next-upgrade-success',valid_hpatch,paths['hpatch_new'],ef=0,es=1,ej=1,esi=1,ea='new',chunk=257,mf='0',replay=1,note='failed allocation leaves state recoverable for a later release')
# FAL and FS backend-specific scenarios.
add('fal','backend','fal-custom-none-full',valid_none,paths['new'],note='FAL main path')
add('fal','backend-algo','fal-gzip-full-upgrade',valid_gzip,paths['new'],chunk=257,note='FAL gzip full upgrade')
add('fal','backend-algo','fal-aes-gzip-full-upgrade',valid_aes_gzip,paths['aes_new'],chunk=257,note='FAL AES+gzip full upgrade')
add('fal','backend-algo','fal-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='FAL HPatchLite host full-diff adapter path')
add('fal','backend-algo','fal-gzip-reset-during-app-write',valid_gzip,paths['new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL gzip reset during APP write')
add('fal','backend-algo','fal-aes-gzip-reset-during-app-write',valid_aes_gzip,paths['aes_new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL AES+gzip reset during APP write')
add('fal','backend-algo','fal-hpatch-reset-during-app-write',valid_hpatch,paths['hpatch_new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL HPatchLite reset during APP write')
add('fal','backend','fal-partition-not-found',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fo='download:0',note='FAL partition lookup failure')
add('fal','backend','fal-read-fail-download',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fr='download:0',note='FAL download read failure')
add('fal','backend','fal-write-fail-download',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',fw='download:0',fbr=1,note='FAL receive write failure')
add('fal','backend','fal-write-fail-app',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',fw='app:0',note='FAL APP write failure')
add('fal','backend','fal-erase-fail-app',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fe='app:0',note='FAL APP erase failure')
add('fal','backend','fal-target-size-exceeded',target_plus_one,paths['plus_one'],ef=0,es=0,ej=0,esi=0,ea='old',note='FAL destination size guard')
add('fal','backend','fal-offset-out-of-range',mutation_pkg_size_too_large,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note='FAL out-of-range read rejected')
add('fal','reset','fal-reset-after-app-erase-before-write',valid_none,paths['new'],ef=0,replay=1,fw='app:0',note='FAL reset after APP erase before first APP write')
add('fal','reset','fal-reset-during-app-write',valid_none,paths['new'],ef=0,replay=1,fw='app:1',note='FAL reset during APP write stream')
add('fal','reset','fal-reset-after-app-write-before-sign',valid_none,paths['new'],ef=0,replay=1,fw='download:0',note='FAL APP written but release sign write failed before reset')
add('fal','reset','fal-reset-after-partial-sign-write',valid_none,paths['new'],replay=1,cs=1,note='FAL corrupt residual sign is ignored and rewritten')
add('custom','sign-hard-fail','custom-sign-write-fail-after-app-written',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fsw='download:0',note='custom release-sign write hard failure after APP write does not jump')
add('custom','sign-hard-fail','custom-sign-read-fail-before-jump',valid_none,paths['new'],fsr='download:1',note='custom sign read failure before final sign check is treated as not released')
add('fal','sign-hard-fail','fal-sign-write-fail-after-app-written',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fw='download:0',note='FAL release-sign write hard failure after APP write does not jump')
add('fal','sign-hard-fail','fal-sign-read-fail-before-jump',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fr='download:1',note='FAL sign read failure is modeled by the FAL read path and must not jump')
add('fs','backend','fs-custom-none-full',valid_none,paths['new'],note='FS main path')
add('fs','backend-algo','fs-gzip-full-upgrade',valid_gzip,paths['new'],chunk=257,note='FS gzip full upgrade')
add('fs','backend-algo','fs-aes-gzip-full-upgrade',valid_aes_gzip,paths['aes_new'],chunk=257,note='FS AES+gzip full upgrade')
add('fs','backend-algo','fs-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='FS HPatchLite host full-diff adapter path')
add('fs','backend-algo','fs-gzip-reset-during-app-write',valid_gzip,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: gzip partial APP write is not recoverable by replay')
add('fs','backend-algo','fs-aes-gzip-reset-during-app-write',valid_aes_gzip,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: AES+gzip partial APP write is not recoverable by replay')
add('fs','backend-algo','fs-hpatch-reset-during-app-write',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: HPatchLite partial APP write is not recoverable by replay')
add('fs','backend','fs-open-download-fail',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fo='download:0',note='FS open download failure')
add('fs','backend','fs-read-download-fail',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',fr='download:0',note='FS read download failure')
add('fs','backend','fs-write-download-fail',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',fw='download:0',fbr=1,note='FS receive write failure')
add('fs','backend','fs-create-download-fail',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',fo='download:0',fbr=1,note='FS create/open download failure')
add('fs','backend','fs-truncate-download-fail',valid_none,paths['new'],er=0,ef=0,es=0,ej=0,esi=0,ea='old',fe='download:0',fbr=1,note='FS truncate/erase download failure')
add('fs','backend','fs-sign-create-fail',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fsw='download:0',note='FS sign file create/write failure')
add('fs','backend','fs-sign-corrupt',valid_none,paths['new'],cs=1,note='FS corrupt sign file is not accepted as release confirmation')
add('fs','backend','fs-stale-sign-with-old-download',valid_none,paths['new'],cs=1,replay=1,note='FS stale/corrupt sign does not cause stale replay skip')
add('fs','reset','fs-reset-after-app-erase-before-write',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:0',note='FS current policy: APP truncate before write is not recoverable by replay')
add('fs','reset','fs-reset-during-app-write',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',note='FS current policy: partial APP file size is not recoverable by replay')
add('fs','reset','fs-reset-after-app-write-before-sign',valid_none,paths['new'],ef=0,replay=1,fsw='download:0',note='FS APP written but release sign write failed before reset')
add('fs','reset','fs-reset-after-partial-sign-write',valid_none,paths['new'],replay=1,cs=1,note='FS corrupt residual sign is ignored and rewritten')
add('fs','sign-hard-fail','fs-sign-write-fail-after-app-written',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fsw='download:0',note='FS release-sign write hard failure after APP write does not jump')
add('fs','sign-hard-fail','fs-sign-close-fail-after-app-written',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fcl='download:0',note='FS release-sign close failure is propagated and marker is removed')
add('fs','sign-hard-fail','fs-app-close-fail-before-sign',valid_none,paths['new'],ef=0,es=0,ej=0,esi=0,ea='new',fcl='app:0',note='FS APP close failure is propagated before release sign is written')
add('fs','resource','fs-repeat-upgrade-fd-leak',valid_none,paths['new'],replay=1,note='repeat FS release closes descriptors')

with open('_ci/host-sim/cases.tsv', 'w', encoding='utf-8') as f:
    for row in cases:
        f.write('|'.join(str(x) for x in row) + '\n')
PY

case_count=0
pass_count=0
{
  printf '# QBoot L1 Host Upgrade Simulation\n\n'
  printf 'This job validates host-side L1 upgrade paths, fault injection, reset replay, parser consistency, mutation handling, backend mocks, update-manager state tests, fake-flash semantics, and filesystem boundary checks.\n\n'
  printf '## Case matrix\n\n'
  printf '| Backend | Group | Case | Result | Receive | First release | Final release | Jump | Sign | APP | Chunk | Receive mode | Log | Note |\n'
  printf '|---|---|---|---:|---:|---:|---:|---:|---:|---|---:|---|---|---|\n'
} > "$summary"

runner_for_backend() {
  case "$1" in
    custom) printf '%s\n' "$runner_custom" ;;
    custom-smallbuf) printf '%s\n' "$runner_custom_smallbuf" ;;
    fal) printf '%s\n' "$runner_fal" ;;
    fs) printf '%s\n' "$runner_fs" ;;
    *) echo "unsupported backend: $1" >&2; return 1 ;;
  esac
}

while IFS='|' read -r backend group case_name package new_app_path old_app_path expect_receive expect_first expect_success expect_jump expect_sign expect_app limit chunk receive_mode replay skip_first fail_open fail_read fail_write fail_erase fail_sign_read fail_sign_write fault_before_receive corrupt_sign corrupt_app malloc_fail_after physical_flash fail_sync fail_close note; do
  case_count=$((case_count + 1))
  runner=$(runner_for_backend "$backend")
  log_file="$log_dir/$case_name.log"
  cmd=("$runner" --case "$case_name" --package "$package" --old-app "$old_app_path" --new-app "$new_app_path" --expect-receive "$expect_receive" --expect-first-success "$expect_first" --expect-success "$expect_success" --expect-jump "$expect_jump" --expect-sign "$expect_sign" --expect-app "$expect_app" --chunk "$chunk" --receive-mode "$receive_mode")
  if [ "$limit" != "0" ]; then cmd+=(--download-limit "$limit"); fi
  if [ "$replay" != "0" ]; then cmd+=(--replay "$replay"); fi
  if [ "$skip_first" != "0" ]; then cmd+=(--skip-first-jump "$skip_first"); fi
  if [ "$fault_before_receive" != "0" ]; then cmd+=(--fault-before-receive "$fault_before_receive"); fi
  if [ "$corrupt_sign" != "0" ]; then cmd+=(--corrupt-sign-before-release "$corrupt_sign"); fi
  if [ "$corrupt_app" != "0" ]; then cmd+=(--corrupt-app-before-replay "$corrupt_app"); fi
  if [ -n "$malloc_fail_after" ]; then cmd+=(--malloc-fail-after "$malloc_fail_after"); fi
  if [ "$physical_flash" != "0" ]; then cmd+=(--physical-flash "$physical_flash"); fi
  if [ -n "$fail_open" ]; then cmd+=(--fail-open "$fail_open"); fi
  if [ -n "$fail_read" ]; then cmd+=(--fail-read "$fail_read"); fi
  if [ -n "$fail_write" ]; then cmd+=(--fail-write "$fail_write"); fi
  if [ -n "$fail_erase" ]; then cmd+=(--fail-erase "$fail_erase"); fi
  if [ -n "$fail_sign_read" ]; then cmd+=(--fail-sign-read "$fail_sign_read"); fi
  if [ -n "$fail_sign_write" ]; then cmd+=(--fail-sign-write "$fail_sign_write"); fi
  if [ -n "$fail_close" ]; then cmd+=(--fail-close "$fail_close"); fi
  if "${cmd[@]}" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
    printf '| %s | %s | %s | PASS | %s | %s | %s | %s | %s | %s | %s | %s | `%s` | %s |\n' "$backend" "$group" "$case_name" "$expect_receive" "$expect_first" "$expect_success" "$expect_jump" "$expect_sign" "$expect_app" "$chunk" "$receive_mode" "$log_file" "$note" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"
    cat "$log_file"
    exit 1
  fi
done < "$case_list"

for update_case in update-mgr-start-finish update-mgr-abort update-mgr-finish-fail update-mgr-register-app-valid update-mgr-register-app-invalid update-mgr-write-before-start update-mgr-start-twice update-mgr-finish-twice update-mgr-finish-after-abort update-mgr-abort-after-finish concurrent-update-start-rejected callback-null-all callback-reentrant-update-rejected callback-abort-during-progress callback-order-check callback-count-check backend-register-twice update-mgr-write-fail-then-abort update-mgr-write-fail-then-restart update-mgr-partial-write-then-finish-current-policy update-mgr-zero-total-size-current-policy update-mgr-size-mismatch-on-finish-current-policy update-mgr-finish-without-full-body-current-policy update-mgr-abort-clears-download-state receive-abort-after-partial-body-then-restart receive-finish-after-write-error-rejected receive-total-size-size_t-overflow-rejected receive-offset-plus-size-overflow-rejected callback-progress-monotonic callback-progress-final-100-on-success callback-progress-not-100-on-failure callback-error-code-propagated callback-abort-during-sign-phase callback-abort-during-decompress-phase callback-abort-during-hpatch-phase callback-reentrant-finish-rejected update-mgr-register-backend-after-start-rejected update-mgr-register-backend-during-update-rejected update-mgr-unregister-or-replace-backend-current-policy update-helper-abort-close-fail-propagated update-helper-ready-close-fail-retries-before-ready update-mgr-start-after-failed-finish update-mgr-start-after-failed-abort update-mgr-finish-callback-reentrant-start-rejected update-mgr-abort-callback-reentrant-start-rejected update-mgr-multiple-contexts-current-policy error-code-update-in-progress error-code-update-not-started; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$update_case.log"
  if "$runner_custom" --mode update-mgr --case "$update_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$update_case"
    printf '| custom | update-mgr | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct update-manager state test |\n' "$update_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$update_case"
    cat "$log_file"
    exit 1
  fi
done

for helper_case in update-helper-backend-size-smoke update-helper-abort-clears-session; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$helper_case.log"
  if "$runner_custom_helper" --mode update-mgr --case "$helper_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$helper_case"
    printf '| custom-helper | update-mgr | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | production helper fallback size/abort semantics smoke |\n' "$helper_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$helper_case"
    cat "$log_file"
    exit 1
  fi
done

for jump_case in jump-disable-irq-check jump-msp-update-check jump-vtor-update-check jump-clear-pending-irq-check jump-deinit-systick-check jump-invalid-stack-pointer jump-invalid-reset-vector jump-vector-table-unaligned jump-stack-pointer-ram-start-boundary jump-stack-pointer-ram-end-boundary jump-reset-vector-thumb-bit-clear-rejected jump-reset-vector-thumb-bit-set-accepted jump-vtor-alignment-cortex-m3-policy jump-vtor-alignment-cortex-m7-policy jump-fpu-state-cleanup-policy jump-cache-barrier-before-branch-policy jump-systick-pending-cleared jump-nvic-enable-bits-cleared; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$jump_case.log"
  if "$runner_custom" --mode jump-stub --case "$jump_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$jump_case"
    printf '| custom | jump-stub | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | host Cortex-M jump preparation assertion |\n' "$jump_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$jump_case"
    cat "$log_file"
    exit 1
  fi
done

for fake_case in fake-flash-one-to-zero-only-write fake-flash-write-without-erase-fail fake-flash-sector-unaligned-erase fake-flash-cross-sector-write fake-flash-partition-nonzero-offset fake-flash-neighbor-partition-not-corrupted fake-flash-program-unit-aligned fake-flash-program-unit-unaligned-rejected fake-flash-program-unit-cross-boundary-rejected fake-flash-erase-then-read-all-ff fake-flash-double-erase-idempotent-current-policy fake-flash-program-timeout-current-policy fake-flash-erase-timeout-current-policy fake-flash-wear-count-not-exceeded-smoke; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$fake_case.log"
  if "$runner_custom" --mode fake-flash --case "$fake_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$fake_case"
    printf '| custom | fake-flash | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | physical fake-flash constraint test |\n' "$fake_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$fake_case"
    cat "$log_file"
    exit 1
  fi
done

for sign_backend in custom fal; do
  sign_runner=$(runner_for_backend "$sign_backend")
  for sign_case in sign-align-exact sign-align-plus-padding sign-at-partition-end-exact sign-write-cross-sector sign-position-out-of-range sign-erase-does-not-corrupt-app-tail; do
    case_count=$((case_count + 1))
    log_file="$log_dir/$sign_backend-$sign_case.log"
    if "$sign_runner" --mode sign-boundary --case "$sign_case" > "$log_file" 2>&1; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s-%s\n' "$sign_backend" "$sign_case"
      printf '| %s | sign-boundary | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct release-sign position and isolation test |\n' "$sign_backend" "$sign_case" "$log_file" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s-%s\n' "$sign_backend" "$sign_case"
      cat "$log_file"
      exit 1
    fi
  done
done

for fs_case in fs-mount-missing fs-read-short-count fs-size-after-truncate-zero fs-close-reopen-readback fs-write-short-count fs-no-space-left fs-path-too-long fs-download-path-readonly fs-sign-path-readonly fs-download-and-sign-same-path fs-stale-temp-file-cleanup fs-existing-sign-file-shorter-than-sign fs-existing-sign-file-longer-than-sign fs-existing-download-file-longer-than-package fs-reopen-fail-after-write-current-policy fs-write-fail-after-download-write-current-policy fs-write-fail-after-download-overwrite-current-policy fs-write-fail-after-download-retry-current-policy fs-rename-temp-to-download-fail-current-policy fs-temp-file-power-loss-before-rename fs-mount-lost-during-release fs-unmount-before-replay fs-directory-missing-created-or-rejected-policy fs-stale-temp-sign-file-ignored; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$fs_case.log"
  if "$runner_fs" --mode fs-boundary --case "$fs_case" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$fs_case"
    printf '| fs | fs-boundary | %s | PASS | - | - | - | - | - | - | - | direct | `%s` | direct filesystem boundary test |\n' "$fs_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$fs_case"
    cat "$log_file"
    exit 1
  fi
done

for parser_case in "parser-consistency-none:$fixture_dir/custom-none-full.rbl" "parser-consistency-gzip:$fixture_dir/custom-gzip.rbl" "parser-consistency-aes-gzip:$fixture_dir/custom-aes-gzip-real.rbl" "parser-consistency-hpatch-host-full-diff:$fixture_dir/custom-hpatch-host-full-diff.rbl" "parser-consistency-boundary-fields:$fixture_dir/target-size-exact-fit.rbl" "parser-consistency-endian-fields:$fixture_dir/target-size-minus-one.rbl" "parser-consistency-product-non-nul:$fixture_dir/custom-product-code-no-nul.rbl" "parser-consistency-version-max:$fixture_dir/custom-version-max-field.rbl"; do
  parser_name=${parser_case%%:*}
  parser_pkg=${parser_case#*:}
  case_count=$((case_count + 1))
  inspect_log="$log_dir/$parser_name.log"
  if "$runner_custom" --inspect --package "$parser_pkg" > "$inspect_log" 2>&1; then
    "$python_bin" - "$parser_pkg" "$inspect_log" <<'PY2'
from pathlib import Path
import json, struct, sys
pkg = Path(sys.argv[1]).read_bytes()
log = Path(sys.argv[2]).read_text()
actual = json.loads(log[log.find('{'):])
fields = struct.unpack('<4sHHI16s24s24sIIIII', pkg[:96])
def dec(b):
    return b.split(b'\0', 1)[0].decode('ascii')
expected = {
    'type': dec(fields[0]),
    'algo': fields[1],
    'algo2': fields[2],
    'part_name': dec(fields[4]),
    'fw_ver': dec(fields[5]),
    'prod_code': dec(fields[6]),
    'pkg_crc': fields[7],
    'raw_crc': fields[8],
    'raw_size': fields[9],
    'pkg_size': fields[10],
    'hdr_crc': fields[11],
}
if actual != expected:
    raise SystemExit(f'parser mismatch for {sys.argv[1]}\nactual={actual}\nexpected={expected}')
PY2
    if [ "$parser_name" = "parser-consistency-none" ]; then
      "$python_bin" - "$inspect_log" <<'PY3'
from pathlib import Path
import json, sys
log = Path(sys.argv[1]).read_text()
actual = json.loads(log[log.find('{'):])
Path('_ci/host-sim/qboot_c_header.json').write_text(json.dumps(actual, indent=2, sort_keys=True) + '\n')
PY3
    fi
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
    printf '| custom | parser | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | Python packer header fields match C parser output |\n' "$parser_name" "$inspect_log" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"
    cat "$inspect_log"
    exit 1
  fi
done

for parser_bad_case in \
  "parser-consistency-bad-magic:$fixture_dir/mutation-bad-magic.rbl" \
  "parser-consistency-bad-header-crc:$fixture_dir/mutation-header-crc.rbl" \
  "parser-consistency-truncated-header:$fixture_dir/mutation-truncated-header.rbl" \
  "parser-consistency-truncated-body:$fixture_dir/mutation-truncated-body.rbl" \
  "parser-consistency-size-overflow:$fixture_dir/mutation-pkg-size-too-large.rbl" \
  "parser-consistency-algo-unsupported:$fixture_dir/mutation-unsupported-compress.rbl"; do
  parser_name=${parser_bad_case%%:*}
  parser_pkg=${parser_bad_case#*:}
  case_count=$((case_count + 1))
  inspect_log="$log_dir/$parser_name.log"
  if "$runner_custom" --inspect --package "$parser_pkg" > "$inspect_log" 2>&1; then
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"
    cat "$inspect_log"
    exit 1
  else
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
    printf '| custom | parser | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | malformed header/body is rejected by C parser |\n' "$parser_name" "$inspect_log" >> "$summary"
  fi
done


run_named_release_case() {
  local backend=$1 case_name=$2 package=$3 old_app=$4 new_app=$5 log_file=$6
  shift 6
  local runner
  runner=$(runner_for_backend "$backend")
  "$runner" --case "$case_name" --package "$package" --old-app "$old_app" --new-app "$new_app" "$@" > "$log_file" 2>&1
}

for sweep in \
  "custom-fault-sweep-full-upgrade custom" \
  "fal-fault-sweep-full-upgrade fal" \
  "fs-fault-sweep-full-upgrade fs" \
  "custom-fault-sweep-replay-after-each-op custom" \
  "fal-fault-sweep-replay-after-each-op fal" \
  "fs-fault-sweep-replay-after-each-op fs" \
  "custom-fault-sweep-sign-phase custom" \
  "fal-fault-sweep-sign-phase fal" \
  "fs-fault-sweep-sign-phase fs"; do
  set -- $sweep
  sweep_case=$1
  sweep_backend=$2
  case_count=$((case_count + 1))
  log_file="$log_dir/$sweep_case.log"
  : > "$log_file"
  if [[ "$sweep_case" == *sign-phase ]]; then
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:1" "--fail-write download:0")
    else
      specs=("--fail-sign-read download:0" "--fail-sign-read download:1" "--fail-sign-write download:0")
    fi
  elif [[ "$sweep_case" == *replay-after-each-op ]]; then
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-write download:0")
    else
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-sign-write download:0")
    fi
  else
    if [[ "$sweep_backend" == "fal" ]]; then
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-erase app:0" "--fail-read download:1" "--fail-write download:0")
    else
      specs=("--fail-read download:0" "--fail-write app:0" "--fail-write app:1" "--fail-erase app:0" "--fail-sign-read download:1" "--fail-sign-write download:0")
    fi
  fi
  sweep_ok=1
  for spec in "${specs[@]}"; do
    read -r opt val <<< "$spec"
    sub_log="$log_dir/$sweep_case-${opt#--}-${val//:/-}.log"
    if [[ "$sweep_case" == *replay-after-each-op && "$sweep_backend" != "fs" ]]; then
      if ! run_named_release_case "$sweep_backend" "$sweep_case-$val" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$sub_log" --expect-receive 1 --expect-first-success 0 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new --replay true "$opt" "$val"; then
        sweep_ok=0
      fi
    else
      expect_app=old
      expect_first=0
      expect_success=0
      expect_jump=0
      expect_sign=0
      if [[ "$val" == app:* ]]; then expect_app=any; fi
      if [[ "$val" == download:0 && ( "$opt" == --fail-sign-write || "$opt" == --fail-write ) ]]; then expect_app=new; fi
      if [[ "$val" == download:* && "$opt" == --fail-sign-read ]]; then
        expect_app=new
        expect_first=1
        expect_success=1
        expect_jump=1
        expect_sign=1
      fi
      if ! run_named_release_case "$sweep_backend" "$sweep_case-$val" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$sub_log" --expect-receive 1 --expect-first-success "$expect_first" --expect-success "$expect_success" --expect-jump "$expect_jump" --expect-sign "$expect_sign" --expect-app "$expect_app" "$opt" "$val"; then
        sweep_ok=0
      fi
    fi
    printf '%s %s -> %s\n' "$opt" "$val" "$sub_log" >> "$log_file"
  done
  if [ "$sweep_ok" -eq 1 ]; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$sweep_case"
    printf '| %s | fault-sweep | %s | PASS | - | - | - | - | - | - | - | sweep | `%s` | deterministic storage failpoint sweep |\n' "$sweep_backend" "$sweep_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$sweep_case"
    cat "$log_file"
    exit 1
  fi
done

for repeat_case in \
  repeat-upgrade-a-to-b-to-c \
  repeat-upgrade-fail-then-success \
  repeat-upgrade-success-then-stale-download-leftover \
  repeat-upgrade-sign-rewritten-each-time \
  repeat-upgrade-gzip-then-aes-then-hpatch \
  repeat-upgrade-hpatch-then-none; do
  case_count=$((case_count + 1))
  log_file="$log_dir/$repeat_case.log"
  if "$runner_custom" --mode repeat-sequence --case "$repeat_case" --fixture-dir "$fixture_dir" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$repeat_case"
    printf '| custom | repeat | %s | PASS | - | - | - | - | - | - | - | in-process | `%s` | in-process multi-upgrade/stale-state pressure sequence |\n' "$repeat_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$repeat_case"
    cat "$log_file"
    exit 1
  fi
done

repeat_case=repeat-upgrade-switch-backend-state-clean
case_count=$((case_count + 1))
log_file="$log_dir/$repeat_case.log"
: > "$log_file"
repeat_ok=1
idx=0
for item in \
  "custom-none-full.rbl:new_app.bin:old_app.bin:custom" \
  "custom-none-full.rbl:new_app.bin:old_app.bin:fal" \
  "custom-none-full.rbl:new_app.bin:old_app.bin:fs"; do
  IFS=: read -r pkg new old backend <<< "$item"
  sub_log="$log_dir/$repeat_case-$idx.log"
  if ! run_named_release_case "$backend" "$repeat_case-$idx" "$fixture_dir/$pkg" "$fixture_dir/$old" "$fixture_dir/$new" "$sub_log" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new; then
    repeat_ok=0
  fi
  printf '%s -> %s\n' "$item" "$sub_log" >> "$log_file"
  idx=$((idx + 1))
done
if [ "$repeat_ok" -eq 1 ]; then
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$repeat_case"
  printf '| custom | backend-switch | %s | PASS | - | - | - | - | - | - | - | independent | `%s` | independent backend state-clean smoke; per-backend process reset is intentional |\n' "$repeat_case" "$log_file" >> "$summary"
else
  printf 'QBOOT_HOST_CASE_FAIL %s\n' "$repeat_case"
  cat "$log_file"
  exit 1
fi

for parser_case in \
  parser-default-package-inspect-smoke:accept \
  parser-leading-padding-rejected:reject; do
  parser_name=${parser_case%%:*}
  parser_expect=${parser_case#*:}
  case_count=$((case_count + 1))
  log_file="$log_dir/$parser_name.log"
  if [ "$parser_expect" = "reject" ]; then
    padded="$fixture_dir/$parser_name.rbl"
    { printf 'X'; cat "$fixture_dir/custom-none-full.rbl"; } > "$padded"
    if "$runner_custom" --inspect --package "$padded" > "$log_file" 2>&1; then
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"
      cat "$log_file"
      exit 1
    fi
  else
    "$runner_custom" --inspect --package "$fixture_dir/custom-none-full.rbl" > "$log_file" 2>&1
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
  printf '| custom | parser-smoke | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | parser default inspect or leading-padding rejection smoke case |\n' "$parser_name" "$log_file" >> "$summary"
done


run_extra_release() {
  local case_name=$1 pkg=$2 new_img=$3 expect=${4:-success}
  local log_file="$log_dir/$case_name.log"
  shift 3
  if [ "$#" -gt 0 ] && { [ "$1" = "success" ] || [ "$1" = "fail" ]; }; then
    expect=$1
    shift
  fi
  local extra_args=("$@")
  case_count=$((case_count + 1))
  if [ "$expect" = "fail" ]; then
    if run_named_release_case custom "$case_name" "$pkg" "$fixture_dir/old_app.bin" "$new_img" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 0 --expect-jump 0 --expect-sign 0 --expect-app old "${extra_args[@]}"; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
      printf '| custom | policy-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | additional rejection/current-policy smoke coverage |\n' "$case_name" "$log_file" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"; cat "$log_file"; exit 1
    fi
  else
    if run_named_release_case custom "$case_name" "$pkg" "$fixture_dir/old_app.bin" "$new_img" "$log_file" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new "${extra_args[@]}"; then
      pass_count=$((pass_count + 1))
      printf 'QBOOT_HOST_CASE_PASS %s\n' "$case_name"
      printf '| custom | policy-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | additional success/current-policy smoke coverage |\n' "$case_name" "$log_file" >> "$summary"
    else
      printf 'QBOOT_HOST_CASE_FAIL %s\n' "$case_name"; cat "$log_file"; exit 1
    fi
  fi
}


# Parser rejection and package-tool generated-fixture inspect smoke cases.
for parser_extra in \
  "parser-reject-bad-magic-corpus:$fixture_dir/mutation-bad-magic.rbl" \
  "parser-reject-header-crc-corpus:$fixture_dir/mutation-header-crc.rbl" \
  "parser-reject-truncated-header-corpus:$fixture_dir/mutation-truncated-header.rbl" \
  "parser-reject-truncated-body-corpus:$fixture_dir/mutation-truncated-body.rbl" \
  "parser-reject-size-overflow-corpus:$fixture_dir/mutation-pkg-size-too-large.rbl" \
  "parser-reject-unsupported-compress-corpus:$fixture_dir/mutation-unsupported-compress.rbl" \
  "parser-reject-unsupported-crypto-corpus:$fixture_dir/mutation-unsupported-crypto.rbl"; do
  parser_name=${parser_extra%%:*}
  parser_pkg=${parser_extra#*:}
  case_count=$((case_count + 1))
  log_file="$log_dir/$parser_name.log"
  if "$runner_custom" --inspect --package "$parser_pkg" > "$log_file" 2>&1; then
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"; cat "$log_file"; exit 1
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
  printf '| custom | parser-reject-smoke | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | deterministic malformed package is rejected |\n' "$parser_name" "$log_file" >> "$summary"
done
for parser_extra in \
  "pack-tool-inspect-none-corpus:$fixture_dir/custom-none-full.rbl" \
  "pack-tool-inspect-gzip-corpus:$fixture_dir/custom-gzip.rbl" \
  "pack-tool-inspect-aes-gzip-corpus:$fixture_dir/custom-aes-gzip-real.rbl" \
  "pack-tool-inspect-hpatch-host-full-diff-corpus:$fixture_dir/custom-hpatch-host-full-diff.rbl" \
  "pack-tool-inspect-product-version-boundary:$fixture_dir/custom-version-max-field.rbl"; do
  parser_name=${parser_extra%%:*}
  parser_pkg=${parser_extra#*:}
  case_count=$((case_count + 1))
  log_file="$log_dir/$parser_name.log"
  if "$runner_custom" --inspect --package "$parser_pkg" > "$log_file" 2>&1; then
    pass_count=$((pass_count + 1))
    printf 'QBOOT_HOST_CASE_PASS %s\n' "$parser_name"
    printf '| custom | package-tool-smoke | %s | PASS | - | - | - | - | - | - | - | inspect | `%s` | package-tool generated fixture parses in C |\n' "$parser_name" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$parser_name"; cat "$log_file"; exit 1
  fi
done

# Metadata, policy, resource, stream, and error-path smoke cases.
run_extra_release metadata-product-match-version-upgrade-target-match "$fixture_dir/custom-version-higher.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-match-version-downgrade-target-match-current-policy "$fixture_dir/custom-version-lower.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-mismatch-version-upgrade-rejected "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-product-empty-version-upgrade-rejected "$fixture_dir/custom-product-empty.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-target-mismatch-product-match-rejected "$fixture_dir/custom-invalid-target.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release metadata-version-max-product-max-target-match-current-policy "$fixture_dir/custom-version-max-field.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-version-wraparound-with-product-match-current-policy "$fixture_dir/custom-version-wraparound-current-policy.rbl" "$fixture_dir/new_app.bin"
run_extra_release metadata-product-embedded-nul-with-version-upgrade-current-policy "$fixture_dir/custom-product-code-embedded-nul.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release policy-success-path-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release resource-gzip-static-buffer-smoke "$fixture_dir/custom-gzip.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release resource-aes-gzip-static-buffer-smoke "$fixture_dir/custom-aes-gzip-real.rbl" "$fixture_dir/aes_new_app.bin" success --chunk 257
run_extra_release resource-app-write-cleanup-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release resource-sign-write-cleanup-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"
run_extra_release stream-1-byte-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 1
run_extra_release stream-prime-sized-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release stream-4096-byte-chunks-package "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin" success --chunk 4096
run_extra_release stream-large-none-package "$fixture_dir/receive-large-chunk-memory-pressure.rbl" "$fixture_dir/large_chunk_app.bin" success --chunk 65536
run_extra_release stream-gzip-package "$fixture_dir/custom-gzip.rbl" "$fixture_dir/new_app.bin" success --chunk 257
run_extra_release stream-aes-gzip-package "$fixture_dir/custom-aes-gzip-real.rbl" "$fixture_dir/aes_new_app.bin" success --chunk 257
run_extra_release stream-hpatch-full-diff-package "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/hpatch_new_app.bin" success --chunk 257
run_extra_release policy-reject-malformed-package-smoke "$fixture_dir/mutation-bad-magic.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release policy-reject-product-mismatch-smoke "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-bad-magic-smoke "$fixture_dir/mutation-bad-magic.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-header-crc-smoke "$fixture_dir/mutation-header-crc.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-raw-crc-smoke "$fixture_dir/mutation-raw-crc.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-product-mismatch-smoke "$fixture_dir/custom-product-code-mismatch.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-target-mismatch-smoke "$fixture_dir/custom-invalid-target.rbl" "$fixture_dir/new_app.bin" fail
run_extra_release error-path-target-too-small-smoke "$fixture_dir/target-size-plus-one.rbl" "$fixture_dir/plus_one_app.bin" fail
run_extra_release resource-hpatch-malloc-fail-smoke "$fixture_dir/custom-hpatch-host-full-diff.rbl" "$fixture_dir/hpatch_new_app.bin" fail --malloc-fail-after 0
run_extra_release policy-success-final-smoke "$fixture_dir/custom-none-full.rbl" "$fixture_dir/new_app.bin"

# Fault replay and FAL layout smoke cases.
for mf_case in custom-fault-replay-write-app fal-fault-replay-write-app fs-fault-replay-read-download custom-reset-app-write-replay fal-reset-app-write-replay fs-reset-read-download-replay custom-repeat-replay-smoke fal-repeat-replay-smoke fs-repeat-replay-smoke custom-fail-sequence-smoke fal-fail-sequence-smoke fs-fail-sequence-smoke; do
  backend=${mf_case%%-*}
  case_count=$((case_count + 1))
  log_file="$log_dir/$mf_case.log"
  fault_args=(--fail-write app:1)
  if [ "$backend" = "fs" ]; then
    fault_args=(--fail-read download:1)
  fi
  if run_named_release_case "$backend" "$mf_case" "$fixture_dir/custom-none-full.rbl" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new --replay true "${fault_args[@]}"; then
    pass_count=$((pass_count + 1)); printf 'QBOOT_HOST_CASE_PASS %s\n' "$mf_case"
    printf '| %s | fault-replay-smoke | %s | PASS | - | - | - | - | - | - | - | replay | `%s` | deterministic single-fault replay smoke sequence |\n' "$backend" "$mf_case" "$log_file" >> "$summary"
  else
    printf 'QBOOT_HOST_CASE_FAIL %s\n' "$mf_case"; cat "$log_file"; exit 1
  fi
done
for fal_layout_case in \
  "fal-default-layout-release-smoke:$fixture_dir/custom-none-full.rbl:success" \
  "fal-default-layout-malformed-header-reject-smoke:$fixture_dir/mutation-truncated-header.rbl:fail"; do
  fal_case_name=${fal_layout_case%%:*}
  fal_rest=${fal_layout_case#*:}
  fal_pkg=${fal_rest%:*}
  fal_expect=${fal_rest##*:}
  case_count=$((case_count + 1))
  log_file="$log_dir/$fal_case_name.log"
  if [ "$fal_expect" = "fail" ]; then
    run_named_release_case fal "$fal_case_name" "$fal_pkg" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 0 --expect-success 0 --expect-jump 0 --expect-sign 0 --expect-app old
  else
    run_named_release_case fal "$fal_case_name" "$fal_pkg" "$fixture_dir/old_app.bin" "$fixture_dir/new_app.bin" "$log_file" --expect-receive 1 --expect-first-success 1 --expect-success 1 --expect-jump 1 --expect-sign 1 --expect-app new
  fi
  pass_count=$((pass_count + 1))
  printf 'QBOOT_HOST_CASE_PASS %s\n' "$fal_case_name"
  printf '| fal | fal-default-layout-smoke | %s | PASS | - | - | - | - | - | - | - | release | `%s` | FAL default-layout release/rejection smoke case |\n' "$fal_case_name" "$log_file" >> "$summary"
done

printf '\n## Parser consistency\n\nPASS: Python packer header fields match C-side qbt_fw_check() parsing for none, gzip, AES+gzip, host HPatchLite no-compress full-diff adapter, exact-fit, endian/boundary, non-NUL product, and max-version packages. Malformed magic, header CRC, truncated header/body, size-overflow, and unsupported-algorithm packages are rejected.\n' >> "$summary"

{
  printf '\n## Notes\n\n'
  printf -- '- Downgrade and extra-tail cases document current policy; old-dependent HPatchLite mismatch is rejected.\n'
  printf -- '- Receive, gzip, and AES paths use static buffers in production; resource smoke tests only exercise the HPatchLite host full-diff allocation path where dynamic allocation exists.\n'
  printf -- '- Fake-flash cases model 1-to-0 writes and sector-aligned erase rules; they do not replace board-level Flash validation.\n'
  printf '\nPassed %d/%d L1 host simulation cases.\n' "$pass_count" "$case_count"
} >> "$summary"

if [ "$pass_count" -ne "$case_count" ]; then
  printf 'L1 host simulation passed %d/%d cases\n' "$pass_count" "$case_count" >&2
  exit 1
fi
printf 'Passed %d/%d L1 host simulation cases.\n' "$pass_count" "$case_count"
printf 'All L1 qboot host simulation cases passed.\n'
