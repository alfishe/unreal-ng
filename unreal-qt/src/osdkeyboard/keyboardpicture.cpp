#include "keyboardpicture.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "debugger/keyboard/debugkeyboardmanager.h"

namespace
{
QJsonObject ReadJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}  // namespace

bool PictureKey::contains(const QPointF& point) const
{
    return shape.isEmpty() ? rect.contains(point) : shape.containsPoint(point, Qt::OddEvenFill);
}

bool PictureKey::modifier() const
{
    if (zx.size() == 1)
        // The Orel BK-08's ROM takes РЕГ and Р/А as shifts too (KEY-SCAN's K-TEST: codes #EF, #F8)
        return zx[0] == ZXKEY_CAPS_SHIFT || zx[0] == ZXKEY_SYM_SHIFT || zx[0] == ZXKEY_BK08_SHIFT ||
               zx[0] == ZXKEY_BK08_RA;
    switch (pc)
    {
        case PcKey::LeftShift:
        case PcKey::RightShift:
        case PcKey::LeftCtrl:
        case PcKey::RightCtrl:
        case PcKey::LeftAlt:
        case PcKey::RightAlt:
        case PcKey::LeftGui:
        case PcKey::RightGui:
            return true;
        default:
            return false;
    }
}

const PictureKey* KeyboardPicture::keyAt(const QPointF& point) const
{
    for (const PictureKey& key : keys)
        if (key.contains(point))
            return &key;
    return nullptr;
}

bool KeyboardPictures::load(const QString& root)
{
    _root = root;
    _models.clear();
    _names.clear();
    const QJsonObject index = ReadJson(root + "/index.json");
    const QJsonObject models = index.value("models").toObject();
    for (auto it = models.begin(); it != models.end(); ++it)
    {
        std::vector<std::string>& ids = _models[it.key().toStdString()];
        for (const QJsonValue& id : it.value().toArray())
            ids.push_back(id.toString().toStdString());
    }
    const QJsonObject keyboards = index.value("keyboards").toObject();
    for (auto it = keyboards.begin(); it != keyboards.end(); ++it)
        _names[it.key().toStdString()] = it.value().toObject().value("name").toString(it.key());
    return !_models.empty();
}

std::vector<std::string> KeyboardPictures::forModel(const std::string& model) const
{
    const auto it = _models.find(model);
    return it == _models.end() ? std::vector<std::string>{} : it->second;
}

QString KeyboardPictures::name(const std::string& id) const
{
    const auto it = _names.find(id);
    return it == _names.end() ? QString::fromStdString(id) : it->second;
}

bool KeyboardPictures::picture(const std::string& id, KeyboardPicture& out, QString& error) const
{
    const QString base = _root + "/" + QString::fromStdString(id);
    const QJsonObject map = ReadJson(base + ".keys.json");
    if (map.isEmpty())
    {
        error = QStringLiteral("no key map %1.keys.json").arg(base);
        return false;
    }
    out = {};
    out.id = id;
    out.name = name(id);
    out.svgPath = base + ".photo.svg";
    const QJsonArray size = map.value("size").toArray();
    out.size = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
    for (const QJsonValue& value : map.value("keys").toArray())
    {
        const QJsonObject k = value.toObject();
        PictureKey key;
        key.id = k.value("id").toString().toStdString();
        key.label = k.value("label").toString(QString::fromStdString(key.id));
        const QJsonArray r = k.value("rect").toArray();
        key.rect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble());
        for (const QJsonValue& p : k.value("shape").toArray())
            key.shape << QPointF(p.toArray().at(0).toDouble(), p.toArray().at(1).toDouble());
        for (const QJsonValue& name : k.value("zx").toArray())
        {
            const ZXKeysEnum zx = DebugKeyboardManager::ResolveKeyName(name.toString().toStdString());
            if (zx == ZXKEY_NONE)
            {
                error = QStringLiteral("%1: key %2 presses an unknown ZX key %3")
                            .arg(QString::fromStdString(id), key.label, name.toString());
                return false;
            }
            key.zx.push_back(zx);
        }
        if (k.contains("pc"))
        {
            key.pc = pckey::FromName(k.value("pc").toString().toStdString());
            if (key.pc == PcKey::None)
            {
                error = QStringLiteral("%1: key %2 presses an unknown PC key %3")
                            .arg(QString::fromStdString(id), key.label, k.value("pc").toString());
                return false;
            }
            out.pc = true;
        }
        key.line = k.value("line").toString().toStdString();
        out.keys.push_back(std::move(key));
    }
    return true;
}
