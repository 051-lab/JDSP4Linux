#include "VisualTheme.h"

namespace VisualThemeProvider
{
QVector<VisualThemeDefinition> definitions()
{
    return {
        {"classic",      "Classic JamesDSP",       ":/styles/themes/classic.qss"},
        {"studio",       "JamesDSP Studio",        ":/styles/themes/studio.qss"},
        {"midnight",     "Midnight Glass",         ":/styles/themes/midnight.qss"},
        {"paper",        "Paper Studio",           ":/styles/themes/paper.qss"},
        {"highcontrast", "Accessibility High Contrast", ":/styles/themes/highcontrast.qss"}
    };
}

QString defaultId()
{
    return QStringLiteral("classic");
}

bool contains(const QString &id)
{
    for (const auto &theme : definitions())
    {
        if (theme.id == id)
            return true;
    }
    return false;
}

VisualThemeDefinition definition(const QString &id)
{
    for (const auto &theme : definitions())
    {
        if (theme.id == id)
            return theme;
    }
    return definitions().first();
}
}
