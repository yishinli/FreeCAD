// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Yishin Li
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include <algorithm>
#include <set>
#include <vector>

#include <FCConfig.h>

#ifdef FC_OS_WIN32
# include <windows.h>
# include <fcntl.h>
# include <io.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QThreadPool>
#include <QTimer>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Parameter.h>
#include <Base/Sequencer.h>

#include "Application.h"
#include "BitmapFactory.h"
#include "Control.h"
#include "Document.h"
#include "DocumentWatcher.h"
#include "MainWindow.h"
#include "MDIViewWithCamera.h"
#include "TaskView/TaskDialog.h"

using namespace Gui;

namespace
{

constexpr int SettleMs = 1000;
constexpr int SettleLimit = 30;  // checks, about half a minute
constexpr int PollMs = 5000;
constexpr int RetryMs = 500;

quint16 u16(const QByteArray& data, int pos)
{
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + pos);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray& data, int pos)
{
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + pos);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// FNV-1a
void mix(quint64& hash, const char* data, int size)
{
    for (int i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= 1099511628211ULL;
    }
}

FileSignature statFile(const QString& path)
{
    FileSignature sig;
    QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        sig.modified = info.lastModified().toMSecsSinceEpoch();
        sig.size = info.size();
    }
    return sig;
}

// Shown after the tab title while the bar shows (an escape: MSVC may read the source as cp950)
QString titleMark()
{
    return QStringLiteral(" ") + QChar(0x27F3);
}

App::Document* documentByName(const std::string& name)
{
    return App::GetApplication().getDocument(name.c_str());
}

// The open documents that link to 'doc', directly or through others
std::vector<App::Document*> usersOf(App::Document* doc)
{
    std::vector<App::Document*> users;
    for (auto other : App::GetApplication().getDocuments()) {
        if (other == doc) {
            continue;
        }
        try {
            auto deps = other->getDependentDocuments(false);
            if (std::find(deps.begin(), deps.end(), doc) != deps.end()) {
                users.push_back(other);
            }
        }
        catch (const Base::Exception&) {
        }
    }
    return users;
}

std::vector<App::Document*> dependenciesOf(App::Document* doc)
{
    try {
        return doc->getDependentDocuments(true);  // the dependencies first, 'doc' last
    }
    catch (const Base::Exception& e) {
        e.reportException();
        return {doc};
    }
}

QString fileNameOf(const App::Document* doc)
{
    return QFileInfo(QString::fromUtf8(doc->FileName.getValue())).fileName();
}

}  // namespace

FileSignature Gui::readFileSignature(const QString& path)
{
    FileSignature sig = statFile(path);
    constexpr int eocdSize = 22;
    if (sig.size < eocdSize) {
        return sig;
    }
    QFile file;
    if (!openSharedForReading(file, path)) {
        return sig;
    }

    // The end of central directory record, followed by a comment of up to 64 KiB
    const qint64 tailSize = std::min<qint64>(sig.size, eocdSize + 0xFFFF);
    if (!file.seek(sig.size - tailSize)) {
        return sig;
    }
    const QByteArray tail = file.read(tailSize);
    if (tail.size() != tailSize) {
        return sig;
    }
    int eocd = -1;
    for (int i = int(tail.size()) - eocdSize; i >= 0; --i) {
        if (u32(tail, i) == 0x06054b50 && i + eocdSize + u16(tail, i + 20) == tail.size()) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        return sig;
    }
    const quint16 count = u16(tail, eocd + 10);
    const quint32 dirSize = u32(tail, eocd + 12);
    const quint32 dirOffset = u32(tail, eocd + 16);
    const qint64 eocdOffset = sig.size - tailSize + eocd;
    if (count == 0xFFFF || dirOffset == 0xFFFFFFFF  // zip64, which FreeCAD does not write
        || qint64(dirOffset) + dirSize > eocdOffset || !file.seek(dirOffset)) {
        return sig;
    }
    const QByteArray dir = file.read(dirSize);
    if (dir.size() != qint64(dirSize)) {
        return sig;
    }

    constexpr int headerSize = 46;
    quint64 digest = 14695981039346656037ULL;
    bool document = false;
    int pos = 0;
    for (int i = 0; i < count; ++i) {
        if (pos + headerSize > dir.size() || u32(dir, pos) != 0x02014b50) {
            return sig;
        }
        const int nameSize = u16(dir, pos + 28);
        const int next = pos + headerSize + nameSize + u16(dir, pos + 30) + u16(dir, pos + 32);
        if (next > dir.size()) {
            return sig;
        }
        const char* name = dir.constData() + pos + headerSize;
        document = document || QByteArray(name, nameSize) == "Document.xml";
        mix(digest, name, nameSize);
        mix(digest, dir.constData() + pos + 16, 4);  // CRC-32
        mix(digest, dir.constData() + pos + 24, 4);  // uncompressed size
        pos = next;
    }
    sig.digest = digest;
    sig.complete = document;
    return sig;
}

