// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/DocumentWatcher.h>
#include <src/App/InitApplication.h>

namespace
{

class DocumentWatcherSignature: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        ASSERT_TRUE(dir.isValid());
        name = App::GetApplication().getUniqueDocumentName("watched");
        doc = App::GetApplication().newDocument(name.c_str(), name.c_str());
        doc->addObject("App::FeatureTest", "Feature");
        path = dir.filePath(QStringLiteral("watched.FCStd"));
        doc->saveAs(path.toUtf8().constData());
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(name.c_str());
    }

    QString copyTo(const QString& fileName, qint64 bytes = -1) const
    {
        QFile source(path);
        EXPECT_TRUE(source.open(QIODevice::ReadOnly));
        QByteArray data = source.readAll();
        if (bytes >= 0) {
            data.truncate(bytes);
        }
        const QString target = dir.filePath(fileName);
        QFile out(target);
        EXPECT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(data);
        return target;
    }

    QTemporaryDir dir;
    std::string name;
    App::Document* doc = nullptr;
    QString path;
};

}  // namespace

TEST_F(DocumentWatcherSignature, SavedDocumentIsComplete)
{
    const Gui::FileSignature sig = Gui::readFileSignature(path);
    EXPECT_TRUE(sig.complete);
    EXPECT_GT(sig.size, 0);
    EXPECT_GE(sig.modified, 0);
    EXPECT_TRUE(sig.sameContent(Gui::readFileSignature(path)));
}

TEST_F(DocumentWatcherSignature, CopyHasTheSameContent)
{
    const Gui::FileSignature original = Gui::readFileSignature(path);
    const Gui::FileSignature copy = Gui::readFileSignature(copyTo(QStringLiteral("copy.FCStd")));
    EXPECT_TRUE(copy.complete);
    EXPECT_EQ(copy.digest, original.digest);
    EXPECT_TRUE(copy.sameContent(original));
}

TEST_F(DocumentWatcherSignature, HalfWrittenFileIsNotComplete)
{
    const qint64 size = Gui::readFileSignature(path).size;
    const Gui::FileSignature half = Gui::readFileSignature(
        copyTo(QStringLiteral("half.FCStd"), size / 2)
    );
    EXPECT_FALSE(half.complete);
    EXPECT_EQ(half.size, size / 2);
    // Missing only the end of the central directory
    EXPECT_FALSE(Gui::readFileSignature(copyTo(QStringLiteral("cut.FCStd"), size - 1)).complete);
}

TEST_F(DocumentWatcherSignature, RewrittenDocumentDiffers)
{
    const Gui::FileSignature before = Gui::readFileSignature(path);
    doc->addObject("App::FeatureTest", "Another");
    doc->save();
    const Gui::FileSignature after = Gui::readFileSignature(path);
    EXPECT_TRUE(after.complete);
    EXPECT_NE(after.digest, before.digest);
    EXPECT_FALSE(after.sameContent(before));
}

TEST_F(DocumentWatcherSignature, OtherFiles)
{
    const QString junk = dir.filePath(QStringLiteral("junk.FCStd"));
    QFile out(junk);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(QByteArray(100, 'x'));
    out.close();
    EXPECT_FALSE(Gui::readFileSignature(junk).complete);
    EXPECT_EQ(Gui::readFileSignature(junk).size, 100);

    const Gui::FileSignature missing = Gui::readFileSignature(dir.filePath(QStringLiteral("none")));
    EXPECT_EQ(missing.modified, -1);
    EXPECT_FALSE(missing.complete);
}

// FreeCAD's save never renames over the old file: it renames it away (backups on) or deletes
// it (backups off), then renames the new one in. On Windows a rename over a file that is open
// fails, however it was opened.
TEST_F(DocumentWatcherSignature, SaveWithBackupWhileOpen)
{
    QFile original(path);
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    const QByteArray before = original.readAll();
    original.close();

    QFile file;
    ASSERT_TRUE(Gui::openSharedForReading(file, path));
    const QString other = copyTo(QStringLiteral("other.FCStd"), 100);
    const QString backup = dir.filePath(QStringLiteral("watched.FCBak"));
    std::error_code error;
    std::filesystem::rename(path.toStdWString(), backup.toStdWString(), error);
    EXPECT_FALSE(error) << error.message();
    std::filesystem::rename(other.toStdWString(), path.toStdWString(), error);
    EXPECT_FALSE(error) << error.message();
    EXPECT_EQ(QFileInfo(path).size(), 100);
    // What was open stays readable as it was
    EXPECT_EQ(file.readAll(), before);
}

TEST_F(DocumentWatcherSignature, SaveWithoutBackupWhileOpen)
{
    QFile original(path);
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    const QByteArray before = original.readAll();
    original.close();

    QFile file;
    ASSERT_TRUE(Gui::openSharedForReading(file, path));
    const QString other = copyTo(QStringLiteral("other.FCStd"), 100);
    std::error_code error;
    std::filesystem::remove(path.toStdWString(), error);
    EXPECT_FALSE(error) << error.message();
    std::filesystem::rename(other.toStdWString(), path.toStdWString(), error);
    EXPECT_FALSE(error) << error.message();
    EXPECT_EQ(file.readAll(), before);
    file.close();
    EXPECT_EQ(QFileInfo(path).size(), 100);
}

TEST_F(DocumentWatcherSignature, SharedCopy)
{
    const QString target = dir.filePath(QStringLiteral("shared.FCStd"));
    ASSERT_TRUE(Gui::copySharedFile(path, target));
    EXPECT_TRUE(Gui::readFileSignature(target).sameContent(Gui::readFileSignature(path)));
    EXPECT_EQ(QFileInfo(target).size(), QFileInfo(path).size());

    EXPECT_FALSE(Gui::copySharedFile(dir.filePath(QStringLiteral("none")), target));
}
