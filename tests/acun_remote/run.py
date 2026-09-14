#!/usr/bin/env python3
"""Host-test the exact C core shipped in the FAP, against recorded and synthetic data.

Fixtures live under tests/acun_remote/data: one directory per recorded set
(two remotes, four buttons), each holding five consecutive presses as Flipper
BinRAW .sub files, plus expected.json with every set's prefix, step and decoded
words. Nothing outside the repository is read.
"""
import ctypes as c
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from itertools import groupby

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DATA = HERE / 'data'
SETS = json.loads((DATA / 'expected.json').read_text())
PRESSES = 5
GAP = 12300  # Inter-frame gap in microseconds; BinRAW blocks omit it.
DECODED_BLOCKS_FLOOR = 45  # of 50 recorded blocks; the rest carry trailing noise.


class Frame(c.Structure):
    _fields_ = [('prefix', c.c_uint32), ('word', c.c_uint16),
                ('suffix', c.c_uint8), ('suffix_count', c.c_uint8),
                ('te', c.c_uint16), ('gap', c.c_uint32)]


class Profile(c.Structure):
    _fields_ = [('frame', Frame), ('step', c.c_uint16), ('accumulator', c.c_uint16),
                ('sends', c.c_uint32), ('generation', c.c_uint32)]


class Decoder(c.Structure):
    _fields_ = [('synchronized', c.c_bool), ('pending_low', c.c_bool),
                ('count', c.c_uint8), ('bits', c.c_uint64), ('units_sum', c.c_uint32)]


TMP = tempfile.TemporaryDirectory(prefix='sequence-core-')
LIBRARY = Path(TMP.name) / 'core.so'
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                '-O2', str(ROOT / 'flipper_apps/acun_remote/sequence_core.c'),
                '-o', str(LIBRARY)], check=True)
lib = c.CDLL(str(LIBRARY))
lib.seq_fit.argtypes = [c.POINTER(Frame), c.POINTER(Profile)]
lib.seq_fit.restype = c.c_bool
lib.seq_advance.argtypes = [c.POINTER(Profile)]
lib.seq_decode.argtypes = [c.POINTER(Decoder), c.c_bool, c.c_uint32, c.POINTER(Frame)]
lib.seq_decode.restype = c.c_bool
lib.seq_pulse.argtypes = [c.POINTER(Frame), c.c_size_t, c.POINTER(c.c_bool), c.POINTER(c.c_uint32)]
lib.seq_pulse.restype = c.c_bool
lib.seq_pack.argtypes = [c.POINTER(Profile), c.POINTER(c.c_uint8)]
lib.seq_unpack.argtypes = [c.POINTER(c.c_uint8), c.POINTER(Profile)]
lib.seq_unpack.restype = c.c_bool
lib.seq_sync.argtypes = [c.POINTER(Profile), c.POINTER(Frame), c.POINTER(c.c_int32)]
lib.seq_sync.restype = c.c_bool


def permute(word):
    """Reference bit permutation, independent of the C core: swap bits 0/13 and 5/8."""
    for a, b in ((0, 13), (5, 8)):
        if ((word >> a) ^ (word >> b)) & 1:
            word ^= (1 << a) | (1 << b)
    return word


def binraw_blocks(path):
    """Yield (data, bit_count, te) for every BinRAW block in a Flipper .sub file."""
    fields = {}
    for line in path.read_text().splitlines():
        if ': ' not in line:
            continue
        key, value = line.split(': ', 1)
        fields[key] = value
        if key == 'Data_RAW' and fields.get('Protocol') == 'BinRAW':
            yield bytes.fromhex(value), int(fields['Bit_RAW']), int(fields['TE'])


def recordings(name):
    return sorted((DATA / name).glob('press_*.sub'))


def words(name):
    return [int(w, 16) for w in SETS[name]['words']]


def prefix(name):
    return int(SETS[name]['prefix'], 16)


def step(name):
    return int(SETS[name]['step'], 16)


def frame(word, prefix=0x1f550c4, suffix=0, count=0):
    return Frame(prefix, word, suffix, count, 410, GAP)


def fit(words, **kwargs):
    out = Profile()
    inputs = (Frame * PRESSES)(*[frame(w, **kwargs) for w in words[:PRESSES]])
    return lib.seq_fit(inputs, c.byref(out)), out


def sync(profile, word, **kwargs):
    delta = c.c_int32()
    ok = lib.seq_sync(c.byref(profile), c.byref(frame(word, **kwargs)), c.byref(delta))
    return ok, delta.value


class CoreTests(unittest.TestCase):
    def test_fixture_layout(self):
        self.assertEqual(sorted(SETS), ['button_1', 'button_2', 'button_3', 'button_4',
                                        'remote_a', 'remote_b'])
        for name in SETS:
            self.assertEqual(len(recordings(name)), PRESSES, name)
            self.assertEqual(len(words(name)), PRESSES, name)

    def test_learn_every_recorded_set(self):
        """Five real presses fit; replaying from the first press reproduces the other four."""
        for name in SETS:
            with self.subTest(set=name):
                w = words(name)
                ok, p = fit(w, prefix=prefix(name))
                self.assertTrue(ok)
                self.assertEqual(p.step, step(name))
                self.assertEqual(p.frame.word, w[-1])
                p.accumulator = (p.accumulator - (PRESSES - 1) * p.step) & 0xffff
                p.frame.word = w[0]
                for expected in w[1:]:
                    lib.seq_advance(c.byref(p))
                    self.assertEqual(p.frame.word, expected)

    def test_sync_to_each_recorded_press(self):
        """Hearing press i of a learned button moves the profile there and replay continues."""
        for name in SETS:
            with self.subTest(set=name):
                w = words(name)
                for i, heard in enumerate(w):
                    _, p = fit(w, prefix=prefix(name))
                    sends = p.sends
                    ok, delta = sync(p, heard, prefix=prefix(name))
                    self.assertTrue(ok)
                    self.assertEqual(delta, i - (PRESSES - 1))
                    self.assertEqual(p.frame.word, heard)
                    self.assertEqual(p.sends, sends)
                    for expected in w[i + 1:]:
                        lib.seq_advance(c.byref(p))
                        self.assertEqual(p.frame.word, expected)

    def test_sync_ahead_and_behind(self):
        for step_, flag in [(0x3762, 0x8000), (0xd1c1, 0), (0xd1c2, 0x8000),
                            (0xd1c3, 0), (0xd1c4, 0x8000)]:
            with self.subTest(step=step_):
                seed = 0x1234
                word_at = lambda i: flag | permute(((seed + i * step_) & 0xffff) >> 1)
                ok, p = fit([word_at(i) for i in range(PRESSES)])
                self.assertTrue(ok)
                ok, delta = sync(p, word_at(7))
                self.assertTrue(ok)
                self.assertEqual(delta, 3)
                lib.seq_advance(c.byref(p))
                self.assertEqual(p.frame.word, word_at(8))
                ok, delta = sync(p, word_at(2))
                self.assertTrue(ok)
                self.assertEqual(delta, -6)
                limit = 65541 if step_ == 0x3762 else 40
                for i in range(3, limit):
                    lib.seq_advance(c.byref(p))
                    self.assertEqual(p.frame.word, word_at(i))

    def test_sync_rejects_other_button(self):
        w = words('remote_a')
        _, p = fit(w, prefix=prefix('remote_a'))
        before = bytes(p)
        self.assertFalse(sync(p, w[0], prefix=prefix('remote_a') ^ 1)[0])
        self.assertFalse(sync(p, w[0], prefix=prefix('remote_a'), suffix=1, count=1)[0])
        self.assertFalse(sync(p, w[0] ^ 0x8000, prefix=prefix('remote_a'))[0])
        self.assertEqual(bytes(p), before)

    def test_whole_counter_cycles(self):
        for step_, flag in [(0x3762, 0x8000), (0xd1c1, 0), (0xd1c2, 0x8000),
                            (0xd1c3, 0), (0xd1c4, 0x8000)]:
            for seed in (0xfffe, 0xffff):
                w = [flag | permute(((seed + i * step_) & 0xffff) >> 1) for i in range(PRESSES)]
                ok, p = fit(w)
                self.assertTrue(ok)
                for i in range(PRESSES, 65541):
                    lib.seq_advance(c.byref(p))
                    self.assertEqual(p.frame.word,
                                     flag | permute(((seed + i * step_) & 0xffff) >> 1))

    def test_missed_corrupt_and_wrong_button(self):
        w = words('remote_a')
        run = [0x8000 | permute(((0x1234 + i * 0x3762) & 0xffff) >> 1) for i in range(6)]
        self.assertFalse(fit([run[i] for i in (0, 1, 3, 4, 5)])[0])  # one press missed
        self.assertFalse(fit([w[i] for i in (0, 1, 1, 2, 3)])[0])  # duplicate press
        self.assertFalse(fit(w[:4] + [w[4] ^ 0x100])[0])  # corrupted bit
        inputs = (Frame * PRESSES)(*[frame(x) for x in w])
        inputs[3].prefix ^= 1
        self.assertFalse(lib.seq_fit(inputs, c.byref(Profile())))  # other remote
        inputs[3].prefix ^= 1
        inputs[3].suffix_count = 1
        self.assertFalse(lib.seq_fit(inputs, c.byref(Profile())))  # other button tail

    def test_journal_crc_roundtrip(self):
        _, p = fit(words('remote_a'))
        p.generation = 73
        lib.seq_advance(c.byref(p))
        raw = (c.c_uint8 * 64)()
        lib.seq_pack(c.byref(p), raw)
        restored = Profile()
        self.assertTrue(lib.seq_unpack(raw, c.byref(restored)))
        self.assertEqual(restored.accumulator, p.accumulator)
        self.assertEqual(restored.generation, 73)
        self.assertEqual(restored.sends, 1)
        lib.seq_advance(c.byref(p))
        lib.seq_advance(c.byref(restored))
        self.assertEqual(p.frame.word, restored.frame.word)
        for offset in range(64):
            raw[offset] ^= 1
            self.assertFalse(lib.seq_unpack(raw, c.byref(Profile())))
            raw[offset] ^= 1

    def test_waveform_roundtrip_including_suffix(self):
        for tail_count in (0, 1, 8):
            for word in (0, 0xffff, 0xa0e7, 0x6d99):
                original = frame(word, suffix=(1 << tail_count) - 1, count=tail_count)
                decoder, received = Decoder(), Frame()
                lib.seq_decode(c.byref(decoder), False, original.gap, c.byref(received))
                hits = 0
                for i in range(2 * (47 + tail_count)):
                    level, duration = c.c_bool(), c.c_uint32()
                    self.assertTrue(lib.seq_pulse(c.byref(original), i, c.byref(level), c.byref(duration)))
                    hits += lib.seq_decode(c.byref(decoder), level.value, duration.value, c.byref(received))
                self.assertEqual(hits, 1)
                self.assertEqual(received.word, original.word)
                self.assertEqual(received.prefix, original.prefix)
                self.assertEqual(received.suffix_count, tail_count)
                self.assertEqual(received.suffix, original.suffix)

    def test_live_decoder_on_recordings(self):
        """The C pulse decoder recovers every recorded press from its raw BinRAW pulses."""
        blocks = matched = 0
        for name in SETS:
            for path, expected in zip(recordings(name), words(name)):
                decoded = 0
                for data, n, te in binraw_blocks(path):
                    blocks += 1
                    samples = ''.join(f'{b:08b}' for b in data)[:n]
                    runs = [(v == '1', len(list(g)) * te) for v, g in groupby(samples)]
                    if runs[-1][0]:
                        runs.append((False, GAP))
                    else:
                        runs[-1] = (False, GAP)
                    decoder, result = Decoder(), Frame()
                    hit = any(lib.seq_decode(c.byref(decoder), level, duration, c.byref(result))
                              for level, duration in runs)
                    if hit:
                        self.assertEqual(result.word, expected, path)
                        self.assertEqual(result.prefix, prefix(name), path)
                        decoded += 1
                self.assertGreater(decoded, 0, f'{path} yielded no frame')
                matched += decoded
        print(f'Live decoder matched {matched}/{blocks} recorded blocks')
        self.assertGreaterEqual(matched, DECODED_BLOCKS_FLOOR)



if __name__ == '__main__':
    unittest.main(verbosity=2)