#ifdef FC_OS_WIN32
namespace
{

# ifndef FILE_SUPPORTS_POSIX_UNLINK_RENAME
#  define FILE_SUPPORTS_POSIX_UNLINK_RENAME 0x00000400
# endif

HANDLE openForReading(const wchar_t* path, DWORD share)
{
    return CreateFileW(path, GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace
#endif

bool Gui::openSharedForReading(QFile& file, const QString& path)
{
#ifdef FC_OS_WIN32
    const QString native = QDir::toNativeSeparators(path);
    const auto name = reinterpret_cast<const wchar_t*>(native.utf16());
    HANDLE handle = openForReading(name, FILE_SHARE_READ | FILE_SHARE_WRITE);
    // Share delete only where a delete frees the name at once (NTFS). On an SMB share or FAT the
    // name stays taken while the file is open here: a save with backups off deletes the file,
    // cannot rename the new one in, and the old one is gone once closed here. There the delete is
    // refused, as Qt does, and the save fails with the file kept.
    DWORD flags = 0;
    if (handle != INVALID_HANDLE_VALUE
        && GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0)
        && (flags & FILE_SUPPORTS_POSIX_UNLINK_RENAME)) {
        HANDLE shared = openForReading(name, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        if (shared != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = shared;
        }
    }
    if (handle != INVALID_HANDLE_VALUE) {
        // The descriptor is made in this CRT and read and closed in Qt's: one shared ucrtbase
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
        if (fd < 0) {
            CloseHandle(handle);
        }
        else if (file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
            return true;
        }
        else {
            _close(fd);
        }
    }
    // A path CreateFileW does not take (too long without the \\?\ prefix): as Qt opens it
#endif
    file.setFileName(path);
    return file.open(QIODevice::ReadOnly);
}

bool Gui::copySharedFile(const QString& source, const QString& target)
{
    QFile in;
    if (!openSharedForReading(in, source)) {
        return false;
    }
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    std::vector<char> buffer(1 << 20);
    bool ok = true;
    for (;;) {
        const qint64 count = in.read(buffer.data(), qint64(buffer.size()));
        if (count <= 0) {
            ok = count == 0;
            break;
        }
        if (out.write(buffer.data(), count) != count) {
            ok = false;
            break;
        }
    }
    ok = out.flush() && ok;
    out.close();
    if (!ok) {
        out.remove();
    }
    return ok;
}

namespace Gui
{

/// The bar above a view of a document whose file, or a file it uses, changed on disk.
class ReloadBar: public QFrame
{
public:
    ReloadBar(MDIView* view, std::string docName)
        : QFrame(view)
        , docName(std::move(docName))
    {
        setObjectName(QStringLiteral("DocumentReloadBar"));
        auto layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        icon = new QLabel(this);
        icon->setPixmap(
            BitmapFactory().pixmap("Std_Revert").scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        );
        text = new QLabel(this);
        text->setWordWrap(true);
        reloadButton = new QPushButton(this);
        ignoreButton = new QPushButton(this);
        for (auto button : {reloadButton, ignoreButton}) {
            button->setFocusPolicy(Qt::NoFocus);
            button->setAutoDefault(false);
        }
        layout->addWidget(icon);
        layout->addWidget(text, 1);
        layout->addWidget(reloadButton);
        layout->addWidget(ignoreButton);

        connect(reloadButton, &QPushButton::clicked, this, [this]() {
            DocumentWatcher::instance()->reload(this->docName);
        });
        connect(ignoreButton, &QPushButton::clicked, this, [this]() {
            DocumentWatcher::instance()->ignore(this->docName);
        });
    }

    void set(const QString& message, bool unsaved, bool working, bool failed)
    {
        const QString state
            = QStringLiteral("%1|%2|%3|%4").arg(message).arg(unsaved).arg(working).arg(failed);
        if (state == shown) {
            return;
        }
        shown = state;
        text->setText(message);
        reloadButton->setEnabled(!working);
        ignoreButton->setEnabled(!working);
        if (failed) {
            reloadButton->setText(DocumentWatcher::tr("Retry"));
            ignoreButton->setText(DocumentWatcher::tr("Ignore"));
        }
        else if (unsaved) {
            reloadButton->setText(DocumentWatcher::tr("Reload and discard changes"));
            ignoreButton->setText(DocumentWatcher::tr("Keep my version"));
        }
        else {
            reloadButton->setText(DocumentWatcher::tr("Reload"));
            ignoreButton->setText(DocumentWatcher::tr("Ignore"));
        }

        // Amber asks for a decision, orange for one that loses work; neither is a 3D view colour
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const bool strong = unsaved || failed;
        const char* back = dark ? (strong ? "#5A2A0C" : "#4A3800") : (strong ? "#FDE2CF" : "#FFF4CE");
        const char* fore = dark ? (strong ? "#FFD9C2" : "#FFE9A8") : (strong ? "#5A2600" : "#3D2E00");
        setStyleSheet(QStringLiteral(
                          "#DocumentReloadBar { background-color: %1; }"
                          "#DocumentReloadBar > QLabel { color: %2; }"
        )
                          .arg(QLatin1String(back), QLatin1String(fore)));
    }

    void slideIn()
    {
        auto animation = new QPropertyAnimation(this, "maximumHeight", this);
        animation->setDuration(150);
        animation->setStartValue(0);
        animation->setEndValue(sizeHint().height());
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            setMaximumHeight(QWIDGETSIZE_MAX);
        });
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }

    const std::string& document() const
    {
        return docName;
    }

private:
    std::string docName;
    QString shown;
    QLabel* icon = nullptr;
    QLabel* text = nullptr;
    QPushButton* reloadButton = nullptr;
    QPushButton* ignoreButton = nullptr;
};

}  // namespace Gui

DocumentWatcher* DocumentWatcher::self = nullptr;

DocumentWatcher* DocumentWatcher::instance()
{
    if (!self) {
        self = new DocumentWatcher(QApplication::instance());
    }
    return self;
}

DocumentWatcher::DocumentWatcher(QObject* parent)
    : QObject(parent)
    , watcher(new QFileSystemWatcher(this))
    , settleTimer(new QTimer(this))
    , decideTimer(new QTimer(this))
    , pollTimer(new QTimer(this))
{
    connect(watcher, &QFileSystemWatcher::fileChanged, this, &DocumentWatcher::onPathChanged);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &DocumentWatcher::onPathChanged);

