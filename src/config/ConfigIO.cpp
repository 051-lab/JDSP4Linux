#include "ConfigIO.h"

#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <fstream>
#include <string>

#include "utils/QtCompat.h"
#include "utils/Log.h"

QString ConfigIO::writeString(const QVariantMap &map)
{
	QString ret("");

	for (const auto &e : map.keys())
	{
		ret += QString("%1=%2\n").arg(e).arg(map.value(e).toString());
	}

	return ret;
}

bool ConfigIO::writeFile(const QString &    path,
                         const QVariantMap &map,
                         const QString &    prefix)
{
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		Log::error(QString("Unable to open configuration for atomic write: %1").arg(path));
		return false;
	}

	QTextStream stream(&file);

	if (!prefix.isEmpty())
	{
		stream << prefix << Qt::endl;
	}

	for (const auto &e : map.keys())
	{
		if (QtCompat::variantTypeId(map.value(e)) == QMetaType::Float)
		{
			stream << e << '=' << QString::number(map.value(e).toDouble(), 'f', 5) << Qt::endl;
		}
		else
		{
			stream << e << '=' << map.value(e).toString() << Qt::endl;
		}
	}

	stream.flush();
	if(stream.status() != QTextStream::Ok || !file.flush() || !file.commit())
	{
		Log::error(QString("Unable to commit atomic configuration write: %1").arg(path));
		return false;
	}
	return true;
}

QVariantMap ConfigIO::readFile(const QString &path)
{
	QVariantMap   map;
	std::ifstream cFile(path.toUtf8().constData());

	if (cFile.is_open())
	{
		std::string line;

		while (getline(cFile, line))
		{
			QPair<QString, QVariant> out;

			if (readLine(QString::fromStdString(line), out))
			{
				map[out.first] = out.second;
			}
		}

		cFile.close();
	}

	return map;
}

QVariantMap ConfigIO::readString(const QString &string)
{
	QVariantMap map;
	QStringList lines = string.split('\n');

	for (const auto &line : lines)
	{
		QPair<QString, QVariant> out;

		if (readLine(line, out))
		{
			map[out.first] = out.second;
		}
	}

	return map;
}

bool ConfigIO::readLine(const QString &line,
                        QPair<QString, QVariant> &out)
{
    if (line.trimmed().isEmpty() || line.trimmed()[0] == '#' || line.trimmed()[0] == '[' || !line.contains('='))
	{
        return false; // Skip commented lines
	}

	auto    delimiterInlineComment = line.indexOf('#'); // Look for config properties mixed up with comments
	auto    extractedProperty      = line.mid(0, delimiterInlineComment);
	auto    delimiterPos           = extractedProperty.indexOf('=');
	auto    name                   = extractedProperty.mid(0, delimiterPos);
	auto    value                  = extractedProperty.mid(delimiterPos + 1);
	QString qname                  = name.trimmed();
	QString qvalue                 = value.trimmed();

	if (qvalue == "true")
	{
		out = QPair<QString, QVariant>(qname, true);
	}
	else if (qvalue == "false")
	{
		out = QPair<QString, QVariant>(qname, false);
	}
	else
	{
		out = QPair<QString, QVariant>(qname, qvalue);
	}

	return true;
}
