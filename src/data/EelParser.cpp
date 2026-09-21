#include "EelParser.h"
#include "utils/Common.h"
#include "utils/Log.h"

#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>

static QRegularExpression assignmentRegex(const QString &key)
{
    return QRegularExpression(QStringLiteral("(?:^|\\n)[\\t ]*%1\\s*=\\s*(?<val>[-+]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*;")
                                   .arg(QRegularExpression::escape(key)));
}

static QString maskCommentsAndStrings(const QString &source)
{
    QString masked = source;
    bool blockComment = false;
    bool lineComment = false;
    bool stringLiteral = false;
    bool escaped = false;

    for (int i = 0; i < source.size(); ++i)
    {
        const QChar ch = source.at(i);
        const QChar next = i + 1 < source.size() ? source.at(i + 1) : QChar();

        if (lineComment)
        {
            if (ch == '\n' || ch == '\r')
                lineComment = false;
            else
                masked[i] = QChar(0x01);
            continue;
        }
        if (blockComment)
        {
            if (ch == '*' && next == '/')
            {
                masked[i] = QChar(0x01);
                masked[++i] = QChar(0x01);
                blockComment = false;
            }
            else if (ch != '\n' && ch != '\r')
                masked[i] = QChar(0x01);
            continue;
        }
        if (stringLiteral)
        {
            if (ch == '\n' || ch == '\r')
                stringLiteral = false;
            else
                masked[i] = ' ';
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                stringLiteral = false;
            continue;
        }
        if (ch == '/' && next == '/')
        {
            masked[i] = QChar(0x01);
            masked[++i] = QChar(0x01);
            lineComment = true;
        }
        else if (ch == '/' && next == '*')
        {
            masked[i] = QChar(0x01);
            masked[++i] = QChar(0x01);
            blockComment = true;
        }
        else if (ch == '"')
        {
            masked[i] = QChar(0x01);
            stringLiteral = true;
        }
    }
    return masked;
}

static int decimalPlaces(float value)
{
    for (int places = 0; places <= 6; ++places)
    {
        const double scale = std::pow(10.0, places);
        if (std::abs(static_cast<double>(value) * scale -
                     std::round(static_cast<double>(value) * scale)) < 0.000001)
            return places;
    }
    return 6;
}

EELParser::EELParser()
{}

EELParser::~EELParser()
{
    clearProperties();
}

