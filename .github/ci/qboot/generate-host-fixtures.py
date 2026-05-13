#!/usr/bin/env python3
"""Generate host simulation fixtures and case list for QBoot CI.

This helper keeps run-host-sim.sh focused on orchestration while preserving the
fixture and case data format consumed by the existing host runner loop.
It must only create product-behavior fixtures and case inputs. Do not add
CI harness self-tests, artifact-presence checks, or generated bookkeeping
comparisons here; those checks make CI validate its own scaffolding instead of
QBoot release behavior.
"""

from pathlib import Path
import importlib.util, os, random, struct, zlib

try:
    fixture_dir_env = os.environ['QBOOT_HOST_FIXTURE_DIR']
    out_dir_env = os.environ['QBOOT_HOST_OUT_DIR']
except KeyError as missing:
    raise SystemExit(
        f'required environment variable {missing.args[0]} is not set; '
        'run via .github/ci/qboot/run-host-sim.sh or export it explicitly'
    )

root = Path(fixture_dir_env)
out_dir = Path(out_dir_env)
root.mkdir(parents=True, exist_ok=True)
out_dir.mkdir(parents=True, exist_ok=True)
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

repo_root = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location(
    'package_tool_web',
    repo_root / 'docs/package-tool/package_tool_web.py',
)
ptw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ptw)

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
    if len(ptw.gzip_compress(candidate)) % 16 == 0:
        aes_new_app = candidate
        break
if aes_new_app is None:
    raise SystemExit('failed to find AES-compatible gzip fixture')
hpatch_new_app = pattern(2048, 9, 0x41)
quickfast_app = pattern(2048, 33, 0x27)

def make_hpatch_multi_cover_new(old_fw):
    old_bytes = bytes(old_fw)
    prefix = pattern(37, 41, 0x21)
    mid = pattern(29, 43, 0x31)
    tail = pattern(45, 47, 0x41)
    cover1 = old_bytes[13:13 + 64]
    cover2_old = old_bytes[84:84 + 80]
    cover2 = bytes((value + 3) & 0xFF for value in cover2_old)
    return prefix + cover1 + mid + cover2 + tail

hpatch_multi_new_app = make_hpatch_multi_cover_new(old_app)
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
    'quickfast': root / 'quickfast_app.bin',
    'hpatch_multi_new': root / 'hpatch_multi_new_app.bin',
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
                  ('aes_new', aes_new_app), ('hpatch_new', hpatch_new_app),
                  ('quickfast', quickfast_app),
                  ('hpatch_multi_new', hpatch_multi_new_app), ('large', large_app),
                  ('exact_fit', exact_fit_app), ('minus_one', minus_one_app),
                  ('plus_one', plus_one_app), ('large_chunk', large_chunk_app),
                  ('sector', sector_app), ('app_b', app_b), ('app_c', app_c)]:
    paths[key].write_bytes(data)

def make_pkg(name, data, cmprs='none'):
    out = root / name
    out.write_bytes(ptw.gzip_compress(data) if cmprs == 'gzip' else data)
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


def wrap_hpatch_body(body, new_len, patch_compress='none'):
    new_size = ptw._hpi_size_bytes(new_len)
    if patch_compress == 'tuz':
        patch_body = ptw.tuz_compress(body)
        uncompress_size = ptw._hpi_size_bytes(len(body))
        code = (ptw.HPATCHLITE_VERSION_CODE << 6) | (len(uncompress_size) << 3) | len(new_size)
        return ptw.HPATCHLITE_MAGIC + bytes([ptw.HPATCHLITE_COMPRESS_TUZ, code]) + new_size + uncompress_size + patch_body
    code = (ptw.HPATCHLITE_VERSION_CODE << 6) | len(new_size)
    return ptw.HPATCHLITE_MAGIC + bytes([ptw.HPATCHLITE_COMPRESS_NONE, code]) + new_size + bytes(body)

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
    return wrap_hpatch_body(bytes(body), len(new_bytes))

