/*
test import and linked files.
this is the second library's source file — extra.slh's sibling.

the negative cases below are the reason this module exists: the completer of
an incomplete class must be able to FOLD the layout it exports.
*/

/*
claude says:

this TU is the completer of extra.slh's BadPack, and it imports library.slh —
so it can spell the one thing a completer must not: a hidden field whose size
lives in ANOTHER module. The exported @BadPack__$sizeof/__$size16 are absolute
symbols (values folded at compile time by llc); an ELF symbol table cannot hold
a relocation, so a hidden `Rope` field (imported incomplete) or a hidden `LPack`
field (a computed-layout class — its own layout rides @Rope__$size16) would make
those exports unmintable. checkOpaqueExportFoldable rejects both at the
completer — an importer could never even see the offending field.

LPack is live (not inside a block) because the second case needs it to exist:
a `.sl`-local class embedding Rope by value, i.e. a COMPUTED layout. It is
otherwise unused.
*/

import string;

import library;

import extra;

/* a local COMPUTED-layout class (embeds the imported Rope by value) — the
   second negative's hidden-field type. Legal here; illegal to HIDE inside an
   incomplete class's completion. */
LPack(int n_ = 1, Rope r_) { }

/*
the completer-unfold rule: completing BadPack with a hidden field this TU
cannot fold. one case per unfoldable flavor — an imported incomplete class,
and a computed-layout class.
*/

//-EXPECT-ERROR: exported layout cannot be a link-time constant
//BadPack(int a_ = 1, Rope r_) { }

//-EXPECT-ERROR: exported layout cannot be a link-time constant
//BadPack(int a_ = 1, LPack p_) { }
