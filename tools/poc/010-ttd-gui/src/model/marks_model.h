#pragma once
//
// marks_model.h — QAbstractListModel for user marks + section spans.
//

#include <QAbstractListModel>
#include <QColor>
#include <QJsonDocument>
#include <QString>
#include <vector>

namespace ttd {

// A single-frame user mark (right-click to add)
struct Mark {
    uint64_t frame;
    QString  label;
    QColor   color;
};

// A section span (loaded from sidecar .marks.json)
struct Section {
    QString  label;
    uint64_t start;
    uint64_t end;
    QColor   color;
};

class MarksModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        FrameRole = Qt::UserRole + 1,
        LabelRole,
        ColorRole,
    };

    explicit MarksModel(QObject* parent = nullptr);

    // QAbstractListModel overrides
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // User marks
    void addMark(uint64_t frame, const QString& label, const QColor& color = QColor(255, 0, 0));
    void removeMark(int row);
    const std::vector<Mark>& marks() const { return _marks; }

    // Section spans
    const std::vector<Section>& sections() const { return _sections; }
    void clearSections() { _sections.clear(); }
    void addSection(const Section& s) { _sections.push_back(s); }

    /// Load section spans + point marks from a .marks.json sidecar.
    /// Returns true on success (file missing is not an error — just no marks).
    bool loadSidecar(const QString& path);

    /// Check if a mark exists at exactly this frame.
    bool hasMarkAtFrame(uint64_t frame) const;

private:
    std::vector<Mark> _marks;
    std::vector<Section> _sections;
};

} // namespace ttd
