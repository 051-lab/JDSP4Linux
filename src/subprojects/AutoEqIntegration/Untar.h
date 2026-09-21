#ifndef UNTAR_H
#define UNTAR_H

#include <QDebug>
#include <QString>
#include <QDir>
#include <QSet>

#include <functional>

#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class Untar
{
public:
    static int extract(const QString& qFilename, const QDir& outputPath, QString& returnState,
                       const std::function<bool()>& shouldStop = {})
    {
        const QByteArray filename = qFilename.toUtf8();

        struct archive *a = nullptr;
        struct archive *ext = nullptr;
        struct archive_entry *entry;
        QSet<QString> members;
        quint64 totalBytes = 0;
        int flags;
        int r;

        auto cleanup = [&]() {
            if (a)
            {
                archive_read_close(a);
                archive_read_free(a);
            }
            if (ext)
            {
                archive_write_close(ext);
                archive_write_free(ext);
            }
        };

        /* Select which attributes we want to restore. */
        flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT |
                ARCHIVE_EXTRACT_SECURE_SYMLINKS;

        a = archive_read_new();
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
        ext = archive_write_disk_new();
        archive_write_disk_set_options(ext, flags);
        archive_write_disk_set_standard_lookup(ext);
        if ((r = archive_read_open_filename(a, filename.constData(), 10240)))
        {
            returnState = QString::fromUtf8(archive_error_string(a));
            qWarning() << returnState << "\n";
            cleanup();
            return (1);
        }
        for (;;) {
            if (shouldStop && shouldStop())
            {
                returnState = QStringLiteral("Extraction cancelled");
                cleanup();
                return 1;
            }
            r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r < ARCHIVE_OK)
                qWarning() << archive_error_string(a);
            if (r < ARCHIVE_WARN)
            {
                returnState = QString::fromUtf8(archive_error_string(a));
                cleanup();
                return 1;
            }

            const char* currentFile = archive_entry_pathname(entry);
            const QString member = currentFile ? QString::fromUtf8(currentFile) : QString();
            if (!currentFile || !is_safe_member(member) ||
                (archive_entry_filetype(entry) != AE_IFREG &&
                 archive_entry_filetype(entry) != AE_IFDIR) ||
                archive_entry_hardlink(entry) != nullptr)
            {
                returnState = QStringLiteral("Archive contains an unsafe member");
                cleanup();
                return 1;
            }
            const QString normalizedMember = QDir::cleanPath(member);
            if (members.contains(normalizedMember))
            {
                returnState = QStringLiteral("Archive contains a duplicate member");
                cleanup();
                return 1;
            }
            members.insert(normalizedMember);
            const la_int64_t memberSize = archive_entry_size(entry);
            if (memberSize < 0 || static_cast<quint64>(memberSize) > maxMemberBytes ||
                totalBytes > maxTotalBytes - static_cast<quint64>(memberSize))
            {
                returnState = QStringLiteral("Archive exceeds extraction size limits");
                cleanup();
                return 1;
            }
            const QString fullOutputPath = outputPath.path() + QDir::separator() + member;
            const QByteArray outputFilename = fullOutputPath.toUtf8();
            archive_entry_set_pathname(entry, outputFilename.constData());

            r = archive_write_header(ext, entry);
            if (r < ARCHIVE_OK)
            {
                returnState = QString::fromUtf8(archive_error_string(ext));
                if (returnState.isEmpty())
                    returnState = QStringLiteral("Cannot write archive member header");
                cleanup();
                return 1;
            }
            else if (memberSize > 0) {
                r = copy_data(a, ext, static_cast<quint64>(memberSize), totalBytes, shouldStop);
                if (shouldStop && shouldStop())
                {
                    returnState = QStringLiteral("Extraction cancelled");
                    cleanup();
                    return 1;
                }
                if (r < ARCHIVE_OK)
                    qWarning() << archive_error_string(ext);
                if (r < ARCHIVE_WARN)
                {
                    returnState = QString::fromUtf8(archive_error_string(ext));
                    cleanup();
                    return 1;
                }
            }
            r = archive_write_finish_entry(ext);
            if (r < ARCHIVE_OK)
                qWarning() << archive_error_string(ext);
            if (r < ARCHIVE_WARN)
            {
                returnState = QString::fromUtf8(archive_error_string(ext));
                cleanup();
                return 1;
            }
        }
        cleanup();
        return 0;
    }

private:
    static constexpr quint64 maxMemberBytes = 64ULL * 1024ULL * 1024ULL;
    static constexpr quint64 maxTotalBytes = 512ULL * 1024ULL * 1024ULL;

    static bool is_safe_member(const QString& member)
    {
        if (member.isEmpty() || member.startsWith('/') || member.contains('\0'))
            return false;
        const QString clean = QDir::cleanPath(member);
        return clean != "." && clean != ".." && !clean.startsWith("../") &&
               !clean.contains("/../") && !clean.endsWith("/..") &&
               !clean.startsWith('~');
    }

    static int
    copy_data(struct archive *ar, struct archive *aw, quint64 memberSize, quint64& totalBytes,
              const std::function<bool()>& shouldStop)
    {
        int r;
        const void *buff;
        size_t size;
        int64_t offset;

        for (;;) {
            if (shouldStop && shouldStop())
                return ARCHIVE_FATAL;
            r = archive_read_data_block(ar, &buff, &size, &offset);
            if (r == ARCHIVE_EOF)
                return memberSize == 0 ? ARCHIVE_OK : ARCHIVE_FATAL;
            if (r < ARCHIVE_OK) {
                qWarning() << "copyData(): " << archive_error_string(ar) << "\n";
                return (r);
            }
            if (size > memberSize || totalBytes > maxTotalBytes - size)
                return ARCHIVE_FATAL;
            r = archive_write_data_block(aw, buff, size, offset);
            if (r < ARCHIVE_OK) {
                qWarning() << "copyData(): " << archive_error_string(ar) << "\n";
                return (r);
            }
            memberSize -= size;
            totalBytes += size;
        }
    }

};

#endif // UNTAR_H
