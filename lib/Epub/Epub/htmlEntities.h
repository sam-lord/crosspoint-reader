// from
// https://github.com/atomic14/diy-esp32-epub-reader/blob/2c2f57fdd7e2a788d14a0bcb26b9e845a47aac42/lib/Epub/RubbishHtmlParser/htmlEntities.cpp

#pragma once
#include <string>

// Lookup a single HTML entity (including & and ;) and return its UTF-8 value
// Returns nullptr if entity is not found
const char* lookupHtmlEntity(const char* entity, int len);

// Decode all numeric character references (&#123; or &#xAB;) in a string
// Also handles named entities like &nbsp;
// Returns a new string with entities decoded
std::string decodeHtmlEntities(const std::string& input);