    settleTimer->setSingleShot(true);
    settleTimer->setInterval(SettleMs);
    connect(settleTimer, &QTimer::timeout, this, [this]() {
        for (auto& [name, entry] : entries) {
            if (entry.phase == Phase::Settling) {
                check(name);
            }
        }
    });
    decideTimer->setSingleShot(true);
    connect(decideTimer, &QTimer::timeout, this, &DocumentWatcher::decide);

    // Change notifications from network shares are unreliable: look while FreeCAD is in use
    pollTimer->setInterval(PollMs);
    connect(pollTimer, &QTimer::timeout, this, &DocumentWatcher::checkAll);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            checkAll();
            pollTimer->start();
        }
        else {
            pollTimer->stop();
        }
    });
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        pollTimer->start();
    }
    connect(qApp, &QCoreApplication::aboutToQuit, pollTimer, &QTimer::stop);

    auto& app = App::GetApplication();
    connections.emplace_back(app.signalFinishRestoreDocument.connect([this](const App::Document& doc) {
        if (std::find(reloading.begin(), reloading.end(), doc.getName()) == reloading.end()) {
            track(doc);
        }
    }));
    connections.emplace_back(app.signalFinishSaveDocument.connect(
        [this](const App::Document& doc, const std::string& file) {
            // Not a copy or a recovery file
            if (QFileInfo(QString::fromUtf8(file.c_str())).absoluteFilePath()
                == QFileInfo(QString::fromUtf8(doc.FileName.getValue())).absoluteFilePath()) {
                track(doc);
            }
        }
    ));
    connections.emplace_back(app.signalDeleteDocument.connect([this](const App::Document& doc) {
        // restore() deletes the document's contents before reading the file again
        if (std::find(reloading.begin(), reloading.end(), doc.getName()) == reloading.end()) {
            forget(doc.getName());
        }
    }));
    connections.emplace_back(
        Application::Instance->signalActivateView.connect([this](const MDIView* view) {
            if (view && view->getAppDocument()) {
                for (auto doc : dependenciesOf(view->getAppDocument())) {
                    check(doc->getName());
                }
                updateBars();
            }
        })
    );
    connections.emplace_back(
        Application::Instance->signalDeleteDocument.connect([this](const Gui::Document&) {
            QTimer::singleShot(0, this, &DocumentWatcher::updateBars);
        })
    );

    for (auto doc : app.getDocuments()) {
        track(*doc);
    }
}

DocumentWatcher::~DocumentWatcher()
{
    self = nullptr;
}

int DocumentWatcher::mode() const
{
    return int(App::GetApplication()
                   .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                   ->GetInt("ExternalChange", 0));
}

void DocumentWatcher::track(const App::Document& doc)
{
    const QString file = QString::fromUtf8(doc.FileName.getValue());
    if (file.isEmpty()) {
        return;
    }
    Entry& entry = entries[doc.getName()];
    const int generation = entry.generation + 1;
    const QString before = entry.path;
    entry = Entry();
    entry.path = QFileInfo(file).absoluteFilePath();
    if (!before.isEmpty() && before != entry.path) {  // saved under another name
        unwatchPath(before);
    }
    entry.loaded = readFileSignature(entry.path);
    entry.generation = generation;
    watchPath(entry.path);
    updateBars();
}

