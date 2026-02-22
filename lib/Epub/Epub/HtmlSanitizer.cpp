#include "HtmlSanitizer.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Convert tag name to lowercase in-place for case-insensitive matching
void toLower(std::string& s) {
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') c += 32;
  }
}

// Tags that are self-closing in HTML and should never be pushed onto the stack
bool isSelfClosingTag(const std::string& tag) {
  return tag == "br" || tag == "hr" || tag == "img" || tag == "input" || tag == "meta" || tag == "link" ||
         tag == "area" || tag == "base" || tag == "col" || tag == "embed" || tag == "source" || tag == "track" ||
         tag == "wbr";
}

}  // namespace

bool sanitizeHtmlFile(const std::string& path) {
  FsFile readFile;
  if (!Storage.openFileForRead("SAN", path, readFile)) {
    LOG_ERR("SAN", "Failed to open file for reading: %s", path.c_str());
    return false;
  }

  const size_t fileSize = readFile.size();
  std::string content;
  content.resize(fileSize);
  const size_t bytesRead = readFile.read(&content[0], fileSize);
  readFile.close();

  if (bytesRead != fileSize) {
    LOG_ERR("SAN", "Short read: got %u of %u bytes", bytesRead, fileSize);
    return false;
  }

  std::string output;
  output.reserve(fileSize);

  std::vector<std::string> tagStack;
  bool modified = false;

  size_t i = 0;
  const size_t len = content.size();

  while (i < len) {
    if (content[i] != '<') {
      output += content[i++];
      continue;
    }

    // We're at '<' — figure out what kind of tag/construct this is

    // Comment: <!-- ... -->
    if (i + 3 < len && content[i + 1] == '!' && content[i + 2] == '-' && content[i + 3] == '-') {
      size_t end = content.find("-->", i + 4);
      if (end == std::string::npos) {
        // Unterminated comment — copy rest as-is
        output.append(content, i, len - i);
        i = len;
      } else {
        end += 3;  // include "-->"
        output.append(content, i, end - i);
        i = end;
      }
      continue;
    }

    // Processing instruction: <?...?>
    if (i + 1 < len && content[i + 1] == '?') {
      size_t end = content.find("?>", i + 2);
      if (end == std::string::npos) {
        output.append(content, i, len - i);
        i = len;
      } else {
        end += 2;
        output.append(content, i, end - i);
        i = end;
      }
      continue;
    }

    // DOCTYPE / CDATA: <!...>
    if (i + 1 < len && content[i + 1] == '!') {
      size_t end = content.find('>', i + 2);
      if (end == std::string::npos) {
        output.append(content, i, len - i);
        i = len;
      } else {
        end += 1;
        output.append(content, i, end - i);
        i = end;
      }
      continue;
    }

    // Find the end of this tag
    size_t tagEnd = content.find('>', i + 1);
    if (tagEnd == std::string::npos) {
      // No closing '>' — copy rest as-is
      output.append(content, i, len - i);
      i = len;
      continue;
    }

    const std::string tagRaw = content.substr(i, tagEnd - i + 1);
    const size_t tagRawLen = tagRaw.size();

    // Self-closing tag like <br/> or <img ... />
    if (tagRawLen >= 3 && tagRaw[tagRawLen - 2] == '/') {
      output += tagRaw;
      i = tagEnd + 1;
      continue;
    }

    // Closing tag: </foo>
    if (tagRawLen >= 4 && tagRaw[1] == '/') {
      // Extract tag name (between "</" and first space or ">")
      size_t nameStart = 2;
      size_t nameEnd = nameStart;
      while (nameEnd < tagRawLen - 1 && tagRaw[nameEnd] != ' ' && tagRaw[nameEnd] != '\t' &&
             tagRaw[nameEnd] != '\n' && tagRaw[nameEnd] != '\r') {
        nameEnd++;
      }
      std::string tagName = tagRaw.substr(nameStart, nameEnd - nameStart);
      toLower(tagName);

      if (tagStack.empty()) {
        // Nothing open — drop this closing tag
        LOG_DBG("SAN", "Dropping orphan </%s>", tagName.c_str());
        modified = true;
        i = tagEnd + 1;
        continue;
      }

      if (tagStack.back() == tagName) {
        // Normal match — pop and emit
        tagStack.pop_back();
        output += tagRaw;
        i = tagEnd + 1;
        continue;
      }

      // Check if it exists deeper in the stack
      bool found = false;
      for (int si = static_cast<int>(tagStack.size()) - 1; si >= 0; si--) {
        if (tagStack[si] == tagName) {
          found = true;
          // Auto-close everything above the match
          for (int ci = static_cast<int>(tagStack.size()) - 1; ci > si; ci--) {
            LOG_DBG("SAN", "Auto-closing <%s> before </%s>", tagStack[ci].c_str(), tagName.c_str());
            output += "</" + tagStack[ci] + ">";
            modified = true;
          }
          tagStack.erase(tagStack.begin() + si, tagStack.end());
          output += tagRaw;
          i = tagEnd + 1;
          break;
        }
      }

      if (!found) {
        // Tag not in stack at all — drop it
        LOG_DBG("SAN", "Dropping unmatched </%s>", tagName.c_str());
        modified = true;
        i = tagEnd + 1;
      }
      continue;
    }

    // Opening tag: <foo ...>
    {
      size_t nameStart = 1;
      size_t nameEnd = nameStart;
      while (nameEnd < tagRawLen - 1 && tagRaw[nameEnd] != ' ' && tagRaw[nameEnd] != '\t' &&
             tagRaw[nameEnd] != '\n' && tagRaw[nameEnd] != '\r' && tagRaw[nameEnd] != '/' &&
             tagRaw[nameEnd] != '>') {
        nameEnd++;
      }
      std::string tagName = tagRaw.substr(nameStart, nameEnd - nameStart);
      toLower(tagName);

      output += tagRaw;
      if (!tagName.empty() && !isSelfClosingTag(tagName)) {
        tagStack.push_back(tagName);
      }
      i = tagEnd + 1;
    }
  }

  if (!modified) {
    return false;
  }

  LOG_DBG("SAN", "Sanitized HTML, writing back to %s", path.c_str());

  FsFile writeFile;
  if (!Storage.openFileForWrite("SAN", path, writeFile)) {
    LOG_ERR("SAN", "Failed to open file for writing: %s", path.c_str());
    return false;
  }
  writeFile.write(reinterpret_cast<const uint8_t*>(output.data()), output.size());
  writeFile.close();
  return true;
}
