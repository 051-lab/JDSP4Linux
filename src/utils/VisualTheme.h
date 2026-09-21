#ifndef VISUALTHEME_H
#define VISUALTHEME_H

#include <QString>
#include <QVector>

struct VisualThemeDefinition
{
    QString id;
    QString name;
    QString stylesheet;
};

namespace VisualThemeProvider
{
QVector<VisualThemeDefinition> definitions();
QString defaultId();
bool contains(const QString &id);
VisualThemeDefinition definition(const QString &id);
}

#endif // VISUALTHEME_H