def make_multi_cover_hpatch_patch(old_fw, new_fw, patch_compress='none'):
    old_bytes = bytes(old_fw)
    new_bytes = bytes(new_fw)
    prefix = new_bytes[:37]
    mid = new_bytes[101:130]
    tail = new_bytes[210:]
    subdiff = bytes(3 for _ in range(80))
    body = bytearray()
    body.extend(ptw._hpi_pack_uint(3))
    body.extend(ptw._hpi_pack_uint(64))
    body.append(0x80 | 13)  # copy old[13:77] with no sub-diff.
    body.extend(ptw._hpi_pack_uint(len(prefix)))
    body.extend(prefix)
    body.extend(ptw._hpi_pack_uint(80))
    body.append(7)          # old position advances from 77 to 84.
    body.extend(ptw._hpi_pack_uint(len(mid)))
    body.extend(mid)
    body.extend(subdiff)
    body.extend(ptw._hpi_pack_uint(0))
    body.append(0)
    body.extend(ptw._hpi_pack_uint(len(tail)))
    body.extend(tail)
    if ptw.hpatchlite_apply_patch(old_bytes, wrap_hpatch_body(bytes(body), len(new_bytes))) != new_bytes:
        raise SystemExit('generated multi-cover HPatchLite fixture does not round-trip')
    return wrap_hpatch_body(bytes(body), len(new_bytes), patch_compress)

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
valid_quicklz = root / 'custom-quicklz-release.rbl'
valid_quicklz.write_bytes(ptw.package_firmware_bytes(quickfast_app, crypt='none', cmprs='quicklz', algo2='crc', part='app', version='v-ci-quicklz', product=product, timestamp=1700000020))
valid_fastlz = root / 'custom-fastlz-release.rbl'
valid_fastlz.write_bytes(ptw.package_firmware_bytes(quickfast_app, crypt='none', cmprs='fastlz', algo2='crc', part='app', version='v-ci-fastlz', product=product, timestamp=1700000021))
valid_hpatch = root / 'custom-hpatch-host-full-diff.rbl'
valid_hpatch.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch', product=product, timestamp=1700000002, patch_compress='none'))
hpatch_delta_patch = make_old_dependent_hpatch_patch(old_app, hpatch_new_app)
valid_hpatch_delta = root / 'custom-hpatch-old-dependent-delta.rbl'
valid_hpatch_delta.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_new_app, pkg_obj=hpatch_delta_patch, crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-old-dependent', product=product, timestamp=1700000007))
hpatch_old_mismatch = root / 'mutation-hpatch-old-image-mismatch.rbl'
hpatch_old_mismatch.write_bytes(valid_hpatch_delta.read_bytes())
hpatch_raw_crc = mutate_from(valid_hpatch, 'mutation-hpatch-raw-crc.rbl',
                             lambda d: (write_u32(d, RAW_CRC_OFF, crc32(hpatch_new_app) ^ 0x24681357), refresh_hdr_crc(d)))
valid_b = make_rbl('repeat-upgrade-b.rbl', pkg_app_b, raw=paths['app_b'])
valid_c_gzip = make_rbl('repeat-upgrade-c-gzip.rbl', pkg_app_c, raw=paths['app_c'], cmprs='gzip')
valid_gzip_exact = make_rbl('gzip-output-size-exact-target.rbl', pkg_exact_gzip, raw=paths['exact_fit'], cmprs='gzip')
valid_gzip_minus_one = make_rbl('gzip-output-size-target-minus-one.rbl', pkg_minus_one_gzip, raw=paths['minus_one'], cmprs='gzip')
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
hpatch_production_tuz = root / 'custom-hpatch-production-tuz.rbl'
hpatch_production_tuz.write_bytes(ptw.package_hpatchlite_rbl_bytes(old_app, hpatch_new_app, crypt='none', algo2='crc', part='app', version='v-ci-host-hpatch-tuz', product=product, timestamp=1700000008, patch_compress='tuz'))
hpatch_production_tuz_tail = root / 'mutation-hpatch-production-tuz-trailing-byte.rbl'
hpatch_production_tuz_tail_patch = bytearray(ptw.hpatchlite_create_patch(old_app, hpatch_new_app, 'tuz'))
hpatch_production_tuz_tail_patch.append(0xA5)
hpatch_production_tuz_tail.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_new_app, pkg_obj=bytes(hpatch_production_tuz_tail_patch), crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-tuz-tail', product=product, timestamp=1700000013))
hpatch_production_too_large = root / 'mutation-hpatch-production-output-too-large.rbl'
hpatch_production_too_large.write_bytes(hpatch_output_too_large.read_bytes())
hpatch_production_raw_crc = mutate_from(hpatch_production_full, 'mutation-hpatch-production-raw-crc.rbl',
                                        lambda d: (write_u32(d, RAW_CRC_OFF, crc32(hpatch_new_app) ^ 0x24681357), refresh_hdr_crc(d)))
