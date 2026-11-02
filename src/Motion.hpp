#pragma once

// Small, fast UI transitions shared by the browser chrome and Settings.
// Never put an opacity effect on a QWebEngineView (or a parent of one):
// it forces the web contents through an offscreen copy and can go blank.

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

namespace Motion {

constexpr int kFast = 140;
constexpr int kNormal = 180;

inline void stopSlide(QWidget* w)
{
    if (auto* old = w->findChild<QPropertyAnimation*>(QStringLiteral("motionSlide"),
                                                      Qt::FindDirectChildrenOnly)) {
        old->stop();
        delete old;
    }
}

// Fade a widget in from transparent. The effect is dropped when done, so an
// idle widget pays nothing for it.
inline void fadeIn(QWidget* w, int ms = kNormal, int delayMs = 0)
{
    if (!w)
        return;
    auto* eff = new QGraphicsOpacityEffect(w);
    eff->setOpacity(0.0);
    w->setGraphicsEffect(eff);
    w->show();
    auto* a = new QPropertyAnimation(eff, "opacity", eff);
    a->setDuration(ms);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w, eff] {
        QTimer::singleShot(0, w, [w, eff = QPointer<QGraphicsOpacityEffect>(eff)] {
            if (eff && w->graphicsEffect() == eff)
                w->setGraphicsEffect(nullptr);
        });
    });
    if (delayMs > 0)
        QTimer::singleShot(delayMs, a, [a] { a->start(); });
    else
        a->start();
}

// Open a bar by growing its height from 0 to its natural size.
inline void slideOpen(QWidget* w, int ms = kFast)
{
    if (!w)
        return;
    const bool hiding = w->property("motionHiding").toBool();
    if (w->isVisible() && !hiding)
        return;
    stopSlide(w);
    w->setProperty("motionHiding", false);
    const int from = w->isVisible() ? w->height() : 0;
    w->setMaximumHeight(QWIDGETSIZE_MAX);
    const int to = qMax(1, w->sizeHint().height());
    w->setMaximumHeight(from);
    w->show();
    auto* a = new QPropertyAnimation(w, "maximumHeight", w);
    a->setObjectName(QStringLiteral("motionSlide"));
    a->setDuration(ms);
    a->setStartValue(from);
    a->setEndValue(to);
    a->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w] {
        w->setMaximumHeight(QWIDGETSIZE_MAX);
    });
    a->start();
}

// Close a bar by shrinking it to 0, then hide it.
inline void slideClose(QWidget* w, int ms = kFast)
{
    if (!w || !w->isVisible() || w->property("motionHiding").toBool())
        return;
    stopSlide(w);
    w->setProperty("motionHiding", true);
    auto* a = new QPropertyAnimation(w, "maximumHeight", w);
    a->setObjectName(QStringLiteral("motionSlide"));
    a->setDuration(ms);
    a->setStartValue(w->height());
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(a, &QPropertyAnimation::finished, w, [w] {
        w->hide();
        w->setMaximumHeight(QWIDGETSIZE_MAX);
        w->setProperty("motionHiding", false);
    });
    a->start();
}

} // namespace Motion
