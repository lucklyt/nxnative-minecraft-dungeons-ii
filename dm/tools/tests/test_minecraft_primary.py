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
from unittest import mock

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
        self.restore: dict[str, int] = {}
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
        original = profile._restore_helpers
        def capture(ctx):
            self.restore = original(ctx)
            return self.restore
        with mock.patch.object(profile, '_restore_helpers', side_effect=capture):
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
        self.handlers[self.base + self.restore['read']] = lambda: self.return_value(0)
        self.handlers[self.base + self.restore['save']] = lambda: self.return_value(0)
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

    def run_title_route(self, online, identity, signed_in, cached=0):
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
        self.handlers[self.base + self.restore['read']] = lambda: self.return_value(cached)
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

    def test_real_saved_user_permits_cold_authentication_attempt(self):
        self.run_title_route(1, self.identity, 0, cached=123456)
        self.assertEqual(self.calls, [0x52c5b70])

    def test_saved_user_does_not_override_offline_mode(self):
        self.run_title_route(0, self.identity, 0, cached=123456)
        self.assertEqual(self.calls, [0x52c5b8c])

    def map_offset(self, off):
        page = (self.base + off) & ~0xfff
        if page not in self.mapped:
            self.uc.mem_map(page, 0x1000)
            self.mapped.add(page)

    def execute_helper(self, name, argument):
        self.uc.reg_write(reg.UC_ARM64_REG_X0, argument)
        self.uc.reg_write(reg.UC_ARM64_REG_LR, self.stop)
        self.uc.emu_start(self.base + self.restore[name], self.stop + 4, count=10000)
        self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_SP), self.heap + 0xf000)
        return self.uc.reg_read(reg.UC_ARM64_REG_X0)

    def install_file_backend(self, data=None, write_error=0):
        self.map_offset(0x98fa000)
        state = {'data': data, 'ops': []}
        def x(r):
            return self.uc.reg_read(reg.UC_ARM64_REG_X0 + r)
        def open_file():
            state['ops'].append('open')
            if state['data'] is None:
                self.return_value(1)
            else:
                self.write64(x(0), 37)
                self.return_value(0)
        def read_file():
            state['ops'].append('read')
            self.assertEqual(x(1), 37)
            chunk = state['data'][:x(4)]
            self.write64(x(0), len(chunk))
            self.uc.mem_write(x(3), chunk)
            self.return_value(0)
        def close_file():
            state['ops'].append('close')
            self.assertEqual(x(0), 37)
            self.return_value(0)
        def create_file():
            state['ops'].append('create')
            self.assertEqual(x(1), 24)
            state['data'] = bytes(24)
            self.return_value(0)
        def write_file():
            state['ops'].append('write')
            self.assertEqual((x(0), x(1), x(3)), (37, 0, 24))
            if not write_error:
                state['data'] = bytes(self.uc.mem_read(x(2), x(3)))
            self.return_value(write_error)
        def delete_file():
            state['ops'].append('delete')
            state['data'] = None
            self.return_value(0)
        def commit():
            state['ops'].append('commit')
            self.assertEqual(bytes(self.uc.mem_read(x(0), 6)), b'save0\0')
            self.return_value(0)
        for i, (offset, handler) in enumerate([(0x98fa038, open_file), (0x98fa048, read_file),
                (0x98fa050, close_file), (0x98fa150, create_file), (0x98fa090, write_file),
                (0x98fa160, delete_file), (0x98fa178, commit)]):
            target = self.stop + 0x200 + i * 0x10
            self.write64(self.base + offset, target)
            self.handlers[target] = handler
        return state

    def test_restore_record_is_bounded_versioned_and_player_owned(self):
        self.handlers.pop(self.base + self.restore['read'])
        record = struct.pack('<8sIIQ', b'NXMSRP1\0', 7, 1, 123456)
        state = self.install_file_backend(record)
        self.assertEqual(self.execute_helper('read', 7), 123456)
        self.assertEqual(state['ops'], ['open', 'read', 'close'])

    def test_missing_truncated_or_wrong_player_record_is_not_restored(self):
        self.handlers.pop(self.base + self.restore['read'])
        for data in (None, b'NXMSRP1', struct.pack('<8sIIQ', b'NXMSRP1\0', 8, 1, 123),
                     struct.pack('<8sIIQ', b'NXMSRP1\0', 7, 2, 123),
                     struct.pack('<8sIIQ', b'invalid\0', 7, 1, 123)):
            state = self.install_file_backend(data)
            self.assertEqual(self.execute_helper('read', 7), 0)
            if data is not None:
                self.assertEqual(state['ops'][-1], 'close')

    def run_record_save(self, text='1234567890123', existing=None, write_error=0):
        self.handlers.pop(self.base + self.restore['save'])
        self.map_offset(0x4280f80)
        self.map_offset(0x8e960)
        self.map_offset(0xb8ec8)
        owner, uid, control, vt, chars = [self.heap + n for n in (0x6500, 0x8000, 0x8100, 0x8200, 0xa000)]
        self.write32(owner + 0x100, 7)
        self.write64(uid, vt)
        self.write64(vt + 0x38, self.stop + 0x300)
        self.uc.mem_write(chars, text.encode('utf-16-le') + b'\0\0')
        actions = []
        def unique_id():
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), 7)
            out = self.uc.reg_read(reg.UC_ARM64_REG_X8)
            self.write64(out, uid)
            self.write64(out + 8, control)
            self.return_value(0)
        def to_string():
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), uid)
            out = self.uc.reg_read(reg.UC_ARM64_REG_X8)
            self.write64(out, chars)
            self.write32(out + 8, len(text) + 1)
            self.write32(out + 12, len(text) + 1)
            self.return_value(0)
        def parse():
            actions.append('parse-id')
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), chars)
            self.return_value(int(text))
        self.handlers[self.base + 0x4280f80] = unique_id
        self.handlers[self.stop + 0x300] = to_string
        self.handlers[self.base + 0x8e960] = parse
        self.handlers[self.base + 0xb8ec8] = lambda: (actions.append('free-string'), self.return_value(0))
        self.handlers[self.base + self.restore['release']] = lambda: (actions.append('release-id'), self.return_value(0))
        state = self.install_file_backend(existing, write_error)
        self.execute_helper('save', owner)
        return state, actions

    def test_record_writer_saves_only_actual_id_and_commits_after_close(self):
        state, actions = self.run_record_save()
        self.assertEqual(struct.unpack('<8sIIQ', state['data']), (b'NXMSRP1\0', 7, 1, 1234567890123))
        self.assertEqual(state['ops'], ['open', 'create', 'open', 'write', 'close', 'commit'])
        self.assertEqual(actions, ['parse-id', 'free-string', 'release-id'])

    def test_record_writer_updates_existing_record(self):
        state, _ = self.run_record_save(existing=bytes(24))
        self.assertNotIn('create', state['ops'])
        self.assertEqual(state['ops'][-2:], ['close', 'commit'])

    def test_record_writer_rejects_non_id_strings(self):
        state, actions = self.run_record_save(text='not-an-id.token')
        self.assertIsNone(state['data'])
        self.assertEqual(state['ops'], [])
        self.assertEqual(actions, ['free-string', 'release-id'])

    def test_failed_record_write_is_closed_without_committing(self):
        state, _ = self.run_record_save(write_error=1)
        self.assertEqual(state['ops'][-1], 'close')
        self.assertNotIn('commit', state['ops'])

    def run_restore_api(self, cached=123456, signed_in=0, kind=0):
        for off in (0x703c930, 0x703cb00, 0x4235908):
            self.map_offset(off)
        async_block, delegate, queue = self.heap + 0x8000, self.heap + 0x8100, self.heap + 0x8200
        self.uc.reg_write(reg.UC_ARM64_REG_X19, 7)
        self.uc.reg_write(reg.UC_ARM64_REG_X20, async_block)
        self.uc.reg_write(reg.UC_ARM64_REG_X23, delegate)
        self.uc.reg_write(reg.UC_ARM64_REG_X24, queue)
        self.uc.mem_write(self.context + 0x60, bytes([kind]))
        self.write64(delegate, self.base + 0x4243dac)
        self.handlers[self.base + 0x7388790] = lambda: self.return_value(1)
        self.handlers[self.base + 0x42870b0] = lambda: self.return_value(self.context)
        self.handlers[self.base + 0x4280904] = lambda: self.return_value(signed_in)
        self.handlers[self.base + self.restore['read']] = lambda: self.return_value(cached)
        calls = []
        def api(name):
            self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), 7)
            if name == 'by-id':
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), cached)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X2), async_block)
                self.assertEqual(struct.unpack('<Q', self.uc.mem_read(delegate, 8))[0], self.base + self.restore['callback'])
            else:
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), async_block)
                self.assertEqual(struct.unpack('<Q', self.uc.mem_read(delegate, 8))[0], self.base + 0x4243dac)
            self.assertEqual(struct.unpack('<QQQ', self.uc.mem_read(async_block, 24)),
                             (queue, delegate, self.base + 0x4240298))
            self.assertEqual(bytes(self.uc.mem_read(async_block + 0x18, 24)), bytes(24))
            calls.append(name)
            self.return_value(0)
        self.handlers[self.base + 0x703c930] = lambda: api('by-id')
        self.handlers[self.base + 0x703cb00] = lambda: api('ui')
        self.handlers[self.base + 0x4235908] = self.uc.emu_stop
        self.run_provider(0x42358ec)
        return calls

    def test_cold_restore_uses_actual_id_and_matching_result_callback(self):
        self.assertEqual(self.run_restore_api(), ['by-id'])

    def test_no_saved_user_keeps_explicit_ui_api(self):
        self.assertEqual(self.run_restore_api(cached=0), ['ui'])

    def test_signed_in_user_keeps_existing_ui_api(self):
        self.assertEqual(self.run_restore_api(signed_in=1), ['ui'])

    def test_other_native_provider_is_not_silently_restored(self):
        self.assertEqual(self.run_restore_api(kind=3), ['ui'])

    def test_live_status_replaces_only_primary_secondary_cache_checks(self):
        for site, owner_off, done, result_reg in [(0x47688e0, 0x28, 0x4768904, reg.UC_ARM64_REG_X19),
                (0x476894c, 0x50, 0x4768998, reg.UC_ARM64_REG_X0),
                (0x47689e0, 0x28, 0x4768a2c, reg.UC_ARM64_REG_X0)]:
            for off in (done, site + 4):
                self.map_offset(off)
            controller, owner = self.heap + 0x6000, self.heap + 0x6500
            self.write64(controller + owner_off, owner)
            self.write32(owner + 0x100, 7)
            self.uc.reg_write(reg.UC_ARM64_REG_X19, controller)
            self.uc.reg_write(reg.UC_ARM64_REG_LR, self.stop)
            self.handlers[self.base + 0x7388790] = lambda: self.return_value(1)
            self.handlers[self.base + 0x42870b0] = lambda: self.return_value(self.context)
            self.handlers[self.base + 0x4280904] = lambda: self.return_value(1)
            self.handlers[self.base + done] = self.uc.emu_stop
            self.run_provider(site)
            self.assertEqual(self.uc.reg_read(result_reg), 1)

    def test_by_id_result_keeps_real_hresult_and_invalidates_failed_restore(self):
        self.map_offset(0x703ca90)
        self.map_offset(0x4243ddc)
        block, async_pointer = self.heap + 0x8500, self.heap + 0x8600
        self.write64(block, self.identity)
        self.write32(block + 8, 7)
        self.write64(async_pointer, self.heap + 0x8700)
        for result in (0, 0x89235105):
            self.uc.reg_write(reg.UC_ARM64_REG_SP, self.heap + 0xf000)
            self.uc.reg_write(reg.UC_ARM64_REG_X0, block)
            self.uc.reg_write(reg.UC_ARM64_REG_X1, async_pointer)
            actions = []
            def get_result(result=result):
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), self.heap + 0x8700)
                self.write64(self.uc.reg_read(reg.UC_ARM64_REG_X1), 0 if result else self.heap + 0x8800)
                self.return_value(result)
            def invalidate(actions=actions):
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), 7)
                actions.append('invalidate')
                self.return_value(0)
            def finish(result=result):
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), result)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X21), block)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X19), self.identity)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_SP), self.heap + 0xef80)
                self.uc.emu_stop()
            self.handlers[self.base + 0x703ca90] = get_result
            self.handlers[self.base + self.restore['delete']] = invalidate
            self.handlers[self.base + 0x4243ddc] = finish
            self.uc.emu_start(self.base + self.restore['callback'], self.stop + 4, count=10000)
            self.assertEqual(actions, ['invalidate'] if result else [])

    def test_real_identity_logout_invalidates_only_microsoft_restore(self):
        self.map_offset(0x4280644)
        for kind in (0, 3):
            self.uc.reg_write(reg.UC_ARM64_REG_X0, self.context)
            self.uc.reg_write(reg.UC_ARM64_REG_X1, 7)
            self.uc.mem_write(self.context + 0x60, bytes([kind]))
            actions = []
            def invalidate(actions=actions):
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), 7)
                actions.append('invalidate')
                self.return_value(0)
            def original():
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X0), self.context)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_X1), 7)
                self.assertEqual(self.uc.reg_read(reg.UC_ARM64_REG_SP), self.heap + 0xefc0)
                self.uc.reg_write(reg.UC_ARM64_REG_SP, self.heap + 0xf000)
                self.uc.emu_stop()
            self.handlers[self.base + self.restore['delete']] = invalidate
            self.handlers[self.base + 0x4280644] = original
            self.run_provider(0x4280640)
            self.assertEqual(actions, ['invalidate'] if kind == 0 else [])

    def test_restore_delete_does_not_remove_another_players_record(self):
        self.handlers.pop(self.base + self.restore['read'])
        state = self.install_file_backend(struct.pack('<8sIIQ', b'NXMSRP1\0', 8, 1, 123))
        self.execute_helper('delete', 7)
        self.assertNotIn('delete', state['ops'])
        self.assertNotIn('commit', state['ops'])

    def test_restore_delete_commits_matching_record_removal(self):
        self.handlers.pop(self.base + self.restore['read'])
        state = self.install_file_backend(struct.pack('<8sIIQ', b'NXMSRP1\0', 7, 1, 123))
        self.execute_helper('delete', 7)
        self.assertEqual(state['ops'][-2:], ['delete', 'commit'])
        self.assertIsNone(state['data'])


if __name__ == '__main__':
    unittest.main()
