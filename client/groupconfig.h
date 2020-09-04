#pragma once

#include <QUrl>
#include <QVariant>

class GroupConfig
{
public:
    explicit GroupConfig(const QString &confFile, const QString &group);

    int x() const;
    int y() const;
    int width() const;
    int height() const;
    QUrl url() const;

private:
    QVariant value(const QString &key) const;

    QString m_confFile;
    QString m_group;

    int m_x = 0;
    int m_y = 0;
    int m_width = 0;
    int m_height = 0;
    QUrl m_url;
};
