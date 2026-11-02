#include "OnionRoute.hpp"
#include "I18n.hpp"
#include "Motion.hpp"

#include <QApplication>
#include <cmath>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QUrlQuery>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {

QNetworkRequest torRequest(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    return req;
}

} // namespace

QNetworkAccessManager* makeTorNetwork(QObject* parent, quint16 socksPort)
{
    auto* nam = new QNetworkAccessManager(parent);
    // Socks5Proxy resolves host names on the proxy side, so DNS goes over Tor too.
    nam->setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, QStringLiteral("127.0.0.1"), socksPort));
    return nam;
}

void probeTorExit(QNetworkAccessManager* nam, std::function<void(const TorExitInfo&)> done)
{

    auto* reply = nam->get(torRequest(QUrl(QStringLiteral("https://check.torproject.org/api/ip"))));
    QObject::connect(reply, &QNetworkReply::finished, nam, [nam, reply, done] {
        TorExitInfo info;
        info.when = QDateTime::currentDateTime();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            info.error = reply->errorString();
            done(info);
            return;
        }
        const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
        info.ok = true;
        info.isTor = obj.value(QLatin1String("IsTor")).toBool();
        info.ip = obj.value(QLatin1String("IP")).toString();
        if (!info.isTor || info.ip.isEmpty()) {
            done(info);
            return;
        }
        // Public relay directory: who runs this exit and where.
        QUrl url(QStringLiteral("https://onionoo.torproject.org/details"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("search"), info.ip);
        q.addQueryItem(QStringLiteral("type"), QStringLiteral("relay"));
        q.addQueryItem(QStringLiteral("fields"), QStringLiteral("nickname,country_name,as_name"));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
        url.setQuery(q);
        auto* r2 = nam->get(torRequest(url));
        QObject::connect(r2, &QNetworkReply::finished, nam, [r2, done, info]() mutable {
            r2->deleteLater();
            if (r2->error() == QNetworkReply::NoError) {
                const auto relays = QJsonDocument::fromJson(r2->readAll()).object()
                                        .value(QLatin1String("relays")).toArray();
                if (!relays.isEmpty()) {
                    const auto r = relays.first().toObject();
                    info.nickname = r.value(QLatin1String("nickname")).toString();
                    info.country = r.value(QLatin1String("country_name")).toString();
                    info.asName = r.value(QLatin1String("as_name")).toString();
                }
            }
            done(info);
        });
    });
}

// Vertical route: one node per hop, a rail between them and a dot that
// travels down the rail while the route is confirmed.
class RouteView final : public QWidget
{
public:
    struct Node {
        QString title;
        QString detail;
        bool known = true;     // drawn solid; unknown hops get a dashed ring
        bool accent = false;   // amber (the exit we actually measured)
    };

    explicit RouteView(QWidget* parent = nullptr) : QWidget(parent)
    {
        m_anim = new QVariantAnimation(this);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setDuration(2400);
        m_anim->setLoopCount(-1);
        m_anim->setEasingCurve(QEasingCurve::InOutSine);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this] { update(); });
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    void setNodes(const QVector<Node>& nodes, bool flowing, bool pulsing)
    {
        m_nodes = nodes;
        m_flowing = flowing;
        m_pulsing = pulsing;
        relayout();
        syncAnimation();
        update();
    }

    QSize sizeHint() const override { return {width() > 0 ? width() : 340, m_height}; }

    void syncAnimation()
    {
        const bool want = isVisible() && (m_flowing || m_pulsing);
        if (want && m_anim->state() != QAbstractAnimation::Running)
            m_anim->start();
        else if (!want)
            m_anim->stop();
    }

