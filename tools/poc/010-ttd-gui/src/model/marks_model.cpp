//
// marks_model.cpp — Implementation of the marks model.
//

#include "marks_model.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>

namespace ttd {

MarksModel::MarksModel(QObject* parent)
    : QAbstractListModel(parent) {}

int MarksModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return static_cast<int>(_marks.size());
}

QVariant MarksModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(_marks.size()))
        return {};

    const Mark& m = _marks[index.row()];
    switch (role) {
        case Qt::DisplayRole:
        case LabelRole:
            return m.label;
        case FrameRole:
            return QVariant::fromValue(m.frame);
        case ColorRole:
            return m.color;
        default:
            return {};
    }
}

QHash<int, QByteArray> MarksModel::roleNames() const {
    return {
        {FrameRole, "frame"},
        {LabelRole, "label"},
        {ColorRole, "color"},
    };
}

void MarksModel::addMark(uint64_t frame, const QString& label, const QColor& color) {
    int row = static_cast<int>(_marks.size());
    beginInsertRows(QModelIndex(), row, row);
    _marks.push_back({frame, label, color});
    endInsertRows();
}

void MarksModel::removeMark(int row) {
    if (row < 0 || row >= static_cast<int>(_marks.size()))
        return;
    beginRemoveRows(QModelIndex(), row, row);
    _marks.erase(_marks.begin() + row);
    endRemoveRows();
}

bool MarksModel::loadSidecar(const QString& path) {
    QFile file(path);
    if (!file.exists())
        return false;  // No sidecar — not an error
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError)
        return false;

    QJsonObject root = doc.object();

    // Sections
    if (root.contains("sections")) {
        _sections.clear();
        QJsonArray arr = root["sections"].toArray();
        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            Section s;
            s.label = obj["label"].toString();
            s.start = static_cast<uint64_t>(obj["start"].toVariant().toULongLong());
            s.end   = static_cast<uint64_t>(obj["end"].toVariant().toULongLong());
            s.color = QColor(obj["color"].toString("#3060FF"));
            _sections.push_back(s);
        }
    }

    // Points (user marks from sidecar)
    if (root.contains("points")) {
        QJsonArray arr = root["points"].toArray();
        for (const auto& val : arr) {
            QJsonObject obj = val.toObject();
            Mark m;
            m.frame = static_cast<uint64_t>(obj["frame"].toVariant().toULongLong());
            m.label = obj["label"].toString();
            m.color = QColor(obj["color"].toString("#FF0000"));
            addMark(m.frame, m.label, m.color);
        }
    }

    return true;
}

bool MarksModel::hasMarkAtFrame(uint64_t frame) const {
    for (const auto& m : _marks)
        if (m.frame == frame)
            return true;
    return false;
}

} // namespace ttd