void DocumentWatcher::forget(const std::string& docName)
{
    auto it = entries.find(docName);
    if (it == entries.end()) {
        return;
    }
    const QString path = it->second.path;
    entries.erase(it);
    unwatchPath(path);
}

void DocumentWatcher::unwatchPath(const QString& path)
{
    const QString dir = QFileInfo(path).absolutePath();
    bool file = false;
    bool folder = false;
    for (auto& [name, entry] : entries) {
        file = file || entry.path == path;
        folder = folder || QFileInfo(entry.path).absolutePath() == dir;
    }
    if (!file) {
        watcher->removePath(path);
    }
    if (!folder) {
        watcher->removePath(dir);
    }
}

void DocumentWatcher::watchPath(const QString& path)
{
    // A file replaced by rename stops being watched, and for a moment it does not exist: the
    // folder tells when it is back
    if (QFileInfo::exists(path) && !watcher->files().contains(path)) {
        watcher->addPath(path);
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (!watcher->directories().contains(dir)) {
        watcher->addPath(dir);
    }
}

void DocumentWatcher::onPathChanged(const QString& path)
{
    for (auto& [name, entry] : entries) {
        if (entry.path == path || QFileInfo(entry.path).absolutePath() == path) {
            watchPath(entry.path);
            check(name);
        }
    }
}

void DocumentWatcher::checkAll()
{
    for (auto& [name, entry] : entries) {
        check(name);
    }
}

void DocumentWatcher::check(const std::string& docName)
{
    auto it = entries.find(docName);
    if (it == entries.end() || it->second.checking || it->second.phase == Phase::Reloading
        || mode() == 2) {
        return;
    }
    Entry& entry = it->second;
    entry.checking = true;
    QPointer<DocumentWatcher> watch(this);
    // On a share that went away a stat can take seconds: keep it off the GUI thread
    QThreadPool::globalInstance()->start([watch,
                                          docName,
                                          path = entry.path,
                                          known = entry.phase == Phase::Settling ? entry.settle
                                              : entry.phase == Phase::Idle       ? entry.loaded
                                                                                 : entry.seen,
                                          generation = entry.generation]() {
        FileSignature sig = statFile(path);
        if (sig.sameStat(known) && sig.modified >= 0) {
            sig = known;
        }
        else if (sig.modified >= 0) {
            sig = readFileSignature(path);
        }
        auto app = QCoreApplication::instance();
        if (!app) {  // quitting
            return;
        }
        QMetaObject::invokeMethod(
            app,
            [watch, docName, generation, sig]() {
                if (watch) {
                    watch->onChecked(docName, generation, sig);
                }
            },
            Qt::QueuedConnection
        );
    });
}

void DocumentWatcher::onChecked(const std::string& docName, int generation, const FileSignature& sig)
{
    auto it = entries.find(docName);
    if (it == entries.end()) {
        return;
    }
    Entry& entry = it->second;
    if (entry.generation != generation) {
        entry.checking = false;
        return;
    }
    entry.checking = false;

    switch (entry.phase) {
        case Phase::Reloading:
            return;
        case Phase::Settling:
            if (sig.modified >= 0 && sig.sameStat(entry.settle) && sig.complete) {
                entry.settle = sig;
                settled(docName);
                return;
            }
            entry.settle = sig;
            if (++entry.settleChecks >= SettleLimit && sig.modified < 0) {  // gone, or unreachable
                entry.phase = Phase::Idle;
                return;
            }
            if (entry.settleChecks >= SettleLimit) {
                // Show it anyway; Reload refuses a file that is still incomplete
                entry.stillWriting = true;
                entry.seen = sig;
                entry.phase = Phase::Changed;
                updateBars();
                return;
            }
            settleTimer->start();
            return;
        case Phase::Changed:
        case Phase::Failed:
            if (entry.stillWriting) {
                if (sig.modified >= 0 && sig.sameStat(entry.seen) && sig.complete) {
                    entry.settle = sig;
                    settled(docName);
                }
                else {
                    entry.seen = sig;
                }
                return;
            }
            // Gone or out of reach: not a change; the next look sees it when it is back
            if (sig.modified < 0 || sig.sameStat(entry.seen)) {
                return;
            }
            if (sig.sameStat(entry.loaded)) {  // put back as it was
                entry.phase = Phase::Idle;
                updateBars();
                return;
            }
            break;
        case Phase::Idle:
            if (sig.modified < 0 || sig.sameStat(entry.loaded)) {
                return;
            }
            break;
    }
    entry.phase = Phase::Settling;
    entry.settle = sig;
    entry.settleChecks = 0;
    settleTimer->start();
}

void DocumentWatcher::settled(const std::string& docName)
{
    Entry& entry = entries[docName];
    entry.stillWriting = false;
    if (entry.settle.sameContent(entry.loaded)) {  // copied or touched, not rewritten
        entry.loaded = entry.settle;
        entry.phase = Phase::Idle;
        updateBars();
        return;
    }
    entry.seen = entry.settle;
    entry.phase = Phase::Changed;
    entry.failure.clear();
    updateBars();
    decideTimer->start(0);
}

bool DocumentWatcher::busy() const
{
    auto& app = App::GetApplication();
    if (app.isRestoring() || Base::Sequencer().isRunning() || QApplication::activeModalWidget()
        || QApplication::activePopupWidget()) {
        return true;
    }
    for (auto doc : app.getDocuments()) {
        if (doc->testStatus(App::Document::Recomputing) || doc->isPerformingTransaction()) {
            return true;
        }
    }
    return false;
}

void DocumentWatcher::decide()
{
    if (mode() != 1) {
        return;
    }
    if (busy() || !reloading.empty() || retryPending) {
        decideTimer->start(RetryMs);
        return;
    }
    for (auto& [name, entry] : entries) {
        if (!entry.offered() || entry.phase != Phase::Changed || entry.stillWriting) {
            continue;
        }
        auto doc = documentByName(name);
        if (!doc) {
            continue;
        }
        // By itself only when nothing is lost: no unsaved change here and no panel open on it
        bool safe = true;
        for (auto affected : affectedBy({name})) {
            auto gdoc = Application::Instance->getDocument(affected);
            if (Control().activeDialog(affected) || (affected == doc && gdoc && gdoc->isModified())) {
                safe = false;
            }
        }
        if (safe) {
            startReload({name});
            return;  // one at a time; the next waits for this one
        }
    }
}

std::vector<std::string> DocumentWatcher::changedFor(const std::string& docName) const
{
    std::vector<std::string> names;
    auto doc = documentByName(docName);
    if (!doc) {
        return names;
    }
    for (auto dep : dependenciesOf(doc)) {
        auto it = entries.find(dep->getName());
        if (it != entries.end() && it->second.offered()) {
            names.emplace_back(dep->getName());
        }
    }
    return names;
}

std::set<App::Document*> DocumentWatcher::affectedBy(const std::vector<std::string>& docNames) const
{
    // The documents and those using them: their panels hold the objects and views
    std::set<App::Document*> affected;
    for (auto& name : docNames) {
        if (auto doc = documentByName(name)) {
            affected.insert(doc);
            for (auto user : usersOf(doc)) {
                affected.insert(user);
            }
        }
    }
    return affected;
}

void DocumentWatcher::ignore(const std::string& docName)
{
    for (auto& name : changedFor(docName)) {
        Entry& entry = entries[name];
        entry.ignored = entry.seen;
        if (entry.phase == Phase::Failed) {
            entry.phase = Phase::Changed;
        }
    }
    updateBars();
}

void DocumentWatcher::preferencesChanged()
{
    updateBars();
    checkAll();
}

void DocumentWatcher::reload(const std::string& docName)
{
    auto names = changedFor(docName);
    if (names.empty() || !reloading.empty() || retryPending) {
        return;
    }
    if (busy()) {
        QTimer::singleShot(RetryMs, this, [this, docName]() { reload(docName); });
        return;
    }
    startReload(names);
}

void DocumentWatcher::startReload(const std::vector<std::string>& docNames, int attempt)
{
    if (attempt > 0) {
        retryPending = false;
    }
    if (!reloading.empty() || retryPending) {
        return;
    }
    const auto affected = affectedBy(docNames);
    bool open = false;
    for (auto doc : affected) {
        if (Control().activeDialog(doc)) {
            Control().reject(doc);
            open = open || Control().activeDialog(doc);
        }
    }
    if (open) {
        if (attempt == 0) {  // a panel may close a moment after reject()
            retryPending = true;
            QTimer::singleShot(RetryMs, this, [this, docNames]() { startReload(docNames, 1); });
            return;
        }
        for (auto& name : docNames) {
            entries[name].phase = Phase::Failed;
            entries[name].failure = tr("Close the open task panel first.");
        }
        updateBars();
        return;
    }

    cleanBeforeStep.clear();
    touchedByReload.clear();
    cameras.clear();
    reloadFailed = false;
    for (auto doc : affected) {
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc) {
            continue;
        }
        cleanBeforeStep[doc->getName()] = !gdoc->isModified();
        if (std::find(docNames.begin(), docNames.end(), doc->getName()) == docNames.end()) {
            continue;
        }
        // The camera of the view in use, else of the first one
        auto view = dynamic_cast<MDIViewWithCamera*>(gdoc->getActiveView());
        for (auto mdi : gdoc->getMDIViews()) {
            if (!view) {
                view = dynamic_cast<MDIViewWithCamera*>(mdi);
            }
        }
        if (view) {
            cameras[doc->getName()] = view->getCamera();
        }
    }

    reloading = docNames;
    for (auto& name : docNames) {
        entries[name].phase = Phase::Reloading;
        entries[name].failure.clear();
    }
    updateBars();
    copyNext(docNames);
}

