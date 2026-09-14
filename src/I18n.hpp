#pragma once
#include <QHash>
#include <QString>

// English only for now. Later: load .qm and swap these.
namespace I18n {

inline QString tr(const char* key)
{
    static const auto table = [] {
        QHash<QString, QString> m;
        m["app.name"] = "Inco Browser";
        m["app.anon"] = "Inco Browser — Anonymous";
        m["search.placeholder"] = "Search privately or enter an address";
        m["home.search"] = "Search privately";
        m["home.private"] = "What means Private";
        m["onionize"] = "Onionize";
        m["onion.on"] = "Onion";
        m["settings"] = "Settings";
        m["settings.privacy"] = "Privacy";
        m["settings.search"] = "Search";
        m["settings.files"] = "Files";
        m["settings.anon"] = "Anonymous sessions";
        m["tor.status.missing"] = "Tor: not installed (bundle tor/tor or system tor)";
        m["tor.status.idle"] = "Tor: found, not running";
        m["tor.status.up"] = "Tor: SOCKS ready";
        m["tor.help"] = "Bundled or system Tor is optional. Inco is not Tor Browser.";
        m["newtab"] = "New Tab";
        return m;
    }();
    const auto it = table.constFind(QString::fromUtf8(key));
    return it == table.cend() ? QString::fromUtf8(key) : it.value();
}

}
