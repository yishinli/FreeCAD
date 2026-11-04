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

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <QObject>
#include <QPointer>
#include <QString>

#include <fastsignals/connection.h>
#include <FCGlobal.h>

class QFile;
class QFileSystemWatcher;
class QTimer;

namespace App
{
class Document;
}

namespace Gui
{
class MDIView;
class ReloadBar;

/// What a project file on disk looks like, enough to tell whether something else rewrote it.
struct GuiExport FileSignature
{
    qint64 modified = -1;  ///< ms since the epoch, -1 when the file could not be read
    qint64 size = -1;
    /// Of the names, CRCs and sizes in the zip's central directory: equal for a copy or a touch
    quint64 digest = 0;
    /// A whole zip holding Document.xml, so restore() can read it
    bool complete = false;

    bool sameStat(const FileSignature& other) const
    {
        return modified == other.modified && size == other.size;
    }
    /// The same content: by the digest when both are complete, else by the stat
    bool sameContent(const FileSignature& other) const
    {
        if (complete && other.complete) {
            return digest == other.digest;
        }
        return sameStat(other);
    }
};

/// Stat the file and read the zip's central directory (no decompression).
GuiExport FileSignature readFileSignature(const QString& path);

/// Open the file for reading without keeping others from renaming it away or deleting it
/// meanwhile, as FreeCAD's save does before it renames the new file in. QFile keeps them from it on
/// Windows. There this is done only on volumes where a delete frees the name at once (NTFS, not
/// SMB shares), and a rename over the open file still fails. The file stays readable as it was.
GuiExport bool openSharedForReading(QFile& file, const QString& path);

/// Copy 'source' to 'target', reading it as openSharedForReading() does.
GuiExport bool copySharedFile(const QString& source, const QString& target);

/**
 * Notices when the file of an open document is rewritten by another program and offers to
 * reload it, in a bar above each of the document's views (BaseApp/Preferences/Document,
 * ExternalChange: 0 ask, 1 reload by itself when nothing is unsaved, 2 off).
 *
 * A change counts once the file has kept the same size and time for a second and reads as a
 * whole project. Reloading reads a local copy, so a writer starting again cannot leave the
 * document half restored, and keeps the camera of the active view.
 */
class GuiExport DocumentWatcher: public QObject
{
    Q_OBJECT

public:
    static DocumentWatcher* instance();

    /// Whether the file changed on disk since it was opened, reloaded or saved here; Ignore and
    /// "keep my version" do not count, so saving over such a change still asks.
    bool changedOnDisk(const App::Document* doc) const;

    void reload(const std::string& docName);
    void ignore(const std::string& docName);
    /// Show or take down the bars after the ExternalChange preference changed
    void preferencesChanged();

private:
    explicit DocumentWatcher(QObject* parent);
    ~DocumentWatcher() override;

    enum class Phase
    {
        Idle,
        Settling,  // changed, waiting for the writer to finish
        Changed,   // the bar shows, or a reload waits for a quiet moment
        Reloading,
        Failed
    };

    struct Entry
    {
        QString path;
        FileSignature loaded;   // as opened, reloaded or saved here
        FileSignature seen;     // the newest settled change
        FileSignature ignored;  // what Ignore was pressed for
        FileSignature settle;   // the last stat while settling
        Phase phase = Phase::Idle;
        int settleChecks = 0;
        int generation = 0;  // drops results of checks started before a reload or save
        bool checking = false;
        bool stillWriting = false;
        QString failure;

        /// A change the bar shows: not one Ignore or "keep my version" was pressed for
        bool offered() const
        {
            return (phase == Phase::Changed || phase == Phase::Failed) && !seen.sameContent(ignored);
        }
    };

    void track(const App::Document& doc);
    void forget(const std::string& docName);
    void watchPath(const QString& path);
    void unwatchPath(const QString& path);
    void onPathChanged(const QString& path);
    void checkAll();
    void check(const std::string& docName);
    void onChecked(const std::string& docName, int generation, const FileSignature& sig);
    void settled(const std::string& docName);
    void decide();
    bool busy() const;
    void startReload(const std::vector<std::string>& docNames, int attempt = 0);
    void finishReload(
        std::vector<std::string> pending,
        const std::string& docName,
        const QString& copy,
        const FileSignature& sig,
        const QString& error
    );
    void reloadStep(
        std::vector<std::string> pending,
        const std::string& docName,
        const QString& copy,
        const FileSignature& sig,
        const QString& error
    );
    bool restoreCopy(App::Document* doc, const QString& copy, QString& failure);
    void copyNext(std::vector<std::string> pending);
    void abortReload(const QString& reason, bool failed);
    void afterReload();
    std::set<App::Document*> affectedBy(const std::vector<std::string>& docNames) const;
    void openMissingLinks(App::Document* doc);
    std::vector<std::string> changedFor(const std::string& docName) const;
    void updateBars();
    void attachBar(MDIView* view);
    int mode() const;

    std::map<std::string, Entry> entries;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* settleTimer = nullptr;
    QTimer* decideTimer = nullptr;
    QTimer* pollTimer = nullptr;
    std::vector<std::string> reloading;          // the documents of the reload under way, in order
    // Unmodified before the next file of the reload is restored, by document affected
    std::map<std::string, bool> cleanBeforeStep;
    std::set<std::string> touchedByReload;       // marked modified by a file the reload restored
    bool retryPending = false;                   // a reload waits for a panel to close
    std::map<std::string, std::string> cameras;  // of the active view, by document
    bool reloadFailed = false;
    std::vector<QPointer<ReloadBar>> bars;
    std::vector<fastsignals::scoped_connection> connections;

    static DocumentWatcher* self;
};

}  // namespace Gui
