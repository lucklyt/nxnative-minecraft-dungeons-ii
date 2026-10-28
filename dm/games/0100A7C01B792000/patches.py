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
    ],
}