void DocumentWatcher::finishReload(
    std::vector<std::string> pending,
    const std::string& docName,
    const QString& copy,
    const FileSignature& sig,
    const QString& error
)
{
    // Whatever goes wrong, the reload ends: an unfinished one would refuse every later one
    try {
        reloadStep(std::move(pending), docName, copy, sig, error);
    }
    catch (...) {
        if (!copy.isEmpty()) {
            QFile::remove(copy);
        }
        reloadFailed = true;  // not known how far it got
        abortReload(tr("Unexpected error."), true);
    }
}

void DocumentWatcher::reloadStep(
    std::vector<std::string> pending,
    const std::string& docName,
    const QString& copy,
    const FileSignature& sig,
    const QString& error
)
{
    auto doc = documentByName(docName);
    auto it = entries.find(docName);
    if (!doc || it == entries.end()) {  // closed meanwhile
        if (!copy.isEmpty()) {
            QFile::remove(copy);
        }
        forget(docName);
        reloading.erase(std::remove(reloading.begin(), reloading.end(), docName), reloading.end());
        if (pending.empty()) {
            afterReload();
        }
        else {
            copyNext(std::move(pending));
        }
        return;
    }
    if (!error.isEmpty()) {
        it->second.failure = error;
        abortReload(error, true);
        return;
    }

    // The copy took a while: not from inside another operation, and not over work the user
    // started meanwhile
    if (busy()) {
        QTimer::singleShot(RetryMs, this, [this, pending, docName, copy, sig]() {
            finishReload(pending, docName, copy, sig, QString());
        });
        return;
    }
    for (auto affected : affectedBy(reloading)) {
        auto gdoc = Application::Instance->getDocument(affected);
        // Against how things were after the last file reloaded: reloading a linked file
        // marks the documents using it modified, which is not the user's work
        auto clean = cleanBeforeStep.find(affected->getName());
        if (Control().activeDialog(affected)
            || (gdoc && gdoc->isModified() && clean != cleanBeforeStep.end() && clean->second)) {
            QFile::remove(copy);
            abortReload(QString(), false);
            return;
        }
    }

    QString failure;
    const bool restored = restoreCopy(doc, copy, failure);
    QFile::remove(copy);
    if (!restored) {
        // restore() empties the document before reading: what uses it must not compute now
        reloadFailed = true;
        it->second.failure = failure;
        abortReload(failure, true);
        return;
    }
    for (auto affected : affectedBy(reloading)) {
        if (auto gdoc = Application::Instance->getDocument(affected)) {
            auto& clean = cleanBeforeStep[affected->getName()];
            if (clean && gdoc->isModified()) {
                touchedByReload.insert(affected->getName());
            }
            clean = !gdoc->isModified();
        }
    }
    it->second.loaded = sig;
    it->second.seen = FileSignature();
    it->second.ignored = FileSignature();
    it->second.phase = Phase::Idle;
    ++it->second.generation;
    watchPath(it->second.path);

    if (pending.empty()) {
        afterReload();
    }
    else {
        copyNext(std::move(pending));
    }
}

