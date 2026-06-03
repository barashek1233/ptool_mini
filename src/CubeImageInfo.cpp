#include "CubeImageInfo.h"
#include <QDebug>

CubeImageInfo::CubeImageInfo(QString const& filename) : m_filename(filename)
{
    QFileInfo info(filename);
    if (info.suffix() == "bin" and filename.contains(".swu")) {
        _fromUbiBin();
    } else if (info.suffix() == "ubi") {
        _fromUbi();
    }
}

QString
CubeImageInfo::unit() const
{
    return m_unit;
}

QString
CubeImageInfo::cube() const
{
    return m_targetCube;
}

QString
CubeImageInfo::imageVersion() const
{
    return m_version;
}

int32_t
CubeImageInfo::buildNumber() const
{
    return m_buildNumber;
}

QString
CubeImageInfo::atochaRepoCommitHash() const
{
    return m_atochaRepoCommitHash;
}

QString
CubeImageInfo::metaAtochaRepoCommitHash() const
{
    return m_metaAtochaRepoCommitHash;
}

QString
CubeImageInfo::filename() const
{
    return m_filename;
}

QVector<CubeImageInfo>
CubeImageInfo::scanDirectoryForImages(QDir const& directory)
{
    QStringList files = directory.entryList(kImageFormats, QDir::Files);
    QVector<CubeImageInfo> result;
    for (QString const& file : qAsConst(files)) {
        result.push_back(CubeImageInfo::infoFromFilename(file));
    }
    return result;
}

void
CubeImageInfo::_fromUbi()
{
    // example image name
    // cube-image-cube-t-b-ips-golden-crown.ubifs.cube-image-cube-t-b-ips-golden-crown-20221125151027-1.0.6-rc30-.ubi
    // cube-parcel-machine-pos-image-cube-d-engy-parcel-machine-pos.ubifs.cube-parcel-machine-pos-image-cube-d-engy-
    // parcel-machine-pos-1.0.0-609+g6c5680b2-g651a88b.ubi
    int firstPointIndex = filename().indexOf('.');
    m_targetCube        = filename().left(firstPointIndex);
    int unitLastPos     = 0;
    for (unitLastPos = firstPointIndex + 1; unitLastPos < filename().length(); ++unitLastPos) {
        if (filename().at(unitLastPos) == '.') {
            break;
        }
    }
    m_unit = filename().mid(firstPointIndex + 1, unitLastPos - 1 - firstPointIndex);

    int plusPos = filename().indexOf("+");
    if (plusPos != -1) {
        int minPos = plusPos - 1;
        for (; minPos > 0; --minPos) {
            if (not filename().at(minPos).isDigit()) {
                break;
            }
        }
        int firstVerSymbol = minPos;
        for (; firstVerSymbol > 0; --firstVerSymbol) {
            if (filename().at(firstVerSymbol) == '-') {
                break;
            }
        }
        int index_underline = filename().indexOf('_');
        if (index_underline != -1) {
            m_version = filename().mid(index_underline + 1, minPos - 2 - index_underline);
        }
        m_buildNumber          = filename().midRef(minPos + 1, plusPos - 1 - minPos).toInt();
        m_atochaRepoCommitHash = filename().mid(plusPos + 2, filename().lastIndexOf('-') - plusPos - 2);
        if (not filename().midRef(plusPos, filename().lastIndexOf('-') - plusPos).contains("+g")) {
            m_atochaRepoCommitHash = kUnknown;
        }

        int lastMinPos             = filename().lastIndexOf('-');
        m_metaAtochaRepoCommitHash = filename().mid(lastMinPos + 2, filename().lastIndexOf(".") - 2 - lastMinPos);
        if (not filename().midRef(lastMinPos, filename().lastIndexOf('.') - lastMinPos).contains("-g")) {
            m_metaAtochaRepoCommitHash = kUnknown;
        }
    }
}

void
CubeImageInfo::_fromUbiBin()
{
    // example image name
    // rootfs.cube-golden-crown-image.cube-t-b-ips-golden-crown_1.0.4-rc06-1.0-r6.3.2.1-864+g503ed1c8-gb512ec0.swu.bin
    m_unit               = filename().mid(0, filename().indexOf('.'));
    int cubeTIndex       = filename().indexOf("cube-t", 0, Qt::CaseInsensitive);
    int cubeDIndex       = filename().indexOf("cube-d", 0, Qt::CaseInsensitive);
    int cubeRevLastIndex = filename().indexOf('_');

    if (cubeDIndex != -1) {
        m_targetCube = filename().mid(cubeDIndex, cubeRevLastIndex - cubeDIndex);
    } else if (cubeTIndex != -1) {
        m_targetCube = filename().mid(cubeTIndex, cubeRevLastIndex - cubeTIndex);
    } else {
        m_targetCube = kUnknown;
    }

    int versionWithBuildVersionIndex = filename().indexOf('+');
    int versionLastIndex             = -1;
    if (versionWithBuildVersionIndex != -1) {
        for (int index = versionWithBuildVersionIndex - 1; index >= 0; --index) {
            if (not filename().at(index).isDigit()) {
                versionLastIndex = index;
                break;
            }
        }

        m_version = filename().mid(cubeRevLastIndex + 1, filename().lastIndexOf("swu") - 2 - cubeRevLastIndex);
        bool ok{false};
        m_buildNumber =
            filename().midRef(versionLastIndex + 1, versionWithBuildVersionIndex - versionLastIndex - 1).toInt(&ok);
        if (not ok) {
            m_buildNumber = -1;
        }
        int lastSwipeIndex = filename().lastIndexOf('-');

        m_atochaRepoCommitHash =
            filename().mid(versionWithBuildVersionIndex + 2, lastSwipeIndex - versionWithBuildVersionIndex - 2);
        m_metaAtochaRepoCommitHash =
            filename().mid(lastSwipeIndex + 2, filename().lastIndexOf("swu") - lastSwipeIndex - 3);

        if (not filename()
                    .midRef(versionWithBuildVersionIndex, lastSwipeIndex - versionWithBuildVersionIndex)
                    .contains("+g")) {
            m_atochaRepoCommitHash = kUnknown;
        }

        if (not filename().midRef(lastSwipeIndex, filename().lastIndexOf("swu") - lastSwipeIndex).contains("-g")) {
            m_metaAtochaRepoCommitHash = kUnknown;
        }

    } else {
        m_version = kUnknown;
    }
}

CubeImageInfo
CubeImageInfo::infoFromFilename(QString const& filename)
{
    // https://gitlab.aqsi.ru/aqc_embedded/aqc_embedded_doc/-/blob/dev/updater/Именование_файлов_обновлений.adoc

    return CubeImageInfo{filename};
}
