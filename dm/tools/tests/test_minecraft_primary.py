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
        for off in (0x42870e0, 0x427d7f0, 0x52a6470, 0x52a6c18, 0x52b7bdc, 0x52b7c5c, 0x52c0544, 0x7388790,
                    0x42870b0, 0x4280904, 0x52c5b70, 0x52c5b8c, 0x52c41ac,
                    0x4768bb0, 0x52c59b4, 0x4766d78, 0x4db38c, 0x4769cf8, 0x52a7218, 0x52af060, 0x52af014, 0x52c0e3c, 0x4292670, 0x4767c34, 0x476794c):
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

    def run_title_route(self, online, identity, signed_in):
        owner = self.heap + 0x6500
        self.uc.reg_write(reg.UC_ARM64_REG_X20, owner)
        self.write32(owner + 0x100, 7)
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(online)
        self.handlers[self.base + 0x42870b0] = lambda: self.return_value(identity)
        def status():
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), identity)
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), 7)
            self.return_value(signed_in)
        self.handlers[self.base + 0x4280904] = status
        for off in (0x52c5b70, 0x52c5b8c):
            def finish(off=off):
                self.calls.append(off)
                self.uc.emu_stop()
            self.handlers[self.base + off] = finish
        self.run_provider(0x52c5a5c)

    def test_unlinked_online_player_reaches_local_menu(self):
        self.run_title_route(1, self.identity, 0)
        self.assertEqual(self.calls, [0x52c5b8c])

    def test_missing_identity_reaches_local_menu(self):
        self.run_title_route(1, 0, 0)
        self.assertEqual(self.calls, [0x52c5b8c])

    def test_authenticated_player_retains_online_flow(self):
        self.run_title_route(1, self.identity, 1)
        self.assertEqual(self.calls, [0x52c5b70])

    def test_offline_authenticated_player_reaches_local_menu(self):
        self.run_title_route(0, self.identity, 1)
        self.assertEqual(self.calls, [0x52c5b8c])

    def run_settings_handoff(self, online=1, kind=0, signed_in=1, have_selector=True, gdk=True):
        controller, owner = self.heap + 0x6200, self.heap + 0x6500
        selector, flow, context = self.heap + 0x7000, self.heap + 0x7200, self.heap + 0x7400
        self.uc.reg_write(reg.UC_ARM64_REG_X20, controller + 0x60)
        self.write64(controller + 0x50, owner)
        self.write32(owner + 0x100, 7)
        self.uc.mem_write(self.context + 0x60, bytes([kind]))
        self.write64(selector + 0x30, flow)
        self.write64(flow, self.base + (0x92fbba8 if gdk else 0x92fe520))
        saved = {r: self.uc.reg_read(r) for r in (reg.UC_ARM64_REG_X19, reg.UC_ARM64_REG_X20, reg.UC_ARM64_REG_X21)}
        actions = []
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(online)
        self.handlers[self.base + 0x42870b0] = lambda: self.return_value(self.context)
        def handler(name, expected, result):
            def run():
                for r, value in expected.items():
                    self.assertEqual(self.uc.reg_read(r), value)
                actions.append(name)
                self.return_value(result)
            return run
        self.handlers[self.base + 0x4280904] = handler('status', {reg.UC_ARM64_REG_X0: self.context, reg.UC_ARM64_REG_X1: 7}, signed_in)
        self.handlers[self.base + 0x52c41ac] = handler('selector', {reg.UC_ARM64_REG_X0: owner}, selector if have_selector else 0)
        self.handlers[self.base + 0x4768bb0] = handler('cancel', {reg.UC_ARM64_REG_X0: controller}, 0)
        self.handlers[self.base + 0x52c59b4] = handler('select', {reg.UC_ARM64_REG_X0: selector, reg.UC_ARM64_REG_X1: 0}, 0)
        self.handlers[self.base + 0x4292670] = handler('context', {reg.UC_ARM64_REG_X0: owner}, context)
        self.handlers[self.base + 0x52c0e3c] = handler('start', {reg.UC_ARM64_REG_X0: flow, reg.UC_ARM64_REG_X1: context}, 0)
        for name, off in [('done', 0x4767c34), ('fallback', 0x476794c)]:
            def finish(name=name):
                actions.append(name)
                if name == 'fallback':
                    self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), self.context)
                self.uc.emu_stop()
            self.handlers[self.base + off] = finish
        self.run_provider(0x4767948)
        for r, value in saved.items():
            self.assertEqual(self.uc.reg_read(r), value)
        return actions

    def test_settings_authenticated_microsoft_user_starts_real_primary(self):
        self.assertEqual(self.run_settings_handoff(), ['status', 'selector', 'select', 'context', 'start', 'done'])

    def test_settings_missing_auth_preserves_original_failure(self):
        self.assertEqual(self.run_settings_handoff(signed_in=0), ['status', 'fallback'])

    def test_settings_other_native_provider_is_untouched(self):
        self.assertEqual(self.run_settings_handoff(kind=3), ['fallback'])

    def test_settings_offline_preserves_original_path(self):
        self.assertEqual(self.run_settings_handoff(online=0), ['fallback'])

    def test_settings_missing_selector_does_not_cancel_original_operation(self):
        self.assertEqual(self.run_settings_handoff(have_selector=False), ['status', 'selector', 'fallback'])

    def test_settings_does_not_start_an_unexpected_selected_flow(self):
        self.assertEqual(self.run_settings_handoff(gdk=False), ['status', 'selector', 'select', 'done'])

    def run_settings_complete(self, online=1, kind=0, pending=1, matching=True, have_subsystem=True):
        controller, owner = self.heap + 0x6200, self.heap + 0x6500
        state, service = self.heap + 0x7000, self.heap + 0x7200
        subsystem = self.heap + 0x7400
        self.uc.reg_write(reg.UC_ARM64_REG_X20, service)
        self.uc.reg_write(reg.UC_ARM64_REG_X19, state)
        self.write64(service + 0x38, owner)
        self.write64(subsystem + 0x30, controller)
        self.write64(controller + 0x50, owner if matching else owner + 0x200)
        self.uc.mem_write(controller + 0xc8, bytes([pending]))
        self.uc.mem_write(self.context + 0x60, bytes([kind]))
        self.write64(state + 0x28, 0x11223344)
        saved = {r: self.uc.reg_read(r) for r in (reg.UC_ARM64_REG_X1, reg.UC_ARM64_REG_X8,
                  reg.UC_ARM64_REG_X19, reg.UC_ARM64_REG_X20, reg.UC_ARM64_REG_X21)}
        actions = []
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(online)
        self.handlers[self.base + 0x42870b0] = lambda: self.return_value(self.context)
        def getter():
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), owner)
            self.return_value(subsystem if have_subsystem else 0)
        self.handlers[self.base + 0x4766d78] = getter
        def weak():
            source = self.uc.reg_read(reg.UC_ARM64_REG_X1)
            target = self.uc.reg_read(reg.UC_ARM64_REG_X0)
            self.assertEqual(struct.unpack('<Q', self.uc.mem_read(source, 8))[0], controller)
            self.write64(target, 0x12345678)
            actions.append('weak')
            self.return_value(0)
        def success():
            weak_ptr = self.uc.reg_read(reg.UC_ARM64_REG_X0)
            self.assertEqual(struct.unpack('<Q', self.uc.mem_read(weak_ptr, 8))[0], 0x12345678)
            actions.append('complete')
            self.return_value(0)
        self.handlers[self.base + 0x4db38c] = weak
        self.handlers[self.base + 0x4769cf8] = success
        self.handlers[self.base + 0x52a7218] = self.uc.emu_stop
        self.run_provider(0x52a7214)
        self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), 0x11223344)
        for r, value in saved.items():
            self.assertEqual(self.uc.reg_read(r), value)
        return actions

    def test_real_server_success_completes_matching_settings_promise(self):
        self.assertEqual(self.run_settings_complete(), ['weak', 'complete'])

    def test_cancelled_settings_is_not_completed(self):
        self.assertEqual(self.run_settings_complete(pending=0), [])

    def test_another_players_settings_is_not_completed(self):
        self.assertEqual(self.run_settings_complete(matching=False), [])

    def test_server_success_does_not_touch_other_platform_linking(self):
        self.assertEqual(self.run_settings_complete(kind=3), [])

    def test_server_success_without_settings_subsystem_is_safe(self):
        self.assertEqual(self.run_settings_complete(have_subsystem=False), [])

    def test_offline_path_does_not_complete_settings(self):
        self.assertEqual(self.run_settings_complete(online=0), [])

    def run_identity_lifetime(self, online=1, kind=0, signed_in=1):
        service, owner = self.heap + 0x7000, self.heap + 0x7200
        self.uc.reg_write(reg.UC_ARM64_REG_X20, service)
        self.write64(service + 0x38, owner)
        self.write64(service + 0x50, self.heap + 0x7400)
        self.write32(owner + 0x100, 7)
        self.uc.mem_write(self.context + 0x60, bytes([kind]))
        saved = {r: self.uc.reg_read(r) for r in (reg.UC_ARM64_REG_X1, reg.UC_ARM64_REG_X8,
                  reg.UC_ARM64_REG_X19, reg.UC_ARM64_REG_X20, reg.UC_ARM64_REG_X21)}
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(online)
        self.handlers[self.base + 0x42870b0] = lambda: self.return_value(self.context)
        def status():
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), self.context)
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), 7)
            self.return_value(signed_in)
        self.handlers[self.base + 0x4280904] = status
        actions = []
        for off, label in [(0x52af060, 'retain-primary'), (0x52af014, 'original-logout')]:
            def finish(label=label):
                actions.append(label)
                if label == 'original-logout':
                    self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), self.heap + 0x7400)
                self.uc.emu_stop()
            self.handlers[self.base + off] = finish
        self.run_provider(0x52af010)
        for r, value in saved.items():
            self.assertEqual(self.uc.reg_read(r), value)
        self.assertNotIn(0x4767bd0, self.sites)  # explicit user unlink is untouched
        return actions

    def test_primary_microsoft_user_survives_session_logout(self):
        self.assertEqual(self.run_identity_lifetime(), ['retain-primary'])

    def test_other_platform_identity_keeps_original_logout(self):
        self.assertEqual(self.run_identity_lifetime(kind=3), ['original-logout'])

    def test_unsigned_identity_keeps_original_logout(self):
        self.assertEqual(self.run_identity_lifetime(signed_in=0), ['original-logout'])

    def test_offline_session_keeps_original_logout(self):
        self.assertEqual(self.run_identity_lifetime(online=0), ['original-logout'])


if __name__ == '__main__':
    unittest.main()
