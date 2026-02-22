#pragma once

#include <string>

// Sanitize an HTML/XHTML file on disk to fix mismatched tags that would cause
// Expat (strict XML parser) to fail.  Reads the file, drops orphan closing tags,
// auto-closes intermediates for cross-nested tags, and writes corrected content
// back to the same path.  Returns true if any fixes were applied.
bool sanitizeHtmlFile(const std::string& path);