bool EELParser::loadFile(QString path)
{
    diagnostics.clear();
    container.path = path;
    container.code = "";
    if (!container.reloadCode())
    {
        Log::warning(QString("Failed to load EEL file: %1").arg(path));
        clearProperties();
        return false;
    }

    clearProperties();

    // Parse parameters

    QRegularExpression descRe(R"((?<var>\w+):(?<def>-?\d+\.?\d*)?<(?<min>-?\d+\.?\d*),(?<max>-?\d+\.?\d*),?(?<step>-?\d+\.?\d*)?>(?<desc>[\s\S][^\n]*))");
    QRegularExpression descListRe(R"((?<var>\w+):(?<def>-?\d+\.?\d*)?<(?<min>-?\d+\.?\d*),(?<max>-?\d+\.?\d*),?(?<step>-?\d+\.?\d*)?\{(?<opt>[^\}]*)\}>(?<desc>[\s\S][^\n]*))");
    QSet<QString> describedKeys;

    for (const auto &line : container.code.split("\n"))
    {
        // List parameters
        {
            auto matchIterator = descListRe.globalMatch(line);

            if (matchIterator.hasNext())
            {
                auto    match = matchIterator.next();
                QString key   = match.captured("var");
                QString min   = match.captured("min");
                QString max   = match.captured("max");
                QString step  = match.captured("step");
                QString opt   = match.captured("opt");
                QString def   = match.captured("def");
                QString desc  = match.captured("desc").trimmed();

                if (describedKeys.contains(key))
                    continue;

                if (step.isEmpty())
                {
                    step = "1";
                }

                QString current = findVariable(key, EELPropertyType::List);

                if (current == NORESULT)
                {
                    diagnostics.append(QString("Control '%1' has no editable numeric assignment").arg(key));
                    continue;
                }

                bool minOk = false;
                bool maxOk = false;
                bool currentOk = false;
                const int minimum = min.toInt(&minOk);
                const int maximum = max.toInt(&maxOk);
                const int currentValue = current.toInt(&currentOk);
                bool defOk = false;
                std::optional<float> defaultValue = def.toFloat(&defOk);
                if ((!def.isEmpty() && !defOk) || !minOk || !maxOk || !currentOk ||
                    minimum > maximum || currentValue < minimum || currentValue > maximum ||
                    (!def.isEmpty() && (defaultValue.value() < minimum || defaultValue.value() > maximum)) ||
                    opt.split(',', Qt::SkipEmptyParts).isEmpty())
                    continue;
                if(def.isEmpty())
                    defaultValue = std::nullopt;

                EELListProperty *prop = new EELListProperty(key, desc, defaultValue, current.toInt(),
                                                            minimum, maximum, opt.split(',', Qt::SkipEmptyParts));
                properties.append(prop);
                describedKeys.insert(key);
                continue;
            }

        }

        // Number range parameters
        {
            auto matchIterator = descRe.globalMatch(line);

            if (matchIterator.hasNext())
            {
                auto    match = matchIterator.next();
                QString key   = match.captured("var");
                QString min   = match.captured("min");
                QString max   = match.captured("max");
                QString step  = match.captured("step");
                QString def   = match.captured("def");
                QString desc  = match.captured("desc").trimmed();

                if (describedKeys.contains(key))
                    continue;

                if (step.isEmpty())
                {
                    step = "0.1";
                }

                QString current = findVariable(key, EELPropertyType::NumberRange);

                if (current == NORESULT)
                {
                    diagnostics.append(QString("Control '%1' has no editable numeric assignment").arg(key));
                    continue;
                }

                bool minOk = false;
                bool maxOk = false;
                bool stepOk = false;
                bool currentOk = false;
                const float minimum = min.toFloat(&minOk);
                const float maximum = max.toFloat(&maxOk);
                const float parsedStep = step.toFloat(&stepOk);
                const float currentValue = current.toFloat(&currentOk);
                bool defOk = false;
                std::optional<float> defaultValue = def.toFloat(&defOk);
                if ((!def.isEmpty() && !defOk) || !minOk || !maxOk || !stepOk || !currentOk ||
                    !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(parsedStep) ||
                    !std::isfinite(currentValue) || minimum > maximum || parsedStep <= 0 ||
                    currentValue < minimum || currentValue > maximum ||
                    (!def.isEmpty() && (defaultValue.value() < minimum || defaultValue.value() > maximum)))
                    continue;
                if(def.isEmpty())
                    defaultValue = std::nullopt;

                EELNumberRangeProperty<float> *prop = new EELNumberRangeProperty<float>(key, desc, defaultValue, current.toFloat(),
                                                                                        minimum, maximum, parsedStep);
                properties.append(prop);
                describedKeys.insert(key);
                continue;
            }
        }
    }

    return true;
}

bool EELParser::saveFile()
{
    lastSaveError.clear();
    if (!isFileLoaded())
    {
        lastSaveError = QStringLiteral("No EEL file is currently loaded.");
        return false;
    }

    return container.save(QString(), nullptr, &lastSaveError);
}

bool EELParser::loadDefaults()
{
    if (!isFileLoaded())
    {
        return false;
    }

    for(const auto& prop : std::as_const(properties))
    {
        if(prop->getType() == EELPropertyType::NumberRange)
        {
            auto* nr = dynamic_cast<EELNumberRangeProperty<float>*>(prop);
            nr->setValue(nr->getDefault());
            manipulateProperty(prop);
        }
        else if (prop->getType() == EELPropertyType::List) {
            auto* list = dynamic_cast<EELListProperty*>(prop);
            list->setValue(list->getDefault());
            manipulateProperty(prop);
        }
    }
    return true;
}

bool EELParser::hasDefaultsDefined()
{
    if (!isFileLoaded())
    {
        return false;
    }

    for(const auto& prop : std::as_const(properties))
    {
        if(prop->hasDefault())
        {
            return true;
        }
    }
    return false;
}

