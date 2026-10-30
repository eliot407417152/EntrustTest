#include "ImageMatcher.h"

#include <QDir>
#include <QFileInfo>

namespace card {

    const QString& autoImageKey() {
        static const QString k = QStringLiteral("@image");
        return k;
    }
    const QString& autoImageNoteKey() {
        static const QString k = QStringLiteral("@imageNote");
        return k;
    }

    QString ImageMatch::note() const {
        switch (status) {
        case NoValue:   return QStringLiteral("[欄位無資料]");
        case NotFound:  return QStringLiteral("[找不到圖片]");
        case Ambiguous: return QStringLiteral("[%1 張圖片符合，請改用其他欄位]").arg(candidates.size());
        default:        return QString();
        }
    }

    bool ImageMatcher::setFolder(const QString& dir) {
        folder_ = dir.trimmed();
        files_.clear();
        if (folder_.isEmpty()) return true;
        if (!QDir(folder_).exists()) { folder_.clear(); return false; }
        rescan();
        return true;
    }

    void ImageMatcher::rescan() {
        files_.clear();
        if (folder_.isEmpty()) return;
        static const QStringList exts{ QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                       QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tif"),
                                       QStringLiteral("tiff") };
        const QFileInfoList list = QDir(folder_).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo& fi : list) {
            if (!exts.contains(fi.suffix().toLower())) continue;
            files_.append({ fi.fileName(), fi.absoluteFilePath(), fi.completeBaseName().toLower() });
        }
    }

    ImageMatch ImageMatcher::find(const QString& value) const {
        ImageMatch m;
        const QString key = value.trimmed().toLower();
        if (key.isEmpty()) return m;                       // NoValue
        m.status = ImageMatch::NotFound;

        QList<const File*> hits;
        for (const File& f : files_) if (f.baseLower == key) hits.append(&f);      // 先看完全相等
        if (hits.isEmpty())
            for (const File& f : files_) if (f.baseLower.contains(key)) hits.append(&f);

        for (const File* f : hits) m.candidates << f->name;
        if (hits.size() == 1) { m.status = ImageMatch::Unique; m.path = hits[0]->path; }
        else if (hits.size() > 1) m.status = ImageMatch::Ambiguous;
        return m;
    }

    void ImageMatcher::applyTo(Record& rec, const QString& field) const {
        rec.remove(autoImageKey());
        rec.remove(autoImageNoteKey());
        if (!hasFolder()) return;
        const ImageMatch m = find(rec.value(field));
        rec.insert(autoImageKey(), m.path);
        rec.insert(autoImageNoteKey(), m.note());
    }

} // namespace card