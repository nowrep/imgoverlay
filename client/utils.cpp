#include "utils.h"

#include <QDir>

QString Utils::resolvedPath(const QString &path, const QString &basePath)
{
    if (QDir::isAbsolutePath(path)) {
        return path;
    }
    return QDir(QDir::cleanPath(basePath)).absoluteFilePath(path);
}
