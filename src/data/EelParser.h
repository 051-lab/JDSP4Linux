#ifndef EELPARSER_H
#define EELPARSER_H

#include <model/codecontainer.h>
#include <QMap>
#include <optional>
#include <utils/Log.h>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <cmath>

#define NORESULT "<<NO RESULT FOUND>>"

class EELBaseProperty;
enum class EELPropertyType;
typedef QVector<EELBaseProperty*> EELProperties;

class EELParser :
	public QObject
{
	Q_OBJECT

public:
	EELParser();
	~EELParser() override;
	bool          loadFile(QString path);
	bool          saveFile();
    bool          loadDefaults();
    bool          hasDefaultsDefined();
    bool          canLoadDefaults();
    bool          isFileLoaded();
	QString       getPath();
	QString       getDescription();
	EELProperties getProperties();
	QStringList   getDiagnostics() const;
	QString       getLastSaveError() const;
	bool          manipulateProperty(EELBaseProperty *prop);

private:
	CodeContainer container;

    QString       findVariable(QString         key,
                               EELPropertyType type);

	bool          replaceVariable(QString         key,
	                              QString         value,
	                              EELPropertyType type);

    void clearProperties();

    EELProperties properties;
    QStringList diagnostics;
    QString lastSaveError;

};

enum class EELPropertyType
{
	Unknown,
    NumberRange,
    List
};

class EELBaseProperty
{
public:
	EELBaseProperty(QString _key,
	                QString _description)
	{
		key         = _key;
		description = _description;
		type        = EELPropertyType::Unknown;
	}

	EELBaseProperty()
	{
		type = EELPropertyType::Unknown;
	};
	virtual ~EELBaseProperty() {};

	QString getKey() const
	{
		return key;
	}

	void setKey(const QString &value)
	{
		key = value;
	}

	QString getDescription() const
	{
		return description;
	}

	void setDescription(const QString &value)
	{
		description = value;
	}

	EELPropertyType getType() const
	{
		return type;
	}

    virtual bool hasDefault() const = 0;

protected:
	QString key;
	QString description;
	EELPropertyType type;
};

template<typename TNum>
class EELNumberRangeProperty :
	public EELBaseProperty
{
public:
	EELNumberRangeProperty(QString _key,
	                       QString _description,
                           std::optional<TNum> _default,
                           TNum    _value,
                           TNum    _minimum,
	                       TNum    _maximum,
                           TNum    _step = 0)
	{
        key         = _key;
        description = _description;
        dflt        = _default;
        value       = _value;
        maximum     = _maximum;
		minimum     = _minimum;
        step        = _step;
		type        = EELPropertyType::NumberRange;
	}

	EELNumberRangeProperty()
	{
		type = EELPropertyType::NumberRange;
	};

	~EELNumberRangeProperty() {}

	TNum getValue() const
	{
		return value;
	}

	void setValue(const TNum &_value)
	{
        TNum candidate = _value;
        if (std::isfinite(static_cast<double>(step)) && step > 0)
            candidate = minimum + static_cast<TNum>(std::round((candidate - minimum) / step)) * step;
        value = fmin(fmax(minimum, candidate), maximum);
	}

	TNum getMaximum() const
	{
		return maximum;
	}

	TNum getMinimum() const
	{
		return minimum;
	}

	TNum getStep() const
	{
		return step;
	}

    TNum getDefault() const
    {
        if(!hasDefault())
        {
            Log::warning("No default value set");
            return fmin(fmax(minimum, value), maximum);
        }
        TNum candidate = dflt.value();
        if (std::isfinite(static_cast<double>(step)) && step > 0)
            candidate = minimum + static_cast<TNum>(std::round((candidate - minimum) / step)) * step;
        return fmin(fmax(minimum, candidate), maximum);
    }

    bool hasDefault() const override
    {
        return dflt.has_value();
    }

private:
    std::optional<TNum> dflt = std::nullopt;
    TNum maximum;
    TNum minimum;
	TNum step;
	TNum value;
};

class EELListProperty :
    public EELNumberRangeProperty<int>
{
public:
    EELListProperty(QString _key,
                    QString _description,
                    std::optional<int> _default,
                    int _value,
                    int _minimum,
                    int _maximum,
                    QStringList _options) : EELNumberRangeProperty(_key, _description, _default, _value, _minimum, _maximum, 1)
    {
        options = _options;
        type = EELPropertyType::List;
    }

    ~EELListProperty() {}

    const QStringList &getOptions() const
    {
        return options;
    }

    void setOptions(const QStringList &newOptions)
    {
        options = newOptions;
    }

private:
    QStringList options;
};


inline bool qFloatCompare(float f1, float f2)
{
    if (qFuzzyIsNull(qAbs(f1 - f2)))
        return true;
    return qFuzzyCompare(f1, f2);
}

#endif // EELPARSER_H