bool DocumentWatcher::restoreCopy(App::Document* doc, const QString& copy, QString& failure)
{
    const std::string name = doc->getName();
    const bool wasActive = getMainWindow()->activeWindow()
        && getMainWindow()->activeWindow()->getAppDocument() == doc;
    try {
        doc->restore(copy.toUtf8().constData());
    }
    catch (const Base::Exception& e) {
        failure = QString::fromUtf8(e.what());
    }
    catch (const std::exception& e) {
        failure = QString::fromUtf8(e.what());
    }
    catch (...) {
        failure = tr("Unexpected error.");
    }
    if (!failure.isEmpty()) {
        return false;
    }

    auto gdoc = Application::Instance->getDocument(doc);
    auto camera = cameras.find(name);
    if (gdoc && camera != cameras.end()) {
        for (auto mdi : gdoc->getMDIViews()) {
            if (auto view = dynamic_cast<MDIViewWithCamera*>(mdi)) {
                try {
                    view->setCamera(camera->second.c_str());
                }
                catch (...) {
                    Base::Console().warning("Reloading {}: the camera was not kept\n", name);
                }
                if (wasActive) {
                    getMainWindow()->setActiveWindow(view);
                }
                break;
            }
        }
    }
    try {
        openMissingLinks(doc);
    }
    catch (...) {
        Base::Console().warning("Reloading {}: files it newly links to were not opened\n", name);
    }
    return true;
}

void DocumentWatcher::copyNext(std::vector<std::string> pending)
{
    // Copy the next one off the GUI thread, then restore from the copy
    const std::string next = pending.front();
    pending.erase(pending.begin());
    auto it = entries.find(next);
    if (it == entries.end()) {
        finishReload(std::move(pending), next, QString(), FileSignature(), QString());
        return;
    }
    const QString target = QDir(QDir::tempPath())
                               .absoluteFilePath(QStringLiteral("FreeCAD-reload-%1-%2.FCStd")
                                                     .arg(QCoreApplication::applicationPid())
                                                     .arg(QString::fromStdString(next)));
    QPointer<DocumentWatcher> watch(this);
    QThreadPool::globalInstance()->start([watch, pending, next, source = it->second.path, target]() {
        QString error;
        FileSignature sig;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const FileSignature before = statFile(source);
            QFile::remove(target);
            if (before.modified < 0 || !copySharedFile(source, target)) {
                error = DocumentWatcher::tr("The file cannot be read.");
                continue;
            }
            if (!statFile(source).sameStat(before)) {  // written again while copying
                error = DocumentWatcher::tr("The file is still being written.");
                continue;
            }
            sig = readFileSignature(target);
            if (!sig.complete) {
                error = DocumentWatcher::tr("The file is not a complete FreeCAD document.");
                break;
            }
            sig.modified = before.modified;  // the copy's own time is not the file's
            error.clear();
            break;
        }
        if (!error.isEmpty()) {
            QFile::remove(target);
        }
        auto app = QCoreApplication::instance();
        if (!app) {  // quitting
            QFile::remove(target);
            return;
        }
        QMetaObject::invokeMethod(
            app,
            [watch, pending, next, target, sig, error]() {
                if (watch) {
                    watch->finishReload(pending, next, error.isEmpty() ? target : QString(), sig, error);
                }
                else {
                    QFile::remove(target);
                }
            },
            Qt::QueuedConnection
        );
    });
}

void DocumentWatcher::abortReload(const QString& reason, bool failed)
{
    // Back to offering the change: failed with the reason, or, when the user started working
    // meanwhile, as a plain change the bar shows again (orange if now unsaved)
    for (auto& name : reloading) {
        auto it = entries.find(name);
        if (it == entries.end() || it->second.phase != Phase::Reloading) {
            continue;
        }
        it->second.phase = failed && !it->second.failure.isEmpty() ? Phase::Failed : Phase::Changed;
    }
    if (failed && !reason.isEmpty()) {
        Base::Console().warning("Reloading failed: {}\n", reason.toStdString());
    }
    afterReload();
}

void DocumentWatcher::afterReload()
{
    // Linking to a rewritten file touches the objects using it, which marks their document
    // modified though the user changed nothing there. Not after a failed restore: they would
    // compute against what is missing, and the errors would hide behind "unmodified".
    if (!reloadFailed) {
        // Only those the reload marked: one the user changed meanwhile stays modified
        for (auto& name : touchedByReload) {
            auto doc = documentByName(name);
            auto gdoc = doc ? Application::Instance->getDocument(doc) : nullptr;
            if (!gdoc || std::find(reloading.begin(), reloading.end(), name) != reloading.end()) {
                continue;
            }
            try {
                if (doc->mustExecute()) {
                    doc->recompute();
                }
            }
            catch (...) {
                Base::Console().warning("Recomputing {} after a reload failed\n", name);
            }
            gdoc->setModified(false);
        }
    }
    // Documents closed while it ran
    for (auto& name : reloading) {
        if (!documentByName(name)) {
            forget(name);
        }
    }
    cleanBeforeStep.clear();
    touchedByReload.clear();
    cameras.clear();
    reloading.clear();
    reloadFailed = false;
    updateBars();
    decideTimer->start(0);
}