protected:
    void resizeEvent(QResizeEvent*) override { relayout(); }
    void showEvent(QShowEvent*) override { syncAnimation(); }
    void hideEvent(QHideEvent*) override { m_anim->stop(); }

    void paintEvent(QPaintEvent*) override
    {
        if (m_nodes.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal cx = 11.5;
        const qreal t = m_anim->state() == QAbstractAnimation::Running
            ? m_anim->currentValue().toReal() : 0.0;

        // Rail.
        const qreal top = m_y.first() + 9, bottom = m_y.last() + 9;
        p.setPen(QPen(QColor("#2c2c2c"), 2));
        p.drawLine(QPointF(cx, top), QPointF(cx, bottom));
        if (m_flowing) {
            const qreal y = top + (bottom - top) * t;
            QLinearGradient g(0, y - 26, 0, y);
            g.setColorAt(0, QColor(230, 192, 123, 0));
            g.setColorAt(1, QColor(230, 192, 123, 150));
            p.setPen(QPen(QBrush(g), 2));
            p.drawLine(QPointF(cx, qMax(top, y - 26)), QPointF(cx, y));
        }

        QFont titleFont = font();
        titleFont.setPixelSize(13);
        titleFont.setWeight(QFont::DemiBold);
        QFont detailFont = font();
        detailFont.setPixelSize(12);

        for (int i = 0; i < m_nodes.size(); ++i) {
            const auto& n = m_nodes[i];
            const QPointF c(cx, m_y[i] + 9);
            QColor ring = n.accent ? QColor("#e6c07b") : QColor("#5a5a5a");
            QColor fill = n.accent ? QColor("#3a2a12") : QColor("#1f1f1f");
            qreal r = 6.5;
            if (n.accent && m_pulsing)
                r += 1.5 * std::sin(t * 3.14159265);
            p.setBrush(fill);
            QPen pen(ring, 1.6);
            if (!n.known)
                pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawEllipse(c, r, r);

            const int x = 30;
            const int w = width() - x;
            p.setPen(QColor(n.accent ? "#f0d9a0" : "#e4e4e4"));
            p.setFont(titleFont);
            p.drawText(QRect(x, m_y[i], w, m_titleH), Qt::AlignLeft | Qt::AlignVCenter, n.title);
            p.setPen(QColor("#8a8a8a"));
            p.setFont(detailFont);
            p.drawText(QRect(x, m_y[i] + m_titleH + 1, w, m_detailH[i]),
                       Qt::AlignLeft | Qt::TextWordWrap, n.detail);
        }

        if (m_flowing) {
            const qreal y = top + (bottom - top) * t;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(230, 192, 123, 60));
            p.drawEllipse(QPointF(cx, y), 6, 6);
            p.setBrush(QColor("#e6c07b"));
            p.drawEllipse(QPointF(cx, y), 3, 3);
        }
    }

private:
    void relayout()
    {
        QFont detailFont = font();
        detailFont.setPixelSize(12);
        const QFontMetrics fm(detailFont);
        const int w = qMax(120, (width() > 0 ? width() : 340) - 30);
        m_titleH = 18;
        m_y.clear();
        m_detailH.clear();
        int y = 0;
        for (const auto& n : m_nodes) {
            const int dh = n.detail.isEmpty() ? 0
                : fm.boundingRect(QRect(0, 0, w, 1000), Qt::TextWordWrap, n.detail).height();
            m_y.push_back(y);
            m_detailH.push_back(dh);
            y += m_titleH + 1 + dh + 14;
        }
        const int h = qMax(0, y - 14);
        if (h != m_height) {
            m_height = h;
            setFixedHeight(h);
            updateGeometry();
        }
    }

    QVector<Node> m_nodes;
    QVector<int> m_y;
    QVector<int> m_detailH;
    int m_titleH = 18;
    int m_height = 0;
    bool m_flowing = false;
    bool m_pulsing = false;
    QVariantAnimation* m_anim = nullptr;
};

