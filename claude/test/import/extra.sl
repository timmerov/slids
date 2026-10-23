/*
Copyright (C) 2026 tim cotter. All rights reserved.
*/

/*
no explicit canon.
*/

/*
claude says:

test import and linked files.
this is the second library's source file — extra.slh's sibling, and Pack's
completer (it defines `_`/`~`).

Pack's hidden fields embed library.slh's Rope two ways — directly (an imported
opaque class) and through LPack (a `.sl`-local computed-layout class over Rope)
— and `z_` sits PAST both, at an offset that is only known once Rope's size is.
So nothing about Pack's layout folds here: its own storage is laid out by the
convention, and its exported @Pack__$sizeof is a word this TU's layout init
computes from @Rope__$sizeof at run time (before any code reads it).
*/

import string;

import library;

import extra;
alias slids;

/* a local COMPUTED-layout class (embeds the imported Rope by value). */
LPack(int n_ = 7, Rope r_) { }

/* Pack's completion: total() reads every hidden field, so a wrong offset or a
   wrong size shows in the consumer's golden. */
Pack(Rope r_, LPack p_, int z_ = 100) {
    _() {}
    ~() {}
    int total() { return a_ + r_.tag() + p_.n_ + z_; }
}
