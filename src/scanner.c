#include "tree_sitter/parser.h"

#include <wctype.h>

// Must match the order of externals($) in grammar.js
enum TokenType {
  COMMENT,
  TOPLEVEL_NEWLINE,
  INTERNAL_NEWLINE,
  BRACKET_OPEN,
  BRACKET_CLOSE,
  ERROR_SENTINEL,  // never used in grammar — true only during error recovery
};

typedef struct {
  uint8_t bracket_depth;
} Scanner;

static bool scan_comment(TSLexer *lexer) {
  int depth = 1;

  while (!lexer->eof(lexer)) {
    switch (lexer->lookahead) {
      case '*':
        lexer->advance(lexer, false);
        if (lexer->lookahead == ')') {
          depth--;
          if (depth == 0) {
            lexer->result_symbol = COMMENT;
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            return true;
          }
        }
        break;

      case '(':
        lexer->advance(lexer, false);
        if (lexer->lookahead == '*') {
          depth++;
        }
        break;

      default:
        lexer->advance(lexer, false);
        break;
    }
  }

  return false;
}

void *tree_sitter_wolfram_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  return scanner;
}

void tree_sitter_wolfram_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_wolfram_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  buffer[0] = (char)scanner->bracket_depth;
  return 1;
}

void tree_sitter_wolfram_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  if (length > 0) {
    scanner->bracket_depth = (uint8_t)buffer[0];
  } else {
    scanner->bracket_depth = 0;
  }
}

bool tree_sitter_wolfram_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  // During error recovery tree-sitter sets all valid_symbols to true.
  // The error sentinel is never used in the grammar, so it's only true
  // during error recovery.
  if (valid_symbols[ERROR_SENTINEL]) {
    // Still handle comments during error recovery
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\r' || lexer->lookahead == '\n') {
      lexer->advance(lexer, true);
    }
    if (lexer->lookahead == '(') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        return scan_comment(lexer);
      }
    }
    return false;
  }

  // Zero-width bracket tracking tokens.
  // Only emit when an actual bracket character follows.
  if (valid_symbols[BRACKET_OPEN]) {
    char c = lexer->lookahead;
    if (c == '[' || c == '(' || c == '{' || c == '<' || c == '=') {
      if (scanner->bracket_depth < 255) scanner->bracket_depth++;
      lexer->result_symbol = BRACKET_OPEN;
      lexer->mark_end(lexer);
      return true;
    }
  }

  if (valid_symbols[BRACKET_CLOSE]) {
    if (scanner->bracket_depth > 0) scanner->bracket_depth--;
    lexer->result_symbol = BRACKET_CLOSE;
    lexer->mark_end(lexer);
    return true;
  }

  // Skip non-newline whitespace
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
    lexer->advance(lexer, true);
  }

  // Handle newlines
  if (lexer->lookahead == '\n') {
    // Consume all consecutive newlines and whitespace
    while (lexer->lookahead == '\n' || lexer->lookahead == ' ' ||
           lexer->lookahead == '\t' || lexer->lookahead == '\r') {
      lexer->advance(lexer, true);
    }
    lexer->mark_end(lexer);

    // Emit toplevel newline at bracket depth 0 when the parser allows it
    if (scanner->bracket_depth == 0 && valid_symbols[TOPLEVEL_NEWLINE]) {
      lexer->result_symbol = TOPLEVEL_NEWLINE;
      return true;
    }

    // Otherwise emit internal newline (in extras, silently consumed).
    // This commits the consumed newline characters so the main lexer
    // doesn't choke on them.
    lexer->result_symbol = INTERNAL_NEWLINE;
    return true;
  }

  // (* comment
  if (lexer->lookahead == '(') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '*') {
      lexer->advance(lexer, false);
      return scan_comment(lexer);
    }
  }

  return false;
}
