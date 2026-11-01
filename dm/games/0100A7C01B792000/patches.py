# Minecraft Dungeons II 1.1.3.0, Unreal Engine 5.6.1, main build c6b32f4d.
# The native iPhone address space cannot hold the allocator's 38 GB pool reservation.
# hle_uemalloc.c installs the verified UE5_6 vtable in the bss object before startup.
CUTS = {
    'c6b32f4d3d4243ff190b082b4f6aea18': [
        # Platform-limit adaptation: local host users have no Nintendo NSAID.
        # Use actual explicit network availability for the two Microsoft link UI
        # eligibility queries. Preserve the unlinked/controller checks and real
        # XAL authentication, callbacks, tokens and server responses downstream.
        (0x535fe90, 'ed19d097', '40a28094', 'Microsoft link description: local user eligibility follows NIFM availability'),
        (0x5633720, 'c9cbc497', '1c547594', 'Microsoft link action: local user eligibility follows NIFM availability'),
        (0x143b0, '6c700294', '1f2003d5', 'nninitStartup: replace FMallocBinned3 constructor at 0xb0560 with host allocator'),
        (0x427d9f4, '61008052', '01008052', 'Prefer existing Microsoft provider 0 for native context/service; preserve actual account and token operations'),
        (0x427d9f8, '74008052', '14008052', 'Prefer existing Microsoft provider 0 for native context/service; preserve actual account and token operations'),
        (0x427dc44, '61008052', '01008052', 'Prefer existing Microsoft provider 0 for native context/service; preserve actual account and token operations'),
        (0x427dc48, '74008052', '14008052', 'Prefer existing Microsoft provider 0 for native context/service; preserve actual account and token operations'),
        (0x4243dd8, '2ee3b794', '9ee3b794', 'Read the matching real interactive add-user result'),
        # Preserve SelectFlow's original title-entry checks. Forcing its online
        # branch here starts interactive XAL before the Settings link UI exists,
        # leaving a fresh user behind the title's Signing In overlay.
        (0x7905278, '74002f436c69656e742f4c6f67696e576974684e696e74656e646f536572766963654163636f756e74004765', ''.join(('74002f436c69656e742f4c6f', '67696e5769746858626f7800', '000000000000000000000000', '0000000000004765')), 'Genuine Xbox PlayFab endpoint'),
        (0x759fabc, '00004900640065006e00740069007400790054006f006b0065006e00', ''.join(('0000580062006f0078005400', '6f006b0065006e0000000000', '00000000')), 'Genuine Xbox credential field'),
        (0x52c0df4, 'a0000036', '1f2003d5', 'Microsoft experimental primary uses real interactive NativeIdentity instead of absent Xbox OS external UI'),
    ],
}

# Portable providers for the exact build above. Reuse the game's authenticated
# Microsoft identity and audience-specific FString tokens; no token is logged,
# manufactured or persisted here. Missing users/tokens use the original provider
# and its failure callbacks. Sparse-map allocation bits and local-user index
# prevent forwarding a removed account or another local user's credentials.
from nso_asm import ADRP, B, ADDI, SUBI, STP, LDP, LDRX, STRX, CMPR



