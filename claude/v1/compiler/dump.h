#pragma once
#include "parser.h"
#include "source_map.h"
#include <iosfwd>
#include <string>

// Deterministic structural dump of a parsed Program. Used by the
// legacy-diff snapshot harness to verify parser behavior parity
// across the frame-based rewrite. Output format: indented YAML-ish,
// one fact per line, top-level entries carry file=/tok=, maps sorted,
// empty sections omitted. Paths are emitted as given to the parser
// (relative to slidsc's cwd) so make-driven runs stay deterministic.
void dumpProgram(const Program& prog, const SourceMap& sm, std::ostream& os);