hpatch_multi_patch = make_multi_cover_hpatch_patch(old_app, hpatch_multi_new_app, 'none')
hpatch_multi_tuz_patch = make_multi_cover_hpatch_patch(old_app, hpatch_multi_new_app, 'tuz')
hpatch_production_multi = root / 'custom-hpatch-production-multi-cover.rbl'
hpatch_production_multi.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_multi_new_app, pkg_obj=hpatch_multi_patch, crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-multi', product=product, timestamp=1700000014))
hpatch_production_multi_tuz = root / 'custom-hpatch-production-multi-cover-tuz.rbl'
hpatch_production_multi_tuz.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_multi_new_app, pkg_obj=hpatch_multi_tuz_patch, crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-multi-tuz', product=product, timestamp=1700000015))
hpatch_production_multi_truncated = root / 'mutation-hpatch-production-multi-cover-truncated.rbl'
hpatch_production_multi_truncated.write_bytes(ptw.package_rbl_bytes(raw_fw=hpatch_multi_new_app, pkg_obj=hpatch_multi_patch[:-1], crypt='none', cmprs='hpatchlite', algo2='crc', part='app', version='v-ci-host-hpatch-multi-trunc', product=product, timestamp=1700000016))

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
gzip_truncated_trailer = mutate_from(valid_gzip, 'mutation-gzip-truncated-trailer.rbl', lambda d: update_body(d, bytes(d[HEADER_SIZE:-8])))
gzip_exact_truncated_trailer = mutate_from(valid_gzip_exact, 'mutation-gzip-truncated-trailer-aligned.rbl', lambda d: update_body(d, bytes(d[HEADER_SIZE:-8])))
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
add('custom-hpatch-only','baseline','custom-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='host HPatchLite no-compress full-diff adapter restore path')
add('custom-hpatch-only','baseline','custom-hpatch-old-dependent-delta',valid_hpatch_delta,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='host HPatchLite cover/sub-diff fixture depends on old APP content')
add('custom','algo-boundary','gzip-output-size-exact-target',valid_gzip_exact,paths['exact_fit'],chunk=4096,note='gzip output exactly fills usable APP area')
add('custom','algo-boundary','gzip-output-size-target-minus-one',valid_gzip_minus_one,paths['minus_one'],chunk=4096,note='gzip output one byte below usable APP area')
add('custom','algo-boundary','gzip-output-size-target-plus-one-rejected',target_plus_one,paths['plus_one'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=4096,note='gzip output one byte above usable APP area is rejected')
add('custom','algo-boundary','aes-gzip-output-size-exact-target',valid_aes_gzip,paths['aes_new'],chunk=257,note='AES+gzip full output is validated through the existing byte-exact APP check')
add('custom','algo-boundary','aes-gzip-ciphertext-empty-rejected',aes_cipher_empty,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='empty AES ciphertext is rejected')
add('custom','algo-boundary','aes-gzip-ciphertext-one-block',aes_cipher_one_block,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='one-block AES+gzip ciphertext is rejected because it cannot hold a valid gzip stream')
add('custom','algo-boundary','aes-gzip-ciphertext-not-block-aligned-rejected',aes_cipher_not_block,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='non-block-aligned AES ciphertext is rejected')
add('custom-hpatch-only','algo-boundary','hpatch-output-size-exact-target',hpatch_exact,paths['exact_fit'],old_path=paths['old'],chunk=257,note='HPatchLite output exactly fills usable APP area')
add('custom-hpatch-only','algo-boundary','hpatch-output-size-target-minus-one',hpatch_minus_one,paths['minus_one'],old_path=paths['old'],chunk=257,note='HPatchLite output one byte below usable APP area')
add('custom-hpatch-only','algo-boundary','hpatch-output-size-target-plus-one-rejected',hpatch_output_too_large,paths['plus_one'],old_path=paths['old'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='HPatchLite output one byte above usable APP area is rejected')
add('custom-hpatch-only','algo-boundary','hpatch-old-image-size-zero-current-policy',hpatch_zero_old,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='HPatchLite full-diff package with zero-size old image is accepted by current policy')
add('custom-hpatch-only','algo-boundary','hpatch-old-image-size-mismatch-current-policy',hpatch_mismatch_old,paths['hpatch_new'],old_path=paths['old'],chunk=257,note='HPatchLite full-diff package with mismatched old image size is accepted by current policy')
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
# QBoot validates package integrity, product code, and target partition here;
# it intentionally does not implement anti-rollback/version ordering.
# Product projects that need rollback protection should enforce it above QBoot
# or add a product-specific policy before accepting the release request.
for name,pkg,note in [('custom-version-same',version_same,'same version accepted'),('custom-version-downgrade-current-policy',version_lower,'downgrade accepted by current no-anti-rollback policy'),('custom-version-upgrade',version_higher,'upgrade accepted'),('custom-version-max-field-current-policy',version_max,'full-width version field accepted'),('custom-version-wraparound-current-policy',version_wrap,'wraparound-like version accepted')]:
    add('custom','policy',name,pkg,paths['new'],note=note)
for name,pkg,note in [('custom-product-code-mismatch',product_bad,'wrong product code rejected'),('custom-product-empty',product_empty,'empty product code rejected'),('custom-product-prefix',product_prefix,'prefix product code rejected'),('custom-product-code-max-len',product_max_len,'full product field rejected'),('custom-product-code-no-nul',product_no_nul,'non-NUL-terminated matching prefix rejected'),('custom-product-code-embedded-nul',product_embedded_nul,'embedded-NUL product rejected'),('custom-invalid-target',invalid_target,'missing target rejected')]:
    add('custom','policy',name,pkg,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',note=note)
for name,pkg,newp,note in [
    ('mutation-bad-magic',mutation_bad_magic,paths['new'],'bad magic rejected'),('mutation-header-crc',mutation_header_crc,paths['new'],'header crc rejected'),('mutation-truncated-header',mutation_truncated_header,paths['new'],'truncated header rejected'),('mutation-truncated-body',mutation_truncated_body,paths['new'],'truncated body rejected'),('mutation-unsupported-compress',mutation_unsupported_compress,paths['new'],'unsupported compression rejected'),('mutation-unsupported-crypto',mutation_unsupported_crypto,paths['new'],'unsupported encryption rejected'),('mutation-pkg-size-too-large',mutation_pkg_size_too_large,paths['new'],'declared pkg_size exceeds body'),('mutation-raw-crc',mutation_raw_crc,paths['new'],'raw crc mismatch rejected'),('mutation-aes-wrong-key',wrong_key_aes,paths['aes_new'],'AES wrong key rejected'),('mutation-aes-bad-padding-or-length',aes_bad_length,paths['aes_new'],'AES-CBC length invalid'),('mutation-aes-bad-iv',aes_bad_iv,paths['aes_new'],'AES IV mismatch rejected'),('mutation-gzip-corrupt-stream',gzip_corrupt_stream,paths['new'],'gzip stream corruption rejected'),('mutation-gzip-truncated-trailer',gzip_truncated_trailer,paths['new'],'gzip stream without trailer rejected'),('mutation-gzip-truncated-trailer-aligned',gzip_exact_truncated_trailer,paths['exact_fit'],'aligned gzip stream without trailer rejected'),('mutation-gzip-size-mismatch',gzip_size_mismatch,paths['new'],'gzip decoded size mismatch rejected')]:
    add('custom','mutation',name,pkg,newp,ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note=note)
for name,pkg,newp,note in [
    ('mutation-hpatch-bad-diff',bad_hpatch,paths['hpatch_new'],'corrupted HPatchLite patch rejected'),
    ('mutation-hpatch-output-too-large',hpatch_output_too_large,paths['plus_one'],'HPatchLite output larger than APP usable size rejected')]:
    add('custom-hpatch-only','mutation',name,pkg,newp,ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note=note)
add('custom','mutation','mutation-extra-tail-current-policy',mutation_extra_tail,paths['new'],note='extra tail ignored by current policy')
add('custom-hpatch-only','mutation','mutation-hpatch-old-image-mismatch-rejected',hpatch_old_mismatch,paths['hpatch_new'],old_path=paths['old_mismatch'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='old-dependent HPatchLite cover/sub-diff package rejects mismatched old APP through raw CRC')
add('custom-hpatch-only','mutation','mutation-hpatch-raw-crc-rejected-by-host-adapter',hpatch_raw_crc,paths['hpatch_new'],old_path=paths['old'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='HPatchLite host adapter keeps algo2=crc and rejects a restored image with mismatched raw_crc before APP write')
add('custom','app-verify','app-corrupt-with-valid-sign',valid_none,paths['new'],replay=1,ca=1,note='valid sign no longer skips corrupted APP verification')
add('custom','app-verify','app-write-offset-check',valid_none,paths['new'],note='byte-exact APP check catches offset errors')
add('custom','app-verify','app-tail-preserved-or-erased-policy',valid_minus_one,paths['minus_one'],chunk=4096,note='tail beyond raw image is reserved for mirrored header/sign policy')
add('custom','resource','repeat-upgrade-no-leak',valid_none,paths['new'],replay=1,note='second release after first success exercises cleanup/reuse')
add('custom-smallbuf','resource','work-buffer-too-small',valid_gzip,paths['new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,note='gzip release rejects an undersized QBOOT_BUF_SIZE work buffer')
add('custom-hpatch-only','resource','alloc-fail-release-buffer',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,mf='0',note='rt_malloc failure in HPatchLite release buffer is rejected')
add('custom-hpatch-only','resource','alloc-fail-hpatch-swap-buffer',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='old',chunk=257,mf='0',note='rt_malloc failure at HPatchLite RAM swap allocation is rejected')
add('custom-hpatch-only','resource','alloc-after-hpatch-swap-buffer-success',valid_hpatch,paths['hpatch_new'],chunk=257,mf='1',note='no hidden second dynamic allocation is required after HPatchLite swap buffer allocation')
add('custom-hpatch-only','resource','alloc-fail-then-next-upgrade-success',valid_hpatch,paths['hpatch_new'],ef=0,es=1,ej=1,esi=1,ea='new',chunk=257,mf='0',replay=1,note='failed allocation leaves state recoverable for a later release')
# FAL and FS backend-specific scenarios.
add('fal','backend','fal-custom-none-full',valid_none,paths['new'],note='FAL main path')
add('fal','backend-algo','fal-gzip-full-upgrade',valid_gzip,paths['new'],chunk=257,note='FAL gzip full upgrade')
add('fal','backend-algo','fal-aes-gzip-full-upgrade',valid_aes_gzip,paths['aes_new'],chunk=257,note='FAL AES+gzip full upgrade')
add('fal-hpatch-only','backend-algo','fal-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='FAL HPatchLite host full-diff adapter path')
add('fal','backend-algo','fal-gzip-reset-during-app-write',valid_gzip,paths['new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL gzip reset during APP write')
add('fal','backend-algo','fal-aes-gzip-reset-during-app-write',valid_aes_gzip,paths['aes_new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL AES+gzip reset during APP write')
add('fal-hpatch-only','backend-algo','fal-hpatch-reset-during-app-write',valid_hpatch,paths['hpatch_new'],ef=0,replay=1,fw='app:1',chunk=257,note='FAL HPatchLite reset during APP write')
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
add('fs-hpatch-only','backend-algo','fs-hpatch-host-full-diff',valid_hpatch,paths['hpatch_new'],chunk=257,note='FS HPatchLite host full-diff adapter path')
add('fs','backend-algo','fs-gzip-reset-during-app-write',valid_gzip,paths['new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: gzip partial APP write is not recoverable by replay')
add('fs','backend-algo','fs-aes-gzip-reset-during-app-write',valid_aes_gzip,paths['aes_new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: AES+gzip partial APP write is not recoverable by replay')
add('fs-hpatch-only','backend-algo','fs-hpatch-reset-during-app-write',valid_hpatch,paths['hpatch_new'],ef=0,es=0,ej=0,esi=0,ea='any',replay=1,fw='app:1',chunk=257,note='FS current policy: HPatchLite partial APP write is not recoverable by replay')
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

with open(out_dir / 'cases.tsv', 'w', encoding='utf-8') as f:
    for row in cases:
        f.write('|'.join(str(x) for x in row) + '\n')
