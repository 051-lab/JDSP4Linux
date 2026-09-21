#ifndef PRESETLISTMODEL_H
#define PRESETLISTMODEL_H

#include <QAbstractListModel>
#include <QString>

class PresetListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit PresetListModel(QObject *parent = nullptr, QString directoryOverride = {});

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void rescan();
    QStringList getList() const;

private:

    QStringList presets;
    QString directoryOverride;
};

#endif // PRESETLISTMODEL_H
