// moria_join_assets.h — placeholder header (v6.23.0 cleanup)
//
// Original purpose: asset-path constants for a v6.6.0 spawn-duplicate
// JoinWorld path. That approach was abandoned in favor of in-place
// widget modification, leaving the entire MoriaJoinAssets:: namespace
// orphaned with zero callers across the source tree.
//
// v6.22.5 wrapped the namespace in #if 0 (Pass 1). v6.23.0 (this file)
// completes Pass 2 by deleting the body. The header itself stays
// because dllmain.cpp:10 still includes it; removing the include is a
// separate trivial cleanup left for later if anyone notices.

#pragma once
