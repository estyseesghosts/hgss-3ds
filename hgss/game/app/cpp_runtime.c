/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The small C++ runtime surface HGSS's lobby references.
 *
 * The lobby is the game's only C++ code (the .cpp files under src/lobby). Metrowerks' C++
 * compiler emitted a handful of runtime helpers next to it; the port builds
 * those same files with GCC, -fno-exceptions and -fno-rtti, and does not link
 * libstdc++ (it would pull a second allocator, exception tables and a
 * different new/delete set into a game that uses neither).
 *
 * The two out-of-line destructors are here as well. MWCC emitted them next to
 * the class definitions (ppw_lobby_update_my_profile.cpp,
 * lobby_event_deque_add_back_capacity.cpp) and the decomp's own build recovers
 * them with tools/object_aliases.py from the .alias-map.json files beside
 * ppw_lobby.cpp and lobby_event_deque_destroy.c: both maps alias the
 * destructor to the C destroy routine at offset 0
 * (`LobbyCharVector_Destroy` / `LobbyEventDeque_Destroy2`). GCC does not emit
 * those destructors, and the alias maps pin MWCC's function sizes, which GCC's
 * larger code does not match, so the two symbols are defined here and the
 * harness skips exactly those two map entries. Calling the same C destroy
 * routine is the aliased behaviour, not an approximation.
 *
 * What remains is the storage surface the lobby uses directly:
 *
 * - `_ZnamRKSt9nothrow_t` is `operator new[](unsigned int, std::nothrow_t
 *   const&)`. lobby_event_vector_assign_range.c calls it through that mangled
 *   C name and checks the result for NULL, so it must return NULL, never
 *   throw or abort. Zero-size requests get one byte, so the result is a unique
 *   non-NULL pointer like a real C++ allocation.
 * - `_ZdaPv` is `operator delete[](void*)`; the matching free.
 * - `_ZNSt15__vector_commonILb1EE18throw_length_errorEv` is MSL's
 *   `std::__vector_common<true>::throw_length_error()`. The port is built with
 *   -fno-exceptions, so there is no exception to throw: print a clear message
 *   and abort. The lobby's growth routines call it only when a capacity
 *   computation would overflow, which is a programming error, not a game
 *   state.
 * - `_ZSt7nothrow` is `std::nothrow`; the game declares it `extern char
 *   _ZSt7nothrow[]` and only passes its address, so one byte suffices.
 *
 * These are program objects (tools/hgss_game.py app_sources), not archive
 * members: they must only exist in the linked program, next to the game code
 * that calls the mangled names.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* `operator new[]`'s size parameter: size_t on the target (32-bit). */
typedef uint32_t hgss_size_t;

/*
 * The lobby's C destroy routines. The class layouts put the vector/deque
 * storage at offset 0, so the C++ `this` pointer is exactly the pointer these
 * routines take.
 */
extern void *LobbyCharVector_Destroy(void *vec);
extern void LobbyEventDeque_Destroy2(void *deque);

/*
 * Bytes::~Bytes(): MWCC emitted this at LobbyCharVector_Destroy's address
 * (ppw_lobby.alias-map.json maps `_ZN5BytesD1Ev` to it at offset 0), and
 * LobbyCharVector_Destroy is the documented destroy path for the VecStorage at
 * offset 0. Same object, same operation.
 */
void _ZN5BytesD1Ev(void *self)
{
    (void)LobbyCharVector_Destroy(self);
}

/*
 * Deque::~Deque(): the same arrangement in
 * lobby_event_deque_destroy.alias-map.json (`_ZN5DequeD1Ev` aliases
 * LobbyEventDeque_Destroy2 at offset 0).
 */
void _ZN5DequeD1Ev(void *self)
{
    LobbyEventDeque_Destroy2(self);
}

/*
 * operator new[](size_t, std::nothrow_t const&).
 *
 * malloc, NULL on failure, and a successful zero-size allocation returns a
 * unique one-byte block rather than NULL so `new (nothrow) T[0]` behaves like
 * the standard says. The second argument is only the tag object's address and
 * is not read.
 */
void *_ZnamRKSt9nothrow_t(hgss_size_t size, const void *nothrowTag)
{
    (void)nothrowTag;
    if (size == 0) {
        size = 1;
    }
    return malloc(size);
}

/*
 * operator delete[](void*). free(NULL) is a no-op; the game only passes
 * pointers from _ZnamRKSt9nothrow_t here (lobby_event_vector_assign_range.c's
 * buffer release path).
 */
void _ZdaPv(void *pointer)
{
    free(pointer);
}

/*
 * std::__vector_common<true>::throw_length_error() for a -fno-exceptions
 * build: report the overflow and stop. This is never reached by normal game
 * data; reaching it means a lobby vector's capacity arithmetic overflowed.
 */
__attribute__((noreturn))
void _ZNSt15__vector_commonILb1EE18throw_length_errorEv(void)
{
    fputs("HGSS lobby: std::__vector_common<true>::throw_length_error "
          "(vector capacity overflow; the port is built without exceptions)\n",
          stderr);
    abort();
}

/*
 * std::nothrow. The game declares `extern char _ZSt7nothrow[]` and passes the
 * address as the tag argument; nothing reads or writes the byte.
 */
char _ZSt7nothrow[1] = { 0 };
