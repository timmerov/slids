
claude is an active agent.
i want it to behave like a passive tool.
this is surpsisingly difficult.

claude has two parts: the cli harness, and the ai.

the cli harness stores data in several places:
this is mostly account info and authentication.
mostly don't touch.
~/.claude.json
this is project info history and backups.
most of it can be safely deleted.
~/.claude/
with the exception of the settings.
this is where you put rules for the harness.
the settings are arcane.
claude ai is actually pretty helpful here.
~/.claude/settings.json
local settings override global settings.
and store the always allow permissions.
./.claude/settings.local.json
the harness injects things into the session at start.

claude the ai is mostly in here.
./CLAUDE.md
there is a RULES section.
claude is pretty good at following these rules to the letter.
everything else is suggestion.
claude is pretty good at reviewing its own rules.
and offering suggestions for phrasing.
and removing conflicts.
the you-are-a-passive-tool instructions are rules in here.
suggestions aren't always followed.

i moved claude into a subdirectory.
expected everything to just work.
silly me.
that's how i found the above listed files.
i deleted the safe ones.
and changed the hard coded directories in others.

i really want to limit the files claude can access.
there's a rule in CLAUDE.md.
but claude was still accessing files outside its directory.
i added rules to settings.json.
that helped but see below.
but claude still went out of bounds.
it told me about the harness when asked for explanation.
it turns out, the harness injects git status.
and claude really likes to use git.
i don't want claude to use git. at all. ever.
it would have to make assumptions about the current git state.
which are 99.44% likely to be wrong.
and a waste of tokens.
the git infor is a way for claude to jailbreak its sandbox.
fortunately, there's a setting.

this is my annotated settings.json:
{
    /*
    tell the harness not inject git information into the session.
    */
    "includeGitInstructions": false,

    "permissions": {
        "allow": [
            /*
            both of these are required.
            allow trumps deny.
            these give claude ai access to files in its sandbox.
            */
            "Read(/home/timmer/Documents/code/slids/claude/**)",
            "Edit(/home/timmer/Documents/code/slids/claude/**)"
        ],
        "deny": [
            /*
            this seems to actually automatically fail git usage.
            CLAUDE.md says don't use git.
            and claude ai generally doesn't.
            i tested without that RULE.
            claude trying to use git when instructed is an
            automatic failure.
            as desired.
            */
            "Bash(git *)",
            "Bash(git:*)",

            /*
            this doesn't mean claude ai cannot access outside files.
            ie automatic failure.
            that doesn't seem to be an option. fuckers.
            this means get permission from the user to access the files.
            so now you have to pay attention to what claude's accessing.
            fortunately, claude ai follows its RULES pretty well.
            see CLAUDE.md.
            this is in case claude ai escapes.
            */
            "Read(/**/*)"
        ]
    },

    /*
    the sandbox doesn't actually work.
    something something sysclt user permissions for bwrap bubble wrap.
    also something about a root level directory.
    also probably don't want to enable it.
    cause it doesn't handle compound bash commands well.
    like pipe | and chaining &&.

    otoh, it's real good at what it does:
    auto-fail git.
    and auto-fail access to out of bounds files.
    */

    /*
    this seems redudant.
    but claude ai insists its necessary.
    it's another way to keep claude in it's sandbox.
    it requires socat to be installed.
    or the harness will immediately disable it.
    $ sudo apt install socat
    */
    "sandbox": {
        "enabled": true,
        "filesystem": {
            "denyRead": ["/**"],
            "allowRead": ["/home/timmer/Documents/code/slids/claude/**"]
        }
    }
}
