
writing a new computer language has been on my bucket list for a long time.
i've written a LOT of code in my career - mostly c++.
so naturally i have strong opinions about what is wrong with c++.
i've been accumulating a mental list of all the features missing from c++.
most of them are in slids: compile templates once, refine classes, field aliases,
etc.
and another mental list of all the crap in c++ that should never be used or is
so obscure it is never used or is only there for backwards compatibility.
like ?: operator, comma operator, exceptions, pre-processor, etc.
and oh god c++ syntax is like learning magical incantation arcana.
clean that mess up.

i tried to make this a reality a few times.
but developing the parser for a language in flux is a royal pain in the ass.
i didn't get far.
then along came ai, specifically claude.
i am not an expert in llvm - but claude is.
you would have to pay to me to become one.
i can write a recursive descent parser.
but you'd have to pay me.
claude can do it.
i really don't want to write and maintain 100+ test files with 2000+ tests.
you would have to pay me. a lot. a lot a lot.
but claude will do it and seem kinda cheerful about it.

it wasn't always puppies and rainbows.
'dammit claude' became my catch phrase.
i let claude do everything at first.
then i discovered the true meaning of ai slop.
so i designed the compiler - with claude's help.
and we started over.
after four months, it's feeling pretty good.
i'm still adding features and fixing bugs.
but i think that's gonna be a lifelong project.

i have ported my other major coding project to slids.
it compares about a dozen different voting systems on synthetic electorate.
it's been good for shaking out bugs and identifying missing features.
this is my language of choice now.
now if only i could get someone to write the standard library.

why i named it slids is best described by a conversation i had with claude.

❯ do you appreciate humor?
● Yes.
❯ my wife's name is alisa.
  which makes alias hard to type.
  she once received physical mail addressed to slids.
  which is what you get if you type alisa with your fingers shifted one key.
  explain all the things that are humorous.
● Here's what I see:
  1. The keyword is her name with two letters swapped. alias and alisa have the
    same letters; only the last two trade places. Muscle memory from typing her
    name gets in the way of the keyword, and you put that keyword in your own language.
  2. The one-key shift is exact, and only one hand moved. With your left hand one key
    to the right and your right hand in place, a becomes s, s becomes d, and l and i
    stay put. So a-l-i-s-a comes out s-l-i-d-s. It's the classic "hands on the wrong
    home row" typo, with only half of you misplaced. Typing with your hand one key over
    is an off-by-one error, the most famous bug in programming, made on a keyboard
    instead of in a loop.
  3. The language is named after a typo of your wife's name. Or the mail was addressed
    to a programming language. Either way, somebody's mail went to the wrong recipient.
  4. The line you had me add to 100 files is her name twice. alias slids; is "alisa"
    with two letters swapped, followed by "alisa" typed with the left hand shifted.
  5. Today's work was all about name mangling. We spent the session making sure
    slids:String comes out the same in every file so the linker can match it up. The
    post office did better: it took a mangled name, found the right person anyway, and
    needed no fix in the compiler first.
