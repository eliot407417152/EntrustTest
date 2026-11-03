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

    QString autoImageKeyFor(const QString& elementId) { return QStringLiteral("@image:") + elementId; }
    QString autoImageNoteKeyFor(const QString& elementId) { return QStringLiteral("@imageNote:") + elementId; }

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

    // ------------------------------------------------------------ ImageMatcherPool ---

    QString ImageMatcherPool::normalize(const QString& folder) {
        const QString t = folder.trimmed();
        return t.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(t));
    }

    bool ImageMatcherPool::ensure(const QString& folder) {
        const QString key = normalize(folder);
        if (key.isEmpty()) return false;
        if (byFolder_.contains(key)) return true;
        ImageMatcher m;
        if (!m.setFolder(folder.trimmed()) || !m.hasFolder()) return false;     // 資料夾不存在：不快取，之後建立了還能再試
        byFolder_.insert(key, m);
        return true;
    }

    void ImageMatcherPool::ensureAll(const CardLayout& layout) {
        for (const Element& e : layout.elements)
            if (e.type == ElementType::Image && e.imageSource == ImageSource::Dynamic) ensure(e.imageFolder);
    }

    void ImageMatcherPool::rescan(const QString& folder) {
        const QString key = normalize(folder);
        if (key.isEmpty()) return;
        auto it = byFolder_.find(key);
        if (it != byFolder_.end()) it->rescan();
        else ensure(folder);
    }

    const ImageMatcher* ImageMatcherPool::find(const QString& folder) const {
        const auto it = byFolder_.constFind(normalize(folder));
        return it == byFolder_.constEnd() ? nullptr : &it.value();
    }

    QString ImageMatcherPool::folderProblem(const QString& folder) const {
        if (folder.trimmed().isEmpty()) return QStringLiteral("尚未設定圖片資料夾");
        if (!find(folder)) return QStringLiteral("找不到圖片資料夾：%1").arg(QDir::toNativeSeparators(folder.trimmed()));
        return QString();
    }

    ImageMatch ImageMatcherPool::match(const QString& folder, const QString& value) const {
        const ImageMatcher* m = find(folder);
        return m ? m->find(value) : ImageMatch();
    }

    void ImageMatcherPool::applyTo(Record& rec, const CardLayout& layout) const {
        for (const Element& e : layout.elements) {
            if (e.type != ElementType::Image || e.imageSource != ImageSource::Dynamic) continue;
            QString path, note;
            const QString problem = folderProblem(e.imageFolder);
            if (!problem.isEmpty()) {
                note = QStringLiteral("[%1]").arg(problem);
            }
            else {
                const ImageMatch m = match(e.imageFolder, rec.value(e.matchField));
                path = m.path;
                note = m.note();
            }
            rec.insert(autoImageKeyFor(e.id), path);
            rec.insert(autoImageNoteKeyFor(e.id), note);
        }
    }

} // namespace card