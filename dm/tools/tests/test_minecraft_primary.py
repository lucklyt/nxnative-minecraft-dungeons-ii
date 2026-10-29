"""Execute the signed login adapters with synthetic accounts, never credentials.

Requires optional `unicorn`; no game files or network access are used.
"""
import importlib
import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
try:
    unicorn = importlib.import_module('unicorn')
    reg = importlib.import_module('unicorn.arm64_const')
    have_unicorn = True
except ImportError:
    unicorn = types.SimpleNamespace()
    reg = types.SimpleNamespace()
    have_unicorn = False

PROFILE = Path(__file__).resolve().parents[2] / 'games/0100A7C01B792000/patches.py'
spec = importlib.util.spec_from_file_location('minecraft_patches', PROFILE)
assert spec is not None and spec.loader is not None
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


@unittest.skipUnless(have_unicorn, 'optional unicorn package required')
class MicrosoftPrimary(unittest.TestCase):
    def setUp(self):
        self.base = 0x100000000
        self.heap = 0x20000000
        self.stop = 0x30000000
        self.uc = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
        self.uc.mem_map(self.heap, 0x10000)
        self.uc.mem_map(self.stop, 0x1000)
        self.uc.reg_write(reg.UC_ARM64_REG_SP, self.heap + 0xf000)
        self.uc.reg_write(reg.UC_ARM64_REG_LR, self.stop)
        self.calls = []
        self.handlers = {self.stop: self.uc.emu_stop}
        self.sites = {}
        words = []
        ctx = types.SimpleNamespace(here=lambda: 0x10000 + len(words) * 4,
                                    emit=words.append, site=lambda *args: None,
                                    put=lambda off, code, why: self.sites.update({off: code}))
        profile.microsoft_primary(ctx)
        self.uc.mem_map(self.base + 0x10000, 0x10000)
        self.uc.mem_write(self.base + 0x10000, b''.join(struct.pack('<I', w) for w in words))
        for off in (0x42870e0, 0x427d7f0, 0x52a6470, 0x52a6c18, 0x52b7bdc, 0x52b7c5c, 0x52c0544, 0x7388790):
            page = (self.base + off) & ~0xfff
            if page not in getattr(self, 'mapped', set()):
                self.uc.mem_map(page, 0x1000)
                self.mapped = getattr(self, 'mapped', set()) | {page}
        self.context, self.subsystem = self.heap + 0x100, self.heap + 0x200
        self.identity, self.data = self.heap + 0x1000, self.heap + 0x3000
        self.account = self.heap + 0x4000
        self.write64(self.context + 8, self.subsystem)
        self.write64(self.subsystem + 0xd8, self.identity)
        self.write64(self.identity, self.base + 0x8e77cf8)
        self.write64(self.identity + 0x190, self.data)
        self.write32(self.identity + 0x1b8, 32)
        self.write32(self.identity + 0x1a0, 1 << 9)
        # A freed sparse slot points at an invalid address and must never be read.
        self.write64(self.data + 0x10, 0xdead0000)
        self.write64(self.data + 9 * 0x28 + 0x10, self.account)
        self.write64(self.account, self.base + 0x8e77b90)
        self.write32(self.account + 0x70, 2)
        for off in (0x88, 0x98):
            self.write64(self.account + off, self.heap + 0x5000 + off)
            self.write32(self.account + off + 8, 12)
        self.uc.reg_write(reg.UC_ARM64_REG_X0, self.heap + 0x6000)
        self.uc.reg_write(reg.UC_ARM64_REG_X1, 2)
        self.uc.reg_write(reg.UC_ARM64_REG_X2, self.heap + 0x6100)
        self.uc.reg_write(reg.UC_ARM64_REG_X8, self.stop + 0x100)
        self.uc.reg_write(reg.UC_ARM64_REG_X19, self.heap + 0x6200)
        self.uc.reg_write(reg.UC_ARM64_REG_X21, self.heap + 0x6300)
        self.handlers[self.base + 0x42870e0] = lambda: self.return_value(self.context)
        self.handlers[self.base + 0x427d7f0] = lambda: self.return_value(self.subsystem)
        for name, off in [('playfab', 0x52a6470), ('minecraft', 0x52a6c18)]:
            self.handlers[self.base + off] = lambda name=name: self.capture(name)
        self.handlers[self.stop + 0x100] = lambda: self.capture('fallback')
        self.uc.hook_add(unicorn.UC_HOOK_CODE, self.hook)

    def write64(self, addr, value):
        self.uc.mem_write(addr, struct.pack('<Q', value))

    def write32(self, addr, value):
        self.uc.mem_write(addr, struct.pack('<I', value))

    def return_value(self, value):
        self.uc.reg_write(reg.UC_ARM64_REG_X0, value)
        self.uc.reg_write(reg.UC_ARM64_REG_PC, self.uc.reg_read(reg.UC_ARM64_REG_LR))

    def capture(self, name):
        self.calls.append((name, self.uc.reg_read(reg.UC_ARM64_REG_X0),
                           self.uc.reg_read(reg.UC_ARM64_REG_X1)))
        self.return_value(0)

    def hook(self, uc, address, size, data):
        fn = self.handlers.get(address)
        if fn:
            fn()

    def run_provider(self, site):
        word = self.sites[site][0]
        delta = word & 0x3ffffff
        if delta & 0x2000000:
            delta -= 0x4000000
        start = self.base + site + delta * 4
        self.uc.emu_start(start, self.stop + 4, count=10000)
        self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_SP), self.heap + 0xf000)

    def test_playfab_audience_and_state(self):
        self.run_provider(0x52a6414)
        self.assertEqual(self.calls, [('playfab', self.heap + 0x6300, self.account + 0x98)])

    def test_minecraft_audience_and_state(self):
        self.run_provider(0x52a67dc)
        self.assertEqual(self.calls, [('minecraft', self.heap + 0x6200, self.account + 0x88)])

    def test_other_local_user_is_not_forwarded(self):
        self.write32(self.account + 0x70, 3)
        self.run_provider(0x52a6414)
        self.assertEqual(self.calls, [('fallback', self.heap + 0x6000, 2)])

    def test_empty_token_preserves_original_failure_provider(self):
        self.write32(self.account + 0x98 + 8, 0)
        self.run_provider(0x52a6414)
        self.assertEqual(self.calls[0][0], 'fallback')

    def test_missing_context_preserves_original_provider(self):
        self.handlers[self.base + 0x42870e0] = lambda: self.return_value(0)
        self.run_provider(0x52a6414)
        self.assertEqual(self.calls[0][0], 'fallback')

    def test_other_identity_layout_is_rejected(self):
        self.write64(self.identity, 0x12340000)
        self.run_provider(0x52a6414)
        self.assertEqual(self.calls[0][0], 'fallback')


    def run_selector(self, online, original=0):
        self.uc.mem_map(self.base + 0xaf1f000, 0x1000)
        self.write64(self.base + 0xaf1f360, original)
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(online)
        self.handlers[self.base + 0x52c0544] = lambda: self.return_value(self.heap + 0x7000)
        for off in (0x52b7c5c, 0x52b7bdc):
            def finish(off=off):
                self.calls.append((off, self.uc.reg_read(reg.UC_ARM64_REG_X1)))
                self.uc.emu_stop()
            self.handlers[self.base + off] = finish
        self.run_provider(0x52b7bd0)

    def test_online_uses_real_microsoft_factory(self):
        self.run_selector(1)
        self.assertEqual(self.calls, [(0x52b7c5c, self.heap + 0x7000)])

    def test_offline_restores_original_initialized_factory(self):
        self.run_selector(0, self.heap + 0x7100)
        self.assertEqual(self.calls, [(0x52b7c5c, self.heap + 0x7100)])

    def test_offline_preserves_original_lazy_factory_path(self):
        self.run_selector(0)
        self.assertEqual(self.calls, [(0x52b7bdc, 0)])


if __name__ == '__main__':
    unittest.main()