bool EELParser::canLoadDefaults()
{
    if (!isFileLoaded())
    {
        return false;
    }

    for(auto* prop : std::as_const(properties))
    {
        if(prop->getType() == EELPropertyType::NumberRange)
        {
            auto* nr = dynamic_cast<EELNumberRangeProperty<float>*>(prop);

            if(prop->hasDefault() && qFloatCompare(nr->getDefault(), nr->getValue()) == false)
            {
                return true;
            }
        }
        else if(prop->getType() == EELPropertyType::List)
        {
            auto* nr = dynamic_cast<EELListProperty*>(prop);

            if(prop->hasDefault() && nr->getDefault() != nr->getValue())
            {
                return true;
            }
        }
    }

    return false;
}

bool EELParser::isFileLoaded()
{
    return container.codeLoaded;
}

QString EELParser::getPath()
{
    return container.path;
}

QString EELParser::getDescription()
{
    QRegularExpression descRe(R"((?:^|(?<=\n))(?:desc:)([\s\S][^\n]*))");

    for (const auto &line : container.code.split("\n"))
    {
        auto matchIterator = descRe.globalMatch(line);

        if (matchIterator.hasNext())
        {
            auto match = matchIterator.next();
            return match.captured(1).trimmed();
        }
    }

    return QFileInfo(container.path).fileName();
}

EELProperties EELParser::getProperties()
{
    return properties;
}

QStringList EELParser::getDiagnostics() const
{
    return diagnostics;
}

QString EELParser::getLastSaveError() const
{
    return lastSaveError;
}

bool EELParser::manipulateProperty(EELBaseProperty *propbase)
{
    lastSaveError.clear();
    if (propbase->getType() == EELPropertyType::NumberRange)
    {
        EELNumberRangeProperty<float> *prop = dynamic_cast<EELNumberRangeProperty<float>*>(propbase);
        QString                        value;

        if (std::floor(prop->getStep()) == prop->getStep()) // is integer?
        {
            value = QString::number((int) prop->getValue());
        }
        else
        {
            value = QString::number(prop->getValue(), 'f', decimalPlaces(prop->getStep()));
        }

        bool replace_res = replaceVariable(prop->getKey(), value, prop->getType());
        if (!replace_res)
        {
            lastSaveError = QStringLiteral("Unable to update the assignment for '%1' in %2.")
                                .arg(prop->getKey(), container.path);
            return false;
        }
        return saveFile();
    }
    else if (propbase->getType() == EELPropertyType::List)
    {
        EELListProperty *prop = dynamic_cast<EELListProperty*>(propbase);
        QString          value = QString::number((int) prop->getValue());

        bool replace_res = replaceVariable(prop->getKey(), value, prop->getType());
        if (!replace_res)
        {
            lastSaveError = QStringLiteral("Unable to update the assignment for '%1' in %2.")
                                .arg(prop->getKey(), container.path);
            return false;
        }
        return saveFile();
    }

    return false;
}

// --- Private members

QString EELParser::findVariable(QString         key,
                                EELPropertyType type)
{
    if (type == EELPropertyType::NumberRange || type == EELPropertyType::List)
    {
        QRegularExpression re = assignmentRegex(key);
        const QString maskedCode = maskCommentsAndStrings(container.code);

        for (const auto &line : maskedCode.split("\n"))
        {
            auto matchIterator = re.globalMatch(line);

            if (matchIterator.hasNext())
            {
                auto match = matchIterator.next();
                return match.captured("val");
            }
        }
    }

    Log::warning(QString("Unable to find a supported variable definition of '%1' in script '%2'").arg(key).arg(getDescription()));
    return NORESULT;
}

bool EELParser::replaceVariable(QString         key,
                                QString         value,
                                EELPropertyType type)
{
    if (type == EELPropertyType::NumberRange || type == EELPropertyType::List)
    {
        QRegularExpression re = assignmentRegex(key);
        const QString maskedCode = maskCommentsAndStrings(container.code);
        auto               matchIterator = re.globalMatch(maskedCode);

        if (matchIterator.hasNext())
        {
            auto match = matchIterator.next();
            int  start = match.capturedStart("val");
            int  len   = match.capturedLength("val");
            container.code.remove(start, len);
            container.code.insert(start, value);
            return true;
        }
    }

    return false;
}

void EELParser::clearProperties()
{
    for(auto& prop : properties)
    {
        delete prop;
        prop = nullptr;
    }
    properties.clear();
}
