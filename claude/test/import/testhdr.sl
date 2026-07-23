/*
test import and linked files.
this stub file is used to run the negative tests in library.slh.
*/

/*
claude says:

WHY THIS FILE EXISTS: a header is never compiled, it is IMPORTED. so a negative case in
library.slh needs some .sl to pull it in, and this stub is it — its whole body is the
import.

HOW THE RUNNER USES IT (run_negatives.sh): each case gets its own temp dir holding the
uncommented library.slh variant, and THIS FILE IS COPIED IN BESIDE IT. that placement is
required, not tidiness: `import X;` searches the importing source's OWN directory FIRST
and only then each -I dir, so a stub compiled from anywhere else imports the REAL header
and the variant is never read. (that is exactly what happened on the first attempt — five
negatives reported as passing compilation while slidsc was reading the real library.slh.)

this file is deliberately NOT a SAMPLE in the Makefile: it has no main and no golden.
it is only ever compiled by the negative runner, one throwaway copy per case.
*/

import library;
import tmpl_lib;