OnionRoutePanel::OnionRoutePanel(bool tor, QWidget* parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_tor(tor)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName("onionRoute");
    setStyleSheet(QStringLiteral(
        "#onionRoute QLabel { background: transparent; }"
        "#onionRoute QLabel#routeTitle { color: #f0f0f0; font-size: 15px; font-weight: 600; }"
        "#onionRoute QLabel#routeNote { color: #6f6f6f; font-size: 11px; }"
        "#onionRoute QPushButton { background: #1f1f1f; color: #e8e8e8; border: 1px solid #2c2c2c;"
        "  border-radius: 10px; padding: 6px 12px; font-size: 12px; }"
        "#onionRoute QPushButton:hover { background: #272727; border-color: #383838; }"
        "#onionRoute QPushButton:disabled { color: #5a5a5a; background: #171717; border-color: #212121; }"
        "#onionRoute QPushButton#dangerBtn { color: #f2b8ab; }"
        "#onionRoute QPushButton#dangerBtn:hover { background: #2a1714; border-color: #44231e; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_content = new QWidget(this);
    outer->addWidget(m_content);
    auto* lay = new QVBoxLayout(m_content);
    lay->setContentsMargins(20, 18, 20, 16);
    lay->setSpacing(12);

    auto* title = new QLabel(I18n::tr("route.title"), m_content);
    title->setObjectName("routeTitle");
    lay->addWidget(title);

    m_route = new RouteView(m_content);
    lay->addWidget(m_route);

    m_note = new QLabel(m_content);
    m_note->setObjectName("routeNote");
    m_note->setWordWrap(true);
    lay->addWidget(m_note);

    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_refresh = new QPushButton(I18n::tr("route.refresh"), m_content);
    m_check = new QPushButton(I18n::tr("route.check"), m_content);
    m_off = new QPushButton(I18n::tr("route.off"), m_content);
    m_off->setObjectName("dangerBtn");
    for (auto* b : {m_refresh, m_check, m_off}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
    }
    row->addWidget(m_refresh);
    row->addWidget(m_check);
    row->addStretch();
    row->addWidget(m_off);
    lay->addLayout(row);
    m_refresh->setVisible(m_tor);
    m_check->setVisible(m_tor);

    connect(m_refresh, &QPushButton::clicked, this, [this] { refresh(); });
    connect(m_check, &QPushButton::clicked, this, [this] {
        hide();
        if (onOpenCheck)
            onOpenCheck();
    });
    connect(m_off, &QPushButton::clicked, this, [this] {
        hide();
        if (onTurnOff)
            onTurnOff();
    });

    setFixedWidth(400);
    updateTexts();
}

void OnionRoutePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    p.fillPath(path, QColor("#151515"));
    p.setPen(QPen(QColor("#2a2a2a"), 1));
    p.drawPath(path);
}

void OnionRoutePanel::hideEvent(QHideEvent* e)
{
    m_hiddenAt = QDateTime::currentMSecsSinceEpoch();
    QFrame::hideEvent(e);
}

void OnionRoutePanel::setExitInfo(const TorExitInfo& info)
{
    m_busy = false;
    m_refresh->setEnabled(true);
    m_info = info;
    updateTexts();
}

void OnionRoutePanel::setCircuit(const TorControl::Circuit& c)
{
    m_circuitBusy = false;
    m_circuit = c;
    if (isVisible())
        updateTexts();
}

void OnionRoutePanel::setCircuitBusy()
{
    m_circuitBusy = true;
    if (isVisible())
        updateTexts();
}

void OnionRoutePanel::refresh()
{
    if (!m_tor || m_busy)
        return;
    m_busy = true;
    m_refresh->setEnabled(false);
    updateTexts();
    if (onRefresh)
        onRefresh();
}

void OnionRoutePanel::updateTexts()
{
    QVector<RouteView::Node> nodes;
    nodes.push_back({I18n::tr("route.you"),
                     m_tor ? I18n::tr("route.you.d") : I18n::tr("route.direct.d"), true, false});
    if (!m_tor) {
        nodes.push_back({I18n::tr("route.site"), I18n::tr("route.direct.site"), true, false});
        m_route->setNodes(nodes, false, false);
        m_note->setText(I18n::tr("route.direct.note"));
        fitToContent();
        return;
    }

    const auto& c = m_circuit;
    const bool haveCircuit = c.ok && c.hops.size() >= 2;
    const auto hopLine = [](const TorControl::Hop& h) {
        QStringList parts;
        parts << (h.nickname.isEmpty() ? QStringLiteral("$") + h.fingerprint.left(8) : h.nickname);
        if (!h.ip.isEmpty())
            parts << h.ip;
        if (!h.country.isEmpty())
            parts << h.country;
        return parts.join(QStringLiteral("  ·  "));
    };
    QString unknown;
    if (!haveCircuit) {
        if (m_circuitBusy || !c.when.isValid())
            unknown = I18n::tr("route.reading");
        else switch (c.fail) {
        case TorControl::Circuit::Fail::NoSocket: unknown = I18n::tr("route.unknown.nosocket"); break;
        case TorControl::Circuit::Fail::NotVerified: unknown = I18n::tr("route.unknown.notverified"); break;
        case TorControl::Circuit::Fail::Auth: unknown = I18n::tr("route.unknown.auth"); break;
        case TorControl::Circuit::Fail::NoCircuit: unknown = I18n::tr("route.unknown.nocircuit"); break;
        default: unknown = I18n::tr("route.unknown.other"); break;
        }
    }

    // Guard (plus the guards of other conflux legs).
    QString guardDetail = I18n::tr("route.guard.d") + QLatin1Char('\n');
    QString middleDetail = I18n::tr("route.middle.d") + QLatin1Char('\n');
    if (haveCircuit) {
        guardDetail += hopLine(c.hops.first());
        QStringList middles;
        for (int i = 1; i + 1 < c.hops.size(); ++i)
            middles << hopLine(c.hops.at(i));
        middleDetail += middles.join(QLatin1Char('\n'));
        for (const auto& leg : c.otherLegs) {
            if (leg.size() < 2)
                continue;
            guardDetail += QLatin1Char('\n') + I18n::tr("route.leg").arg(hopLine(leg.first()));
            QStringList legMiddles;
            for (int i = 1; i + 1 < leg.size(); ++i)
                legMiddles << hopLine(leg.at(i));
            if (!legMiddles.isEmpty())
                middleDetail += QLatin1Char('\n') + I18n::tr("route.leg").arg(legMiddles.join(QStringLiteral(", ")));
        }
    } else {
        guardDetail += unknown;
        middleDetail += unknown;
    }
    nodes.push_back({I18n::tr("route.guard"), guardDetail, haveCircuit, false});
    nodes.push_back({I18n::tr("route.middle"), middleDetail, haveCircuit, false});

    // Exit: relay from the circuit, address as seen by check.torproject.org.
    QString exitDetail;
    const bool checkDone = !m_busy && m_info.when.isValid();
    const bool checkTor = checkDone && m_info.ok && m_info.isTor;
    QString checkLine;
    if (!checkDone) {
        checkLine = I18n::tr("route.exit.checking");
    } else if (!m_info.ok) {
        checkLine = I18n::tr("route.exit.fail");
        if (!m_info.error.isEmpty())
            checkLine += QStringLiteral(" (") + m_info.error + QLatin1Char(')');
    } else if (!m_info.isTor) {
        checkLine = I18n::tr("route.exit.nottor");
    }
    if (haveCircuit) {
        const auto& ex = c.hops.last();
        exitDetail = hopLine(ex);
        if (checkTor) {
            exitDetail += QLatin1Char('\n') + (m_info.ip == ex.ip ? I18n::tr("route.exit.checkmatch")
                                                                  : I18n::tr("route.exit.checkdiff").arg(m_info.ip));
        } else if (!checkLine.isEmpty()) {
            exitDetail += QLatin1Char('\n') + checkLine;
        }
    } else if (checkTor) {
        QStringList parts{m_info.ip};
        if (!m_info.nickname.isEmpty())
            parts << m_info.nickname;
        if (!m_info.country.isEmpty())
            parts << m_info.country;
        exitDetail = parts.join(QStringLiteral("  ·  "));
        if (!m_info.asName.isEmpty())
            exitDetail += QLatin1Char('\n') + m_info.asName;
    } else {
        exitDetail = checkLine;
    }
    const bool exitKnown = haveCircuit || checkTor;
    nodes.push_back({I18n::tr("route.exit"), exitDetail, exitKnown, true});
    nodes.push_back({I18n::tr("route.site"), I18n::tr("route.site.d"), true, false});
    const bool waiting = m_busy || !m_info.when.isValid() || m_circuitBusy;
    m_route->setNodes(nodes, exitKnown && !waiting, waiting);

    QStringList note;
    if (haveCircuit) {
        switch (c.pick) {
        case TorControl::Pick::Stream: {
            QString target = c.streamTarget;
            note << I18n::tr("route.pick.stream").arg(target.left(target.lastIndexOf(QLatin1Char(':'))));
            break;
        }
        case TorControl::Pick::ExitMatch: note << I18n::tr("route.pick.exit"); break;
        default: note << I18n::tr("route.pick.newest"); break;
        }
        if (!c.otherLegs.isEmpty())
            note << I18n::tr("route.conflux").arg(c.otherLegs.size() + 1);
        note << I18n::tr("route.note.ctl").arg(c.when.toString(QStringLiteral("HH:mm")));
    } else {
        if (checkTor)
            note << I18n::tr("route.checked").arg(m_info.when.toString(QStringLiteral("HH:mm")));
        note << I18n::tr("route.note");
    }
    m_note->setText(note.join(QLatin1Char(' ')));
    fitToContent();
}

void OnionRoutePanel::fitToContent()
{
    // The route view changes its fixed height when a detail line wraps;
    // let the layouts pick that up before sizing the popup.
    m_content->layout()->invalidate();
    m_content->layout()->activate();
    layout()->invalidate();
    layout()->activate();
    resize(width(), sizeHint().height());
}

void OnionRoutePanel::popupBelow(QWidget* anchor)
{
    updateTexts();
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 6));
    if (const QScreen* s = anchor->screen()) {
        const QRect g = s->availableGeometry();
        pos.setX(qBound(g.left() + 8, pos.x(), g.right() - width() - 8));
    }
    move(pos);
    show();
    Motion::fadeIn(m_content, Motion::kNormal);
    // Re-check if we have nothing yet or the last result is over a minute old.
    if (m_tor && (!m_info.when.isValid() || m_info.when.secsTo(QDateTime::currentDateTime()) > 60))
        refresh();
}
