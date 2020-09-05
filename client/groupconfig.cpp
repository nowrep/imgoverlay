#include "groupconfig.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>

GroupConfig::GroupConfig(const QString &confFile, const QString &group)
    : m_confFile(confFile)
    , m_group(group)
{
    m_x = value(QStringLiteral("X")).toInt();
    m_y = value(QStringLiteral("Y")).toInt();
    m_width = value(QStringLiteral("Width")).toInt();
    m_height = value(QStringLiteral("Height")).toInt();
    m_url = value(QStringLiteral("Url")).toUrl();

    const QString scriptPath = value(QStringLiteral("InjectScript")).toString();
    if (!scriptPath.isEmpty()) {
        QFile file(Utils::resolvedPath(scriptPath, QFileInfo(m_confFile).path()));
        if (!file.open(QFile::ReadOnly)) {
            qWarning() << "Failed to read" << file.fileName();
        } else {
            m_injectScript = file.readAll();
        }
    }
}

int GroupConfig::x() const
{
    return m_x;
}

int GroupConfig::y() const
{
    return m_y;
}

int GroupConfig::width() const
{
    return m_width;
}

int GroupConfig::height() const
{
    return m_height;
}

QUrl GroupConfig::url() const
{
    return m_url;
}

QString GroupConfig::injectScript() const
{
    return m_injectScript;
}

QVariant GroupConfig::value(const QString &key) const
{
    return QSettings(m_confFile, QSettings::IniFormat).value(QStringLiteral("%1/%2").arg(m_group, key));
}
