#pragma once

#include <QString>
#include <QVector>
#include <QDir>

static QStringList const kImageFormats = {"*.ubi", "*.ubi.bin"};
static QString const kUnknown{"Unknown"};

class CubeImageInfo
{
private:
    CubeImageInfo(QString const& filename);

public:
    QString unit() const;
    QString cube() const;
    QString imageVersion() const;
    int32_t buildNumber() const;
    QString atochaRepoCommitHash() const;
    QString metaAtochaRepoCommitHash() const;
    QString filename() const;

    static CubeImageInfo infoFromFilename(QString const& filename);
    [[nodiscard]] static QVector<CubeImageInfo> scanDirectoryForImages(QDir const& directory);

    CubeImageInfo() = default;

private:
    QString m_filename;
    QString m_unit{kUnknown};
    QString m_targetCube{kUnknown};
    QString m_version{kUnknown};
    QString m_atochaRepoCommitHash{kUnknown};
    QString m_metaAtochaRepoCommitHash{kUnknown};
    int32_t m_buildNumber{-1};

private:
    void _fromUbi();
    void _fromUbiBin();
};
