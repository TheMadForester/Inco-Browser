#pragma once
#include <QHash>
#include <QLocale>
#include <QString>

namespace I18n {

inline QString& currentLang() {
    static QString lang = "en";
    return lang;
}

inline void setLanguage(const QString& code) {
    QString c = code.toLower();
    if (c == "system" || c.isEmpty()) {
        const auto sys = QLocale::system().name().toLower();
        if (sys.startsWith("de")) c = "de";
        else if (sys.startsWith("fr")) c = "fr";
        else if (sys.startsWith("it")) c = "it";
        else if (sys.startsWith("zh")) c = "zh";
        else c = "en";
    }
    if (c != "en" && c != "de" && c != "fr" && c != "it" && c != "zh")
        c = "en";
    currentLang() = c;
}

inline QString tr(const char* key) {
    static const QHash<QString, QString> en = {
        {"app.name", "Inco Browser"},
        {"app.anon", "Inco Browser — Anonymous"},
        {"search.placeholder", "Search privately or enter an address"},
        {"find.placeholder", "Find in page"},
        {"find.next", "Next"},
        {"find.prev", "Prev"},
        {"onionize", "Onionize"},
        {"onion.on", "Onion"},
        {"anon", "Anon"},
        {"settings", "Settings"},
        {"settings.privacy", "Privacy"},
        {"settings.passwords", "Passwords"},
        {"settings.bookmarks", "Bookmarks"},
        {"settings.search", "Search"},
        {"settings.files", "Files"},
        {"settings.anon", "Anonymous sessions"},
        {"settings.language", "Language"},
        {"newtab", "New Tab"},
        {"downloads", "Downloads"},
        {"open.folder", "Open folder"},
        {"hide", "Hide"},
        {"back", "Back"},
        {"forward", "Forward"},
        {"reload", "Reload"},
        {"mute", "Mute tab"},
        {"bookmarks", "Bookmarks"},
        {"open.link", "Open link in new tab"},
        {"copy.link", "Copy link"},
        {"sign.in", "Sign in"},
        {"log.in", "Log in"},
        {"site", "Site"},
        {"username", "Username"},
        {"password", "Password"},
        {"tor.status.missing", "Tor: not installed (bundle tor/tor or system tor)"},
        {"tor.status.idle", "Tor: found, not running"},
        {"tor.status.up", "Tor: SOCKS ready"},
        {"tor.help", "Bundled or system Tor is optional. Inco is not Tor Browser."},
        {"clear.cookies", "Clear cookies when Inco closes"},
        {"clear.cache", "Clear cache when Inco closes"},
        {"block.popups", "Block popups"},
        {"keep.logins", "Keep website logins (persistent cookies)"},
        {"https.only", "HTTPS-only (upgrade http to https)"},
        {"inco.filter", "Inco Filter (block known ad/tracker hosts)"},
        {"filter.extra", "Also block these hosts (one per line)"},
        {"filter.allow", "Never block these hosts"},
        {"wipe.now", "Clear cookies & cache now"},
        {"autofill", "Autofill when vault is unlocked (exact host, one match)"},
        {"change.vault", "Change vault master password"},
        {"show.bookmarks", "Show bookmarks on the new tab page"},
        {"search.url", "Search URL (%s = query)"},
        {"download.folder", "Download folder:"},
        {"route.tor", "Route anonymous windows through system Tor (127.0.0.1:9050)"},
        {"check.tor", "Check for Tor update"},
        {"update.tor", "Update bundled Tor"},
        {"data.note", "No Tor. No VPN. Privacy here means local residue."},
        {"lang.system", "System"},
        {"lang.en", "English"},
        {"lang.de", "Deutsch"},
        {"lang.fr", "Français"},
        {"lang.it", "Italiano"},
        {"lang.zh", "中文"},
    };

    static const QHash<QString, QString> de = {
        {"app.anon", "Inco Browser — Anonym"},
        {"search.placeholder", "Privat suchen oder Adresse eingeben"},
        {"find.placeholder", "Auf Seite suchen"},
        {"find.next", "Weiter"},
        {"find.prev", "Zurück"},
        {"onionize", "Onionize"},
        {"onion.on", "Onion"},
        {"settings", "Einstellungen"},
        {"settings.privacy", "Privatsphäre"},
        {"settings.passwords", "Passwörter"},
        {"settings.bookmarks", "Lesezeichen"},
        {"settings.search", "Suche"},
        {"settings.files", "Dateien"},
        {"settings.anon", "Anonyme Sitzungen"},
        {"settings.language", "Sprache"},
        {"newtab", "Neuer Tab"},
        {"downloads", "Downloads"},
        {"open.folder", "Ordner öffnen"},
        {"hide", "Verstecken"},
        {"back", "Zurück"},
        {"forward", "Vor"},
        {"reload", "Neu laden"},
        {"mute", "Tab stumm"},
        {"bookmarks", "Lesezeichen"},
        {"open.link", "Link in neuem Tab öffnen"},
        {"copy.link", "Link kopieren"},
        {"sign.in", "Anmelden"},
        {"log.in", "Einloggen"},
        {"site", "Seite"},
        {"username", "Benutzername"},
        {"password", "Passwort"},
        {"tor.status.missing", "Tor: nicht installiert"},
        {"tor.status.idle", "Tor: gefunden, nicht gestartet"},
        {"tor.status.up", "Tor: SOCKS bereit"},
        {"tor.help", "Bundled oder System-Tor ist optional. Inco ist kein Tor Browser."},
        {"clear.cookies", "Cookies löschen wenn Inco schließt"},
        {"clear.cache", "Cache löschen wenn Inco schließt"},
        {"block.popups", "Popups blockieren"},
        {"keep.logins", "Website-Logins behalten"},
        {"https.only", "Nur HTTPS"},
        {"inco.filter", "Inco Filter (bekannte Tracker blockieren)"},
        {"filter.extra", "Zusätzlich diese Hosts blockieren"},
        {"filter.allow", "Diese Hosts nie blockieren"},
        {"wipe.now", "Cookies & Cache jetzt löschen"},
        {"autofill", "Autofill wenn Tresor entsperrt"},
        {"change.vault", "Master-Passwort ändern"},
        {"show.bookmarks", "Lesezeichen auf New Tab zeigen"},
        {"search.url", "Such-URL (%s = Anfrage)"},
        {"download.folder", "Download-Ordner:"},
        {"route.tor", "Anonyme Fenster über System-Tor routen"},
        {"check.tor", "Nach Tor-Update prüfen"},
        {"update.tor", "Bundled Tor aktualisieren"},
        {"data.note", "Kein Tor. Kein VPN. Privatsphäre hier = lokale Spuren."},
        {"lang.system", "System"},
        {"lang.de", "Deutsch"},
    };

    static const QHash<QString, QString> fr = {
        {"search.placeholder", "Rechercher en privé ou entrer une adresse"},
        {"find.placeholder", "Rechercher dans la page"},
        {"settings", "Paramètres"},
        {"settings.privacy", "Confidentialité"},
        {"settings.passwords", "Mots de passe"},
        {"settings.bookmarks", "Signets"},
        {"newtab", "Nouvel onglet"},
        {"downloads", "Téléchargements"},
        {"back", "Retour"},
        {"reload", "Actualiser"},
        {"open.link", "Ouvrir le lien dans un nouvel onglet"},
        {"copy.link", "Copier le lien"},
        {"sign.in", "Connexion"},
        {"username", "Nom d'utilisateur"},
        {"password", "Mot de passe"},
        {"lang.fr", "Français"},
    };

    static const QHash<QString, QString> it_ = {
        {"search.placeholder", "Cerca in privato o inserisci un indirizzo"},
        {"find.placeholder", "Trova nella pagina"},
        {"settings", "Impostazioni"},
        {"settings.privacy", "Privacy"},
        {"settings.passwords", "Password"},
        {"settings.bookmarks", "Segnalibri"},
        {"newtab", "Nuova scheda"},
        {"downloads", "Download"},
        {"back", "Indietro"},
        {"reload", "Ricarica"},
        {"open.link", "Apri link in nuova scheda"},
        {"copy.link", "Copia link"},
        {"sign.in", "Accedi"},
        {"username", "Nome utente"},
        {"password", "Password"},
        {"lang.it", "Italiano"},
    };

    static const QHash<QString, QString> zh = {
        {"search.placeholder", "私密搜索或输入地址"},
        {"find.placeholder", "在页面中查找"},
        {"settings", "设置"},
        {"settings.privacy", "隐私"},
        {"settings.passwords", "密码"},
        {"settings.bookmarks", "书签"},
        {"newtab", "新标签页"},
        {"downloads", "下载"},
        {"back", "后退"},
        {"reload", "刷新"},
        {"open.link", "在新标签页中打开链接"},
        {"copy.link", "复制链接"},
        {"sign.in", "登录"},
        {"username", "用户名"},
        {"password", "密码"},
        {"lang.zh", "中文"},
    };

    const QString k = QString::fromUtf8(key);
    const QString& lang = currentLang();

    auto lookup = [&](const QHash<QString, QString>& t) -> QString {
        auto it = t.constFind(k);
        return it != t.cend() ? it.value() : QString();
    };

    if (lang == "de") { auto v = lookup(de); if (!v.isEmpty()) return v; }
    if (lang == "fr") { auto v = lookup(fr); if (!v.isEmpty()) return v; }
    if (lang == "it") { auto v = lookup(it_); if (!v.isEmpty()) return v; }
    if (lang == "zh") { auto v = lookup(zh); if (!v.isEmpty()) return v; }

    auto it = en.constFind(k);
    return it != en.cend() ? it.value() : k;
}

} // namespace I18n