def _settings_handoff(ctx):
    # Settings supplies the real RemoteConnect UI. After its genuine Microsoft
    # result and account-ID checks, kind 0 has no cross-platform native-token
    # provider (its slot is main+0x10, returning false). Retain its live UI
    # promise while the original GDK primary authenticates with both servers.
    # _settings_complete resolves it only on the original server-success path.
    site = 0x4767948
    ctx.site(site, 'da7dec97')
    start = ctx.here()
    words, labels, branches = [], {}, []

    def emit(word):
        words.append(word)

    def bl(target):
        emit(B(start + 4 * len(words), target) | 0x80000000)

    def mark(name):
        labels[name] = len(words)

    def branch(opcode, name):
        branches.append((len(words), opcode, name))
        emit(0)

    def restore():
        for word in (0xa94107e0, 0xa9420fe2, 0xa94317e4, 0xa9441fe6,
                     0xf9402be8, 0xa94653f3, 0xf9403bf5, 0xa9407bfd,
                     0x910243ff):
            emit(word)

    emit(0xd10243ff)  # sub sp, sp, #0x90
    emit(0xa9007bfd)  # stp x29, x30, [sp]
    emit(0xa90107e0)  # stp x0, x1, [sp, #0x10]
    emit(0xa9020fe2)  # stp x2, x3, [sp, #0x20]
    emit(0xa90317e4)  # stp x4, x5, [sp, #0x30]
    emit(0xa9041fe6)  # stp x6, x7, [sp, #0x40]
    emit(0xf9002be8)  # str x8, [sp, #0x50]
    emit(0xa90653f3)  # stp x19, x20, [sp, #0x60]
    emit(0xf9003bf5)  # str x21, [sp, #0x70]
    emit(ADRP(16, start + 4 * len(words), 0x7388000))
    emit(0x911e4210)  # add x16, x16, #0x790: NIFM availability
    emit(0xd63f0200)  # blr x16
    branch(0x36000000, 'fallback')  # tbz w0, #0
    bl(0x42870b0)
    branch(0xb4000000, 'fallback')  # cbz x0
    emit(0x39418008)  # ldrb w8, [x0, #0x60]: actual native provider kind
    branch(0x35000008, 'fallback')  # cbnz w8: only Microsoft kind 0
    emit(0xf9402a68)  # ldr x8, [x19, #0x50]: actual local player
    branch(0xb4000008, 'fallback')  # cbz x8
    emit(0xb9410101)  # ldr w1, [x8, #0x100]: local-player index
    bl(0x4280904)    # real native identity login status
    branch(0x36000000, 'fallback')  # tbz w0, #0
    emit(0xf9402a60)  # ldr x0, [x19, #0x50]
    bl(0x52c41ac)    # original live UClientLoginFlowSelector getter
    branch(0xb4000000, 'fallback')  # cbz x0
    emit(0xaa0003f4)  # mov x20, x0
    emit(0xaa1403e0)  # mov x0, x20
    emit(0x52800001)  # mov w1, #0
    bl(0x52c59b4)    # original SelectFlow, owns/retains the selected flow
    emit(0xf9401a95)  # ldr x21, [x20, #0x30]
    branch(0xb4000015, 'done')  # cbz x21
    emit(0xf94002a8)  # ldr x8, [x21]
    emit(ADRP(9, start + 4 * len(words), 0x92fb000))
    emit(0x912ea129)  # add x9, x9, #0xba8: GDK primary vtable
    emit(0xeb09011f)  # cmp x8, x9
    branch(0x54000001, 'done')  # b.ne
    emit(0xf9402a60)  # ldr x0, [x19, #0x50]
    bl(0x4292670)    # actual player's login context
    branch(0xb4000000, 'done')  # cbz x0
    emit(0xaa0003e1)  # mov x1, x0
    emit(0xaa1503e0)  # mov x0, x21
    bl(0x52c0e3c)    # original GDK primary start; real auth and server callbacks
    mark('done')
    restore()
    emit(B(start + 4 * len(words), 0x4767c34))  # original callback cleanup
    mark('fallback')
    restore()
    bl(0x42870b0)    # displaced original call
    emit(B(start + 4 * len(words), 0x476794c))
    for index, opcode, name in branches:
        delta = labels[name] - index
        mask = 0x3fff if opcode & 0x7e000000 == 0x36000000 else 0x7ffff
        assert -(mask // 2) <= delta <= mask // 2
        words[index] = opcode | ((delta & mask) << 5)
    for word in words:
        ctx.emit(word)
    ctx.put(site, [B(site, start)],
            'Retain Settings completion while the original GDK flow performs genuine server login')


def _settings_complete(ctx, restore):
    # This site is after the original Minecraft response validation, account
    # lookup and successful session/profile initialization (52a7310). Neither
    # XAL completion nor token dispatch can reach it on a failed server login.
    # Resolve only a still-live Settings operation belonging to that same player;
    # the game's own weak-object callback delivers and destroys the UI promise.
    site = 0x52a7214
    ctx.site(site, '601640f9')
    start = ctx.here()
    words, branches = [], []

    def emit(word):
        words.append(word)

    def bl(target):
        emit(B(start + 4 * len(words), target) | 0x80000000)

    def guard(opcode):
        branches.append((len(words), opcode))
        emit(0)

    emit(SUBI(31, 31, 0xb0))
    emit(STP(29, 30, 31, 0))
    for r in range(0, 8, 2):
        emit(STP(r, r + 1, 31, 0x10 + r * 8))
    emit(STRX(8, 31, 0x50 // 8))
    emit(STP(19, 20, 31, 0x60))
    emit(STRX(21, 31, 0x70 // 8))
    emit(LDRX(21, 20, 0x38 // 8))  # server-success player's actual ULocalPlayer
    guard(0xb4000015)  # cbz x21
    bl(0x7388790)
    guard(0x36000000)  # tbz w0, #0: explicit network required
    bl(0x42870b0)
    guard(0xb4000000)
    emit(0x39418008)  # ldrb w8, [x0, #0x60]
    guard(0x35000008)  # only Microsoft kind 0
    emit(0xaa1503e0)  # actual server-success owner
    bl(restore['save'])
    emit(0xaa1503e0)  # mov x0, x21
    bl(0x4766d78)    # original player's AccountLinkFlowSubsystem getter
    guard(0xb4000000)
    emit(LDRX(19, 0, 0x30 // 8))
    guard(0xb4000013)
    emit(LDRX(8, 19, 0x50 // 8))
    emit(CMPR(8, 21))
    guard(0x54000001)  # another local player's controller must not be resolved
    emit(0x39432268)  # ldrb w8, [x19, #0xc8]
    emit(0x7100051f)  # cmp w8, #1: operation still alive, not cancelled
    guard(0x54000001)
    emit(STRX(19, 31, 0x88 // 8))
    emit(STRX(31, 31, 0x80 // 8))
    emit(ADDI(0, 31, 0x80))
    emit(ADDI(1, 31, 0x88))
    bl(0x4db38c)     # construct the original FWeakObjectPtr from the real UObject
    emit(ADDI(0, 31, 0x80))
    bl(0x4769cf8)    # original Settings successful completion, after server success
    done = len(words)
    for r in range(0, 8, 2):
        emit(LDP(r, r + 1, 31, 0x10 + r * 8))
    emit(LDRX(8, 31, 0x50 // 8))
    emit(LDP(19, 20, 31, 0x60))
    emit(LDRX(21, 31, 0x70 // 8))
    emit(LDP(29, 30, 31, 0))
    emit(ADDI(31, 31, 0xb0))
    emit(0xf9401660)  # displaced ldr x0, [x19, #0x28]
    emit(B(start + 4 * len(words), site + 4))
    for index, opcode in branches:
        mask = 0x3fff if opcode & 0x7e000000 == 0x36000000 else 0x7ffff
        words[index] = opcode | (((done - index) & mask) << 5)
    for word in words:
        ctx.emit(word)
    ctx.put(site, [B(site, start)],
            'Complete a matching live Settings UI only after genuine Minecraft server login')


def _primary_identity_lifetime(ctx):
    # Platform-limit adaptation: Switch's session logout treats Microsoft as a
    # secondary identity and signs it out together with the native identity.
    # In this port they are the same real primary Microsoft user. Keep that user
    # across game-session/title transitions; the original state continuation
    # still clears the game session. Actual identity logout outside this session-teardown state retains its
    # original Microsoft logout and invalidates the private restore record.
    site = 0x52af010
    ctx.site(site, '802a40f9')
    start = ctx.here()
    words, branches = [], []

    def emit(word):
        words.append(word)

    def bl(target):
        emit(B(start + 4 * len(words), target) | 0x80000000)

    def guard(opcode):
        branches.append((len(words), opcode))
        emit(0)

    def restore():
        for r in range(0, 8, 2):
            emit(LDP(r, r + 1, 31, 0x10 + r * 8))
        emit(LDRX(8, 31, 0x50 // 8))
        emit(LDP(29, 30, 31, 0))
        emit(ADDI(31, 31, 0x60))

    emit(SUBI(31, 31, 0x60))
    emit(STP(29, 30, 31, 0))
    for r in range(0, 8, 2):
        emit(STP(r, r + 1, 31, 0x10 + r * 8))
    emit(STRX(8, 31, 0x50 // 8))
    bl(0x7388790)
    guard(0x36000000)
    bl(0x42870b0)
    guard(0xb4000000)
    emit(0x39418008)  # actual native context kind
    guard(0x35000008)
    emit(LDRX(8, 20, 0x38 // 8))
    guard(0xb4000008)
    emit(0xb9410101)  # actual local-player index
    bl(0x4280904)
    guard(0x36000000)  # preserve only an actually signed-in primary identity
    restore()
    emit(B(start + len(words) * 4, 0x52af060))
    fallback = len(words)
    restore()
    emit(0xf9402a80)  # displaced ldr x0, [x20, #0x50]
    emit(B(start + len(words) * 4, site + 4))
    for index, opcode in branches:
        mask = 0x3fff if opcode & 0x7e000000 == 0x36000000 else 0x7ffff
        words[index] = opcode | (((fallback - index) & mask) << 5)
    for word in words:
        ctx.emit(word)
    ctx.put(site, [B(site, start)],
            'Preserve a real primary Microsoft identity across game-session logout; explicit unlink remains original')


def _primary_link_status(ctx):
    # A Microsoft primary user has no secondary platform-association record.
    # For kind 0 report the real identity status for the controller's own local
    # player; do not invent a saved linked ID or alter other platform providers.
    for site, expected, owner_offset, done, result_reg in (
            (0x47688e0, '600e40f9', 0x28, 0x4768904, 19),
            (0x476894c, '602240f9', 0x50, 0x4768998, 0),
            (0x47689e0, '600e40f9', 0x28, 0x4768a2c, 0)):
        ctx.site(site, expected)
        start = ctx.here()
        words, branches = [], []

        def emit(word, words=words):
            words.append(word)

        def bl(target, start=start, words=words):
            emit(B(start + len(words) * 4, target) | 0x80000000)

        def guard(opcode, branches=branches, words=words):
            branches.append((len(words), opcode))
            emit(0)

        def restore():
            for r in range(0, 8, 2):
                emit(LDP(r, r + 1, 31, 0x10 + r * 8))
            emit(LDRX(8, 31, 0x50 // 8))
            emit(LDP(29, 30, 31, 0))
            emit(ADDI(31, 31, 0x60))

        emit(SUBI(31, 31, 0x60))
        emit(STP(29, 30, 31, 0))
        for r in range(0, 8, 2):
            emit(STP(r, r + 1, 31, 0x10 + r * 8))
        emit(STRX(8, 31, 0x50 // 8))
        bl(0x7388790)
        guard(0x36000000)
        bl(0x42870b0)
        guard(0xb4000000)
        emit(0x39418008)
        guard(0x35000008)  # no change to a different primary provider
        emit(LDRX(8, 19, owner_offset // 8))
        guard(0xb4000008)
        emit(0xb9410101)  # actual local-player index
        bl(0x4280904)
        emit(0x2a0003f0)  # preserve the genuine returned status across restore
        restore()
        emit(0x2a1003e0 | result_reg)
        emit(B(start + len(words) * 4, done))
        fallback = len(words)
        restore()
        emit(int.from_bytes(bytes.fromhex(expected), 'little'))
        emit(B(start + len(words) * 4, site + 4))
        for index, opcode in branches:
            mask = 0x3fff if opcode & 0x7e000000 == 0x36000000 else 0x7ffff
            words[index] = opcode | (((fallback - index) & mask) << 5)
        for word in words:
            ctx.emit(word)
        ctx.put(site, [B(site, start)],
                'Read actual primary Microsoft identity status instead of secondary association cache')


class _RestoreCode:
    """Small exact-build assembler for the private account-cache adapters."""
    def __init__(self, ctx):
        self.ctx, self.start = ctx, ctx.here()
        self.words, self.labels, self.refs, self.data = [], {}, [], []

    def emit(self, word):
        self.words.append(word)

    def mark(self, name):
        self.labels[name] = len(self.words)

    def branch(self, opcode, name):
        self.refs.append((len(self.words), opcode, name, 'branch'))
        self.emit(0)

    def addr(self, reg, name):
        self.refs.append((len(self.words), reg, name, 'adr'))
        self.emit(0)

    def bl(self, target):
        self.emit(B(self.start + len(self.words) * 4, target) | 0x80000000)

    def got(self, offset):
        self.emit(ADRP(16, self.start + len(self.words) * 4, offset))
        self.emit(LDRX(16, 16, (offset & 0xfff) // 8))
        self.emit(0xd63f0200)

    def mov(self, dst, src):
        self.emit(0xaa0003e0 | src << 16 | dst)

    def imm(self, dst, value):
        assert 0 <= value < 65536
        self.emit(0x52800000 | value << 5 | dst)

    def literal(self, name, data):
        self.data.append((name, data))

    def finish(self):
        for name, data in self.data:
            self.mark(name)
            data += b'\0' * (-len(data) % 4)
            self.words.extend(int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4))
        for index, opcode, name, kind in self.refs:
            delta = self.labels[name] - index
            if kind == 'adr':
                n = delta * 4
                assert -(1 << 20) <= n < 1 << 20
                self.words[index] = 0x10000000 | (n & 3) << 29 | ((n >> 2) & 0x7ffff) << 5 | opcode
            elif opcode == 0x14000000:
                self.words[index] = opcode | (delta & 0x3ffffff)
            else:
                mask = 0x3fff if opcode & 0x7e000000 == 0x36000000 else 0x7ffff
                assert -(mask // 2) <= delta <= mask // 2
                self.words[index] = opcode | (delta & mask) << 5
        for word in self.words:
            self.ctx.emit(word)
        return self.start


def _restore_helpers(ctx):
    # This file contains a versioned local-player index and the actual XUID,
    # never a token. The server-success path is its only writer. Normal SDK file
    # operations use the game's mounted transactional save0 archive.
    path = b'save0:/nxnative-ms-primary-v1\0'
    magic = b'NXMSRP1\0'
    helpers = {}

    def begin(frame=0x80):
        a = _RestoreCode(ctx)
        a.emit(SUBI(31, 31, frame))
        a.emit(STP(29, 30, 31, 0))
        a.emit(STP(19, 20, 31, 0x10))
        a.emit(STP(21, 22, 31, 0x20))
        return a

    def end(a, frame=0x80):
        a.emit(LDP(21, 22, 31, 0x20))
        a.emit(LDP(19, 20, 31, 0x10))
        a.emit(LDP(29, 30, 31, 0))
        a.emit(ADDI(31, 31, frame))
        a.emit(0xd65f03c0)

    # Return zero on any missing/truncated/version/player mismatch. File content
    # permits an authentication attempt; it never establishes signed-in status.
    a = begin()
    a.mov(19, 0)
    a.emit(ADDI(0, 31, 0x30)); a.addr(1, 'path'); a.imm(2, 1)
    a.got(0x98fa038)
    a.branch(0x35000000, 'zero')
    a.emit(ADDI(0, 31, 0x38)); a.emit(LDRX(1, 31, 0x30 // 8))
    a.imm(2, 0); a.emit(ADDI(3, 31, 0x40)); a.imm(4, 24)
    a.got(0x98fa048)
    a.mov(20, 0)
    a.emit(LDRX(0, 31, 0x30 // 8)); a.got(0x98fa050)
    a.branch(0x35000014, 'zero')
    a.emit(LDRX(8, 31, 0x38 // 8)); a.emit(0xf100611f)  # cmp x8, #24
    a.branch(0x54000001, 'zero')
    a.addr(9, 'magic'); a.emit(LDRX(9, 9)); a.emit(LDRX(8, 31, 0x40 // 8))
    a.emit(CMPR(8, 9)); a.branch(0x54000001, 'zero')
    a.emit(0xb9404be8)  # ldr w8, [sp, #0x48]: local index
    a.emit(0x6b13011f); a.branch(0x54000001, 'zero')
    a.emit(0xb9404fe8); a.emit(0x7100051f)  # version 1
    a.branch(0x54000001, 'zero')
    a.emit(LDRX(0, 31, 0x50 // 8)); a.branch(0x14000000, 'return')
    a.mark('zero'); a.imm(0, 0)
    a.mark('return'); end(a)
    a.literal('path', path); a.literal('magic', magic)
    helpers['read'] = a.finish()

    a = begin()
    a.bl(helpers['read']); a.branch(0xb4000000, 'return')
    a.addr(0, 'path'); a.got(0x98fa160)
    a.branch(0x35000000, 'return')
    a.addr(0, 'mount'); a.got(0x98fa178)
    a.mark('return'); end(a)
    a.literal('path', path); a.literal('mount', b'save0\0')
    helpers['delete'] = a.finish()

    # Use the game's original shared-reference release tail with its exact
    # stack frame, including the destructor path if the last reference is freed.
    ctx.site(0x4237098, '68220091')
    a = _RestoreCode(ctx)
    a.emit(SUBI(31, 31, 0x30)); a.emit(STP(29, 30, 31, 0x10))
    a.emit(STP(20, 19, 31, 0x20)); a.emit(ADDI(29, 31, 0x10))
    a.mov(19, 0); a.branch(0xb4000013, 'return')
    a.emit(B(a.start + len(a.words) * 4, 0x4237098))
    a.mark('return'); a.emit(LDP(20, 19, 31, 0x20))
    a.emit(LDP(29, 30, 31, 0x10)); a.emit(ADDI(31, 31, 0x30)); a.emit(0xd65f03c0)
    helpers['release'] = a.finish()

    # GetUniquePlayerId -> the ID object's own ToString. The nickname getter is a different method; neither names nor tokens
    # belong in this record.
    a = begin(0xc0)
    a.mov(19, 0)
    a.emit(STP(31, 31, 31, 0x60)); a.emit(STP(31, 31, 31, 0x70))
    a.imm(0, 0); a.bl(0x42870e0); a.branch(0xb4000000, 'cleanup')
    a.emit(0xb9410261)  # ldr w1, [x19, #0x100]
    a.emit(ADDI(8, 31, 0x60)); a.bl(0x4280f80)
    a.emit(LDRX(0, 31, 0x60 // 8)); a.branch(0xb4000000, 'cleanup')
    a.emit(LDRX(9, 0)); a.emit(LDRX(9, 9, 0x38 // 8))
    a.emit(ADDI(8, 31, 0x70)); a.emit(0xd63f0120)
    a.emit(0xb9407be9)  # FString count (including terminator)
    a.emit(0x7100093f); a.branch(0x5400000b, 'cleanup')  # count < 2
    a.emit(0x7100553f); a.branch(0x5400000c, 'cleanup')  # count > 21
    a.emit(LDRX(20, 31, 0x70 // 8)); a.branch(0xb4000014, 'cleanup')
    a.mov(10, 20); a.emit(0x51000529)  # sub w9, w9, #1
    a.mark('digits')
    a.emit(0x78402548)  # ldrh w8, [x10], #2
    a.emit(0x5100c108); a.emit(0x7100251f)  # unsigned (char - '0') <= 9
    a.branch(0x54000008, 'cleanup')
    a.emit(0x71000529); a.branch(0x54000001, 'digits')
    a.emit(0x79400148); a.branch(0x35000008, 'cleanup')  # terminating NUL
    a.mov(0, 20); a.imm(1, 0); a.imm(2, 10); a.bl(0x8e960)
    a.branch(0xb4000000, 'cleanup'); a.mov(22, 0)
    a.addr(8, 'magic'); a.emit(LDRX(8, 8)); a.emit(STRX(8, 31, 0x40 // 8))
    a.emit(0xb9410268); a.emit(0xb9004be8)  # actual local-player index
    a.imm(8, 1); a.emit(0xb9004fe8); a.emit(STRX(22, 31, 0x50 // 8))
    a.emit(ADDI(0, 31, 0x30)); a.addr(1, 'path'); a.imm(2, 2)
    a.got(0x98fa038); a.branch(0x34000000, 'write')
    a.addr(0, 'path'); a.imm(1, 24); a.got(0x98fa150)
    a.branch(0x35000000, 'cleanup')
    a.emit(ADDI(0, 31, 0x30)); a.addr(1, 'path'); a.imm(2, 2)
    a.got(0x98fa038); a.branch(0x35000000, 'cleanup')
    a.mark('write')
    a.imm(8, 1); a.emit(0xb9008be8)  # WriteOption::Flush
    a.emit(LDRX(0, 31, 0x30 // 8)); a.imm(1, 0)
    a.emit(ADDI(2, 31, 0x40)); a.imm(3, 24); a.emit(ADDI(4, 31, 0x88))
    a.got(0x98fa090); a.mov(21, 0)
    a.emit(LDRX(0, 31, 0x30 // 8)); a.got(0x98fa050)
    a.branch(0x35000015, 'cleanup')
    a.addr(0, 'mount'); a.got(0x98fa178)
    a.mark('cleanup')
    a.emit(LDRX(0, 31, 0x70 // 8)); a.branch(0xb4000000, 'release')
    a.bl(0xb8ec8)
    a.mark('release'); a.emit(LDRX(0, 31, 0x68 // 8)); a.bl(helpers['release'])
    end(a, 0xc0)
    a.literal('path', path); a.literal('mount', b'save0\0'); a.literal('magic', magic)
    helpers['save'] = a.finish()

    # The existing NativeIdentity completion consumes only its matching XAL API.
    # A private ById callback retains original HRESULT, user installation,
    # delegates and cleanup. Failure removes restore eligibility for next boot.
    ctx.site(0x4243dac, 'ff0302d1fd7b04a9f85f05a9f65706a9f44f07a9fd030191f50300aa200040f9e1230091b30240f9ff0700f9')
    a = _RestoreCode(ctx)
    for word in (0xd10203ff, 0xa9047bfd, 0xa9055ff8, 0xa90657f6,
                 0xa9074ff4, 0x910103fd, 0xaa0003f5, 0xf9400020,
                 0x910023e1, 0xf94002b3, 0xf90007ff):
        a.emit(word)
    a.bl(0x703ca90)
    a.branch(0x36000000 | 31 << 19, 'result')  # HRESULT >= 0
    a.mov(20, 0); a.emit(0xb9400aa0); a.bl(helpers['delete']); a.mov(0, 20)
    a.mark('result'); a.emit(B(a.start + len(a.words) * 4, 0x4243ddc))
    helpers['callback'] = a.finish()
    return helpers


def _restore_login_call(ctx, helpers):
    site = 0x42358ec
    ctx.site(site, 'e10315aa185c00a9e20314aa008000ad080800f9e003132a0b1cb894')
    a = _RestoreCode(ctx)
    a.emit(STP(29, 30, 31, -0x20) | 0x00800000)  # pre-index frame
    a.bl(0x7388790); a.branch(0x36000000, 'ui')
    a.bl(0x42870b0); a.branch(0xb4000000, 'ui')
    a.emit(0x39418008); a.branch(0x35000008, 'ui')
    a.mov(1, 19); a.bl(0x4280904); a.branch(0x37000000, 'ui')
    a.mov(0, 19); a.bl(helpers['read']); a.branch(0xb4000000, 'ui')
    a.mov(1, 0)
    a.emit(ADRP(8, a.start + len(a.words) * 4, helpers['callback']))
    a.emit(ADDI(8, 8, helpers['callback'] & 0xfff)); a.emit(STRX(8, 23))
    a.emit(STP(24, 23, 20, 0))
    a.emit(0x6f00e400)  # movi v0.2d, #0
    a.emit(0xad008280)  # stp q0, q0, [x20, #0x10]
    # The allocator setup between the hook and original call must always run.
    a.emit(ADRP(8, a.start + len(a.words) * 4, 0x4240000)); a.emit(ADDI(8, 8, 0x298))
    a.emit(STRX(8, 20, 0x10 // 8))
    a.mov(2, 20); a.mov(0, 19); a.bl(0x703c930)
    a.branch(0x14000000, 'return')
    a.mark('ui')
    a.emit(STP(24, 23, 20, 0)); a.emit(0x6f00e400); a.emit(0xad008280)
    a.emit(ADRP(8, a.start + len(a.words) * 4, 0x4240000)); a.emit(ADDI(8, 8, 0x298))
    a.emit(STRX(8, 20, 0x10 // 8))
    a.mov(1, 20); a.mov(0, 19); a.bl(0x703cb00)
    a.mark('return')
    a.emit(LDP(29, 30, 31, 0)); a.emit(ADDI(31, 31, 0x20))
    a.emit(B(a.start + len(a.words) * 4, 0x4235908))
    start = a.finish()
    ctx.put(site, [B(site, start)], 'Restore the real saved Microsoft XUID with matching XAL ById async/result; UI remains explicit')

def _restore_logout(ctx, helpers):
    # Every actual Microsoft identity logout invalidates restore eligibility.
    # The separate game-session lifetime adapter does not enter this method.
    site = 0x4280640
    ctx.site(site, 'ff0301d1')
    a = _RestoreCode(ctx)
    a.emit(SUBI(31, 31, 0x60)); a.emit(STP(29, 30, 31, 0))
    for r in range(0, 8, 2):
        a.emit(STP(r, r + 1, 31, 0x10 + r * 8))
    a.emit(STRX(8, 31, 0x50 // 8))
    a.branch(0xb4000000, 'original')
    a.emit(0x39418008); a.branch(0x35000008, 'original')
    a.mov(0, 1); a.bl(helpers['delete'])
    a.mark('original')
    for r in range(0, 8, 2):
        a.emit(LDP(r, r + 1, 31, 0x10 + r * 8))
    a.emit(LDRX(8, 31, 0x50 // 8)); a.emit(LDP(29, 30, 31, 0))
    a.emit(ADDI(31, 31, 0x60)); a.emit(SUBI(31, 31, 0x40))
    a.emit(B(a.start + len(a.words) * 4, site + 4))
    start = a.finish()
    ctx.put(site, [B(site, start)], 'Invalidate the matching saved XUID on actual Microsoft identity logout')


def microsoft_primary(ctx):
    restore = _restore_helpers(ctx)
    # Source sites are verified against this exact build before installation.
    ctx.site(0x52c5a5c, 'e00314aa')
    ctx.site(0x52b7bd0, '48e3029001b141f9210400b5')
    ctx.site(0x52a6414, '00013fd6')
    ctx.site(0x52a67dc, '00013fd6')
    # A persisted XUID only permits a genuine silent restore attempt. A fresh
    # user still reaches the local menu and explicitly links through Settings.
    a = _RestoreCode(ctx)
    a.emit(SUBI(31, 31, 0x60)); a.emit(STP(29, 30, 31, 0))
    for r in range(0, 8, 2):
        a.emit(STP(r, r + 1, 31, 0x10 + r * 8))
    a.emit(STRX(8, 31, 0x50 // 8))
    a.bl(0x7388790); a.branch(0x36000000, 'local')
    a.bl(0x42870b0); a.branch(0xb4000000, 'local')
    a.emit(0x39418008); a.branch(0x35000008, 'local')
    a.emit(0xb9410281); a.bl(0x4280904)
    a.branch(0x37000000, 'online')
    a.emit(0xb9410280); a.bl(restore['read'])
    a.branch(0xb4000000, 'local')
    for label, target in [('online', 0x52c5b70), ('local', 0x52c5b8c)]:
        a.mark(label)
        for r in range(0, 8, 2):
            a.emit(LDP(r, r + 1, 31, 0x10 + r * 8))
        a.emit(LDRX(8, 31, 0x50 // 8)); a.emit(LDP(29, 30, 31, 0))
        a.emit(ADDI(31, 31, 0x60)); a.emit(B(a.start + len(a.words) * 4, target))
    route = a.finish()
    ctx.put(0x52c5a5c, [B(0x52c5a5c, route)],
            'Use actual identity or a real persisted XUID to attempt original server login; fresh players stay local')
    selector = ctx.here()
    ctx.emit(0xd10183ff)  # sub sp, sp, #0x60
    ctx.emit(0xa9007bfd)  # stp x29, x30, [sp]
    ctx.emit(0xa90107e0)  # stp x0, x1, [sp, #0x10]
    ctx.emit(0xa9020fe2)  # stp x2, x3, [sp, #0x20]
    ctx.emit(0xa90317e4)  # stp x4, x5, [sp, #0x30]
    ctx.emit(0xa9041fe6)  # stp x6, x7, [sp, #0x40]
    ctx.emit(0xf9002be8)  # str x8, [sp, #0x50]
    ctx.emit(ADRP(16, ctx.here(), 0x7388000))
    ctx.emit(0x911e4210)  # add x16, x16, #0x790
    ctx.emit(0xd63f0200)  # blr x16
    ctx.emit(0x36000160)  # tbz w0, #0, offline
    ctx.emit(0xa94107e0)  # ldp x0, x1, [sp, #0x10]
    ctx.emit(0xa9420fe2)  # ldp x2, x3, [sp, #0x20]
    ctx.emit(0xa94317e4)  # ldp x4, x5, [sp, #0x30]
    ctx.emit(0xa9441fe6)  # ldp x6, x7, [sp, #0x40]
    ctx.emit(0xf9402be8)  # ldr x8, [sp, #0x50]
    ctx.emit(B(ctx.here(), 0x52c0544) | 0x80000000)  # bl . // @bl 0x52c0544
    ctx.emit(0xaa0003e1)  # mov x1, x0
    ctx.emit(0xa9407bfd)  # ldp x29, x30, [sp]
    ctx.emit(0x910183ff)  # add sp, sp, #0x60
    ctx.emit(B(ctx.here(), 0x52b7c5c))
    ctx.emit(0xa94107e0)  # ldp x0, x1, [sp, #0x10]
    ctx.emit(0xa9420fe2)  # ldp x2, x3, [sp, #0x20]
    ctx.emit(0xa94317e4)  # ldp x4, x5, [sp, #0x30]
    ctx.emit(0xa9441fe6)  # ldp x6, x7, [sp, #0x40]
    ctx.emit(0xa9407bfd)  # ldp x29, x30, [sp]
    ctx.emit(0x910183ff)  # add sp, sp, #0x60
    ctx.emit(ADRP(8, ctx.here(), 0xaf1f000))
    ctx.emit(0xf941b101)  # ldr x1, [x8, #0x360]
    ctx.emit(0xb4000041)  # cbz x1, original_next
    ctx.emit(B(ctx.here(), 0x52b7c5c))
    ctx.emit(B(ctx.here(), 0x52b7bdc))
    playfab = ctx.here()
    ctx.emit(0xd10143ff)  # sub sp, sp, #0x50
    ctx.emit(0xa9007bfd)  # stp x29, x30, [sp]
    ctx.emit(0x910003fd)  # mov x29, sp
    ctx.emit(0xa90107e0)  # stp x0, x1, [sp, #0x10]
    ctx.emit(0xa90223e2)  # stp x2, x8, [sp, #0x20]
    ctx.emit(0xf9001bf5)  # str x21, [sp, #0x30]
    ctx.emit(0x52800000)  # mov w0, #0
    ctx.emit(B(ctx.here(), 0x42870e0) | 0x80000000)  # bl . // @bl 0x42870e0
    ctx.emit(0xb4000720)  # cbz x0, fallback
    ctx.emit(0xf9400400)  # ldr x0, [x0, #8]
    ctx.emit(0xb40006e0)  # cbz x0, fallback
    ctx.emit(0x52800001)  # mov w1, #0
    ctx.emit(B(ctx.here(), 0x427d7f0) | 0x80000000)  # bl . // @bl 0x427d7f0
    ctx.emit(0xb4000680)  # cbz x0, fallback
    ctx.emit(0xf9406c00)  # ldr x0, [x0, #0xd8]
    ctx.emit(0xb4000640)  # cbz x0, fallback
    ctx.emit(0xf9400009)  # ldr x9, [x0]
    ctx.emit(ADRP(10, ctx.here(), 0x8e77000))
    ctx.emit(0x9133e14a)  # add x10, x10, #0xcf8
    ctx.emit(0xeb0a013f)  # cmp x9, x10
    ctx.emit(0x540005a1)  # b.ne fallback
    ctx.emit(0xb941b80c)  # ldr w12, [x0, #0x1b8]
    ctx.emit(0x3400056c)  # cbz w12, fallback
    ctx.emit(0x7140059f)  # cmp w12, #0x1000
    ctx.emit(0x54000528)  # b.hi fallback
    ctx.emit(0xf940c809)  # ldr x9, [x0, #0x190]
    ctx.emit(0xb40004e9)  # cbz x9, fallback
    ctx.emit(0xf940d80a)  # ldr x10, [x0, #0x1b0]
    ctx.emit(0x9106800b)  # add x11, x0, #0x1a0
    ctx.emit(0xf100015f)  # cmp x10, #0
    ctx.emit(0x9a8a016a)  # csel x10, x11, x10, eq
    ctx.emit(0xb9401bee)  # ldr w14, [sp, #0x18]
    ctx.emit(0x5280000b)  # mov w11, #0
    ctx.emit(0x6b0c017f)  # cmp w11, w12
    ctx.emit(0x540003e2)  # b.hs fallback
    ctx.emit(0x53057d6d)  # lsr w13, w11, #5
    ctx.emit(0xb86d594d)  # ldr w13, [x10, w13, uxtw #2]
    ctx.emit(0x1acb25ad)  # lsr w13, w13, w11
    ctx.emit(0x3600032d)  # tbz w13, #0, next
    ctx.emit(0x5280050d)  # mov w13, #0x28
    ctx.emit(0x9bad256d)  # umaddl x13, w11, w13, x9
    ctx.emit(0xf94009ad)  # ldr x13, [x13, #0x10]
    ctx.emit(0xb40002ad)  # cbz x13, next
    ctx.emit(0xf94001af)  # ldr x15, [x13]
    ctx.emit(ADRP(16, ctx.here(), 0x8e77000))
    ctx.emit(0x912e4210)  # add x16, x16, #0xb90
    ctx.emit(0xeb1001ff)  # cmp x15, x16
    ctx.emit(0x54000201)  # b.ne next
    ctx.emit(0xb94071af)  # ldr w15, [x13, #0x70]
    ctx.emit(0x6b0e01ff)  # cmp w15, w14
    ctx.emit(0x540001a1)  # b.ne next
    ctx.emit(0x910261a1)  # add x1, x13, #0x98
    ctx.emit(0xf940002f)  # ldr x15, [x1]
    ctx.emit(0xb400018f)  # cbz x15, fallback
    ctx.emit(0xb940082f)  # ldr w15, [x1, #8]
    ctx.emit(0x710005ff)  # cmp w15, #1
    ctx.emit(0x54000129)  # b.ls fallback
    ctx.emit(0x52900010)  # mov w16, #0x8000
    ctx.emit(0x6b1001ff)  # cmp w15, w16
    ctx.emit(0x540000c8)  # b.hi fallback
    ctx.emit(0xf9401be0)  # ldr x0, [sp, #0x30]
    ctx.emit(B(ctx.here(), 0x52a6470) | 0x80000000)  # bl . // @bl 0x52a6470
    ctx.emit(0x14000006)  # b done
    ctx.emit(0x1100056b)  # add w11, w11, #1
    ctx.emit(0x17ffffe1)  # b loop
    ctx.emit(0xa94107e0)  # ldp x0, x1, [sp, #0x10]
    ctx.emit(0xa94223e2)  # ldp x2, x8, [sp, #0x20]
    ctx.emit(0xd63f0100)  # blr x8
    ctx.emit(0xa9407bfd)  # ldp x29, x30, [sp]
    ctx.emit(0x910143ff)  # add sp, sp, #0x50
    ctx.emit(0xd65f03c0)  # ret
    minecraft = ctx.here()
    ctx.emit(0xd10143ff)  # sub sp, sp, #0x50
    ctx.emit(0xa9007bfd)  # stp x29, x30, [sp]
    ctx.emit(0x910003fd)  # mov x29, sp
    ctx.emit(0xa90107e0)  # stp x0, x1, [sp, #0x10]
    ctx.emit(0xa90223e2)  # stp x2, x8, [sp, #0x20]
    ctx.emit(0xf9001bf3)  # str x19, [sp, #0x30]
    ctx.emit(0x52800000)  # mov w0, #0
    ctx.emit(B(ctx.here(), 0x42870e0) | 0x80000000)  # bl . // @bl 0x42870e0
    ctx.emit(0xb4000720)  # cbz x0, fallback
    ctx.emit(0xf9400400)  # ldr x0, [x0, #8]
    ctx.emit(0xb40006e0)  # cbz x0, fallback
    ctx.emit(0x52800001)  # mov w1, #0
    ctx.emit(B(ctx.here(), 0x427d7f0) | 0x80000000)  # bl . // @bl 0x427d7f0
    ctx.emit(0xb4000680)  # cbz x0, fallback
    ctx.emit(0xf9406c00)  # ldr x0, [x0, #0xd8]
    ctx.emit(0xb4000640)  # cbz x0, fallback
    ctx.emit(0xf9400009)  # ldr x9, [x0]
    ctx.emit(ADRP(10, ctx.here(), 0x8e77000))
    ctx.emit(0x9133e14a)  # add x10, x10, #0xcf8
    ctx.emit(0xeb0a013f)  # cmp x9, x10
    ctx.emit(0x540005a1)  # b.ne fallback
    ctx.emit(0xb941b80c)  # ldr w12, [x0, #0x1b8]
    ctx.emit(0x3400056c)  # cbz w12, fallback
    ctx.emit(0x7140059f)  # cmp w12, #0x1000
    ctx.emit(0x54000528)  # b.hi fallback
    ctx.emit(0xf940c809)  # ldr x9, [x0, #0x190]
    ctx.emit(0xb40004e9)  # cbz x9, fallback
    ctx.emit(0xf940d80a)  # ldr x10, [x0, #0x1b0]
    ctx.emit(0x9106800b)  # add x11, x0, #0x1a0
    ctx.emit(0xf100015f)  # cmp x10, #0
    ctx.emit(0x9a8a016a)  # csel x10, x11, x10, eq
    ctx.emit(0xb9401bee)  # ldr w14, [sp, #0x18]
    ctx.emit(0x5280000b)  # mov w11, #0
    ctx.emit(0x6b0c017f)  # cmp w11, w12
    ctx.emit(0x540003e2)  # b.hs fallback
    ctx.emit(0x53057d6d)  # lsr w13, w11, #5
    ctx.emit(0xb86d594d)  # ldr w13, [x10, w13, uxtw #2]
    ctx.emit(0x1acb25ad)  # lsr w13, w13, w11
    ctx.emit(0x3600032d)  # tbz w13, #0, next
    ctx.emit(0x5280050d)  # mov w13, #0x28
    ctx.emit(0x9bad256d)  # umaddl x13, w11, w13, x9
    ctx.emit(0xf94009ad)  # ldr x13, [x13, #0x10]
    ctx.emit(0xb40002ad)  # cbz x13, next
    ctx.emit(0xf94001af)  # ldr x15, [x13]
    ctx.emit(ADRP(16, ctx.here(), 0x8e77000))
    ctx.emit(0x912e4210)  # add x16, x16, #0xb90
    ctx.emit(0xeb1001ff)  # cmp x15, x16
    ctx.emit(0x54000201)  # b.ne next
    ctx.emit(0xb94071af)  # ldr w15, [x13, #0x70]
    ctx.emit(0x6b0e01ff)  # cmp w15, w14
    ctx.emit(0x540001a1)  # b.ne next
    ctx.emit(0x910221a1)  # add x1, x13, #0x88
    ctx.emit(0xf940002f)  # ldr x15, [x1]
    ctx.emit(0xb400018f)  # cbz x15, fallback
    ctx.emit(0xb940082f)  # ldr w15, [x1, #8]
    ctx.emit(0x710005ff)  # cmp w15, #1
    ctx.emit(0x54000129)  # b.ls fallback
    ctx.emit(0x52900010)  # mov w16, #0x8000
    ctx.emit(0x6b1001ff)  # cmp w15, w16
    ctx.emit(0x540000c8)  # b.hi fallback
    ctx.emit(0xf9401be0)  # ldr x0, [sp, #0x30]
    ctx.emit(B(ctx.here(), 0x52a6c18) | 0x80000000)  # bl . // @bl 0x52a6c18
    ctx.emit(0x14000006)  # b done
    ctx.emit(0x1100056b)  # add w11, w11, #1
    ctx.emit(0x17ffffe1)  # b loop
    ctx.emit(0xa94107e0)  # ldp x0, x1, [sp, #0x10]
    ctx.emit(0xa94223e2)  # ldp x2, x8, [sp, #0x20]
    ctx.emit(0xd63f0100)  # blr x8
    ctx.emit(0xa9407bfd)  # ldp x29, x30, [sp]
    ctx.emit(0x910143ff)  # add sp, sp, #0x50
    ctx.emit(0xd65f03c0)  # ret
    ctx.put(0x52b7bd0, [B(0x52b7bd0, selector), 0xd503201f, 0xd503201f],
            'Choose genuine Microsoft primary login only when native networking is enabled; preserve original offline flow')
    ctx.put(0x52a6414, [B(0x52a6414, playfab) | 0x80000000],
            'Forward genuine PlayFab-audience Xbox token to the original login callback')
    ctx.put(0x52a67dc, [B(0x52a67dc, minecraft) | 0x80000000],
            'Forward genuine Minecraft-audience Xbox token to the original login callback')
    _settings_handoff(ctx)
    _settings_complete(ctx, restore)
    _restore_login_call(ctx, restore)
    _restore_logout(ctx, restore)
    _primary_identity_lifetime(ctx)
    _primary_link_status(ctx)


STUBS = {'c6b32f4d3d4243ff190b082b4f6aea18': microsoft_primary}

# Optional DM_AUTH_TRACE observations, passive signed gates. The device-code
# parser gate is after its string extraction call, before the move into user_code.
OBS = {'c6b32f4d3d4243ff190b082b4f6aea18': [
    (0x52c0e3c, 22, 'a9be7bfd', 'Microsoft primary flow entry'),
    (0x4235908, 23, '2a2003f5', 'Real interactive XAL start result'),
    (0x710542c, 24, '394002a8', 'Device user-code parser result (contents never logged)'),
    (0x4243ddc, 25, '2a0003f4', 'Real interactive XAL completion result'),
    (0x52a63f4, 26, 'd503201f', 'PlayFab audience request entry'),
    (0x52a66e4, 27, '12001c08', 'Minecraft audience request entry'),
    (0x52a6ed4, 28, 'f9401e80', 'Original Minecraft response callback entry'),
    (0x52a43ac, 29, '91014320', 'Original login failure entry'),
    (0x44a24f0, 30, '910123e0', 'Original cloud-list request entry'),
    (0x44ab4ec, 31, 'f9401c00', 'Original cloud-list character delivery'),
]}
