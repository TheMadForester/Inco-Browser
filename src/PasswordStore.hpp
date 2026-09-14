#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct SavedLogin
{
    QString host;
    QString user;
    QString password;
};

class PasswordStore
{
public:
    explicit PasswordStore(const QString& path);

    bool exists() const;
    bool isUnlocked() const { return m_unlocked; }

    bool create(const QString& masterPassword);
    bool unlock(const QString& masterPassword);
    bool changeMasterPassword(const QString& oldPw, const QString& newPw);
    void lock();
    QString path() const { return m_path; }

    QVector<SavedLogin> all() const;
    bool upsert(const SavedLogin& login);
    bool removeHost(const QString& host);

private:
    bool persist() const;
    QByteArray deriveKey(const QString& master, const QByteArray& salt) const;
    QByteArray encrypt(const QByteArray& plaintext) const;
    QByteArray decrypt(const QByteArray& blob) const;

    QString m_path;
    QByteArray m_key;
    QByteArray m_salt;
    QVector<SavedLogin> m_rows;
    bool m_unlocked = false;
};
