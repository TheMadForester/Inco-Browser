#pragma once

#include "SettingsStore.hpp"
#include "UrlInterceptor.hpp"

#include <QObject>

class QWebEngineProfile;

class BrowserProfile : public QObject
{
    Q_OBJECT

public:
    explicit BrowserProfile(bool ephemeral = false, QObject* parent = nullptr);
    bool isEphemeral() const { return m_ephemeral; }

    QWebEngineProfile* engine() const { return m_profile; }
    void apply(const IncoSettings& settings);
    void wipeOnExit(const IncoSettings& settings);

private:
    QWebEngineProfile* m_profile = nullptr;
    UrlInterceptor* m_blocker = nullptr;
    bool m_filterOn = true;
    bool m_ephemeral = false;
};
