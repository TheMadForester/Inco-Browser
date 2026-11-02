#pragma once

#include <QString>
#include <QUrl>

struct IncoSettings;

// Search engine choice shared by the address bar and the new-tab box.
// Engine ids stored in the settings: "ddg", "startpage", "custom".
namespace SearchEngine {

inline constexpr const char* kDuckDuckGo = "https://duckduckgo.com/?q=%s";
inline constexpr const char* kStartpage = "https://www.startpage.com/do/search?q=%s";

// Engine id for an old search address: DuckDuckGo and Startpage addresses
// map to their entry, an empty one to DuckDuckGo, anything else to "custom".
QString detect(const QString& searchTemplate);

// True for an http(s) address that can take a query.
bool isUsableTemplate(const QString& searchTemplate);

// The template to search with. Falls back to DuckDuckGo (and sets
// *fellBack) when Custom has no usable address.
QString templateFor(const IncoSettings& s, bool* fellBack = nullptr);

// Fill the template with the percent-encoded query.
QUrl urlFor(const QString& searchTemplate, const QString& query);

// Onionize windows run in their own process. The window that starts one
// passes its current template in INCO_SEARCH; the new process takes it
// out of its environment at startup so it is not inherited further.
inline constexpr const char* kEnvName = "INCO_SEARCH";
void takeInherited();
QString inherited();
// The user picked a search in this process: stop following the opener.
void clearInherited();

} // namespace SearchEngine