void DocumentWatcher::openMissingLinks(App::Document* doc)
{
    // restore() only resolves links to documents already open; opening a file is what loads
    // the files it links to
    const QDir base(QFileInfo(QString::fromUtf8(doc->FileName.getValue())).canonicalPath());
    std::vector<std::string> paths;
    auto add = [&](const App::PropertyXLink& link) {
        const char* file = link.getFilePath();
        if (link.getValue() || !file || !*file) {
            return;
        }
        QString path = QString::fromUtf8(file);
        if (path.startsWith(QLatin1String("https://"))) {
            return;
        }
        path = QDir::cleanPath(base.absoluteFilePath(path));
        const std::string utf8 = path.toUtf8().constData();
        if (QFileInfo::exists(path) && !App::GetApplication().getDocumentByPath(utf8.c_str())
            && std::find(paths.begin(), paths.end(), utf8) == paths.end()) {
            paths.push_back(utf8);
        }
    };
    for (auto obj : doc->getObjects()) {
        std::vector<App::Property*> props;
        obj->getPropertyList(props);
        for (auto prop : props) {
            if (auto link = dynamic_cast<App::PropertyXLink*>(prop)) {
                add(*link);
            }
            else if (auto list = dynamic_cast<App::PropertyXLinkSubList*>(prop)) {
                for (auto& sub : list->getSubListValues()) {
                    add(sub);
                }
            }
        }
    }
    if (paths.empty()) {
        return;
    }
    std::vector<std::string> errors;
    App::DocumentInitFlags flags;
    flags.createView = false;
    App::GetApplication().openDocuments(paths, nullptr, nullptr, &errors, flags);
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (!errors[i].empty()) {
            Base::Console().warning("Cannot open {}: {}\n", paths[i], errors[i]);
        }
    }
}

bool DocumentWatcher::changedOnDisk(const App::Document* doc) const
{
    auto it = entries.find(doc->getName());
    if (it == entries.end()) {
        return false;
    }
    const FileSignature now = statFile(it->second.path);
    if (now.modified < 0 || now.sameStat(it->second.loaded)) {
        return false;
    }
    return !readFileSignature(it->second.path).sameContent(it->second.loaded);
}

void DocumentWatcher::attachBar(MDIView* view)
{
    auto bar = dynamic_cast<ReloadBar*>(view->menuWidget());
    if (!bar) {
        bar = new ReloadBar(view, view->getAppDocument()->getName());
        view->setMenuWidget(bar);
        bars.emplace_back(bar);
        bar->slideIn();
    }
    if (!view->windowTitle().endsWith(titleMark())) {
        view->setWindowTitle(view->windowTitle() + titleMark());
    }
}

void DocumentWatcher::updateBars()
{
    const bool off = mode() == 2;
    std::set<MDIView*> shown;
    for (auto doc : App::GetApplication().getDocuments()) {
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc || off) {
            continue;
        }
        // This file first, then the files it uses
        const Entry* first = nullptr;
        const App::Document* firstDoc = nullptr;
        int count = 0;
        bool unsaved = gdoc->isModified();
        auto deps = dependenciesOf(doc);
        std::reverse(deps.begin(), deps.end());
        for (auto dep : deps) {
            auto it = entries.find(dep->getName());
            if (it == entries.end()) {
                continue;
            }
            const Entry& entry = it->second;
            if (entry.phase != Phase::Reloading && !entry.offered()) {
                continue;
            }
            if (!first) {
                first = &entry;
                firstDoc = dep;
            }
            ++count;
            if (dep != doc) {
                if (auto gdep = Application::Instance->getDocument(dep)) {
                    unsaved = unsaved || gdep->isModified();
                }
            }
        }
        if (!first) {
            continue;
        }

        QString message;
        const QString file = fileNameOf(firstDoc);
        if (first->phase == Phase::Reloading) {
            message = tr("Reloading %1...").arg(file);
        }
        else if (first->phase == Phase::Failed) {
            message = tr("Reloading %1 failed: %2").arg(file, first->failure);
        }
        else if (firstDoc == doc && count > 1) {
            message
                = tr("%1 and %n file(s) it uses were changed on disk.", nullptr, count - 1).arg(file);
        }
        else if (firstDoc == doc) {
            message = tr("%1 was changed on disk.").arg(file);
        }
        else {
            message = tr("%1, used by this file, was changed on disk.").arg(file);
        }
        if (first->stillWriting) {
            message += QLatin1Char(' ') + tr("It is still being written.");
        }
        if (unsaved && first->phase != Phase::Reloading) {
            message += QLatin1Char(' ') + tr("Reloading discards your unsaved changes.");
        }

        for (auto view : gdoc->getMDIViews()) {
            attachBar(view);
            if (auto bar = dynamic_cast<ReloadBar*>(view->menuWidget())) {
                bar->set(message, unsaved, first->phase == Phase::Reloading, first->phase == Phase::Failed);
            }
            shown.insert(view);
        }
    }

    // Take the bars down where nothing is left to show
    for (auto& bar : bars) {
        if (!bar) {
            continue;
        }
        auto view = qobject_cast<MDIView*>(bar->parentWidget());
        if (view && !shown.count(view)) {
            QString title = view->windowTitle();
            if (title.endsWith(titleMark())) {
                title.chop(titleMark().size());
                view->setWindowTitle(title);
            }
            view->setMenuWidget(nullptr);
            bar = nullptr;
        }
    }
    bars.erase(std::remove(bars.begin(), bars.end(), nullptr), bars.end());
}

#include "moc_DocumentWatcher.cpp"
