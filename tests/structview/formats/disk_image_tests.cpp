#include "../structview_testsupport.h"

namespace {

qsizetype writeIsoDirectoryRecord(QByteArray *image,
                                   qsizetype offset,
                                   const QByteArray &identifier,
                                   quint32 extentSector,
                                   quint32 dataLength,
                                   quint8 flags)
{
    const qsizetype padding = (identifier.size() % 2) == 0 ? 1 : 0;
    const qsizetype length = 33 + identifier.size() + padding;
    (*image)[offset] = char(length);
    writeLe32(image, offset + 2, extentSector);
    writeBe32(image, offset + 6, extentSector);
    writeLe32(image, offset + 10, dataLength);
    writeBe32(image, offset + 14, dataLength);
    (*image)[offset + 25] = char(flags);
    writeLe16(image, offset + 28, 1);
    writeBe16(image, offset + 30, 1);
    (*image)[offset + 32] = char(identifier.size());
    image->replace(offset + 33, identifier.size(), identifier);
    return length;
}

void loadLazyChildren(StructureRow *row)
{
    if (!row || !row->lazyChildLoader)
        return;

    StructureLazyChildLoader loader = std::move(row->lazyChildLoader);
    row->lazyChildrenLoaded = true;
    auto children = loader();
    for (auto &child : children)
    {
        child->parent = row;
        row->children.push_back(std::move(child));
    }
}

} // namespace

class StructViewDiskImageTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersRawImgMbrPartitions();
    void builderRendersIsoVolumeDescriptors();
    void builderRendersIsoDirectoryTreesAcrossSectorPadding();
};

void StructViewDiskImageTests::builderRendersRawImgMbrPartitions()
{
    // Scenario: raw disk images commonly start with an MBR partition table.
    // Expected: the standard RAWIMG definition renders typed partition entries
    // and maps each non-empty partition payload to firstLba * 512.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("rawimg.strata")), "rawimg.strata failed to parse");
    TypeDecl *rawImgRoot = exportedNamed(&library, QStringLiteral("RAWIMG"));
    QVERIFY(rawImgRoot);

    QByteArray image(4096, '\0');
    writeLe32(&image, 440, 0x12345678);

    constexpr qsizetype firstPartition = 446;
    image[firstPartition + 0] = char(0x80);
    image[firstPartition + 1] = char(0x01);
    image[firstPartition + 2] = char(0x01);
    image[firstPartition + 4] = char(0x0c);
    image[firstPartition + 5] = char(0xfe);
    image[firstPartition + 6] = char(0xff);
    image[firstPartition + 7] = char(0xff);
    writeLe32(&image, firstPartition + 8, 1);
    writeLe32(&image, firstPartition + 12, 2);

    constexpr qsizetype secondPartition = firstPartition + 16;
    image[secondPartition + 4] = char(0x83);
    writeLe32(&image, secondPartition + 8, 3);
    writeLe32(&image, secondPartition + 12, 1);

    writeLe16(&image, 510, 0xaa55);
    image[512] = char(0xaa);
    image[513] = char(0xbb);
    image[1536] = char(0xcc);

    auto rows = buildRows(&library, rawImgRoot, image);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("RAWIMG"));

    StructureRow *mbr = findChildNamed(rows[0].get(), QStringLiteral("RAWIMG_MASTER_BOOT_RECORD mbr"));
    QVERIFY2(mbr, qPrintable(childNames(rows[0].get())));
    QCOMPARE(findChildNamed(mbr, QStringLiteral("dword diskSignature"))->value, QStringLiteral("305419896"));
    QCOMPARE(findChildNamed(mbr, QStringLiteral("word signature"))->value, QStringLiteral("RAWIMG_BOOT_SIGNATURE_MBR"));

    StructureRow *partitions = findChildNamed(mbr, QStringLiteral("RAWIMG_PARTITION_ENTRY partitions[]"));
    QVERIFY2(partitions, qPrintable(childNames(mbr)));
    QCOMPARE(partitions->children.size(), size_t(4));
    QCOMPARE(partitions->children[0]->name, QStringLiteral("[0]RAWIMG_PARTITION_FAT32_LBA"));
    QCOMPARE(partitions->children[1]->name, QStringLiteral("[1]RAWIMG_PARTITION_LINUX"));
    QCOMPARE(partitions->children[2]->name, QStringLiteral("[2]RAWIMG_PARTITION_EMPTY"));

    StructureRow *first = partitions->children[0].get();
    QCOMPARE(findChildNamed(first, QStringLiteral("byte bootIndicator"))->value,
             QStringLiteral("RAWIMG_PARTITION_ACTIVE"));
    QCOMPARE(findChildNamed(first, QStringLiteral("byte partitionType"))->value,
             QStringLiteral("RAWIMG_PARTITION_FAT32_LBA"));
    QCOMPARE(findChildNamed(first, QStringLiteral("dword firstLba"))->value, QStringLiteral("1"));
    QCOMPARE(findChildNamed(first, QStringLiteral("dword sectorCount"))->value, QStringLiteral("2"));

    StructureRow *firstSectorCount = findChildNamed(first, QStringLiteral("dword sectorCount"));
    QVERIFY2(firstSectorCount, qPrintable(childNames(first)));
    StructureRow *firstData = findChildNamed(firstSectorCount, QStringLiteral("BYTE PartitionData[]"));
    QVERIFY2(firstData, qPrintable(childNames(firstSectorCount)));
    QCOMPARE(firstData->offset, QStringLiteral("00000200"));
    QCOMPARE(firstData->children[0]->value, QStringLiteral("170"));
    QCOMPARE(firstData->children[1]->value, QStringLiteral("187"));

    StructureRow *secondSectorCount = findChildNamed(partitions->children[1].get(), QStringLiteral("dword sectorCount"));
    QVERIFY2(secondSectorCount, qPrintable(childNames(partitions->children[1].get())));
    StructureRow *secondData = findChildNamed(secondSectorCount, QStringLiteral("BYTE PartitionData[]"));
    QVERIFY2(secondData, qPrintable(childNames(secondSectorCount)));
    QCOMPARE(secondData->offset, QStringLiteral("00000600"));

    StructureRow *emptySectorCount = findChildNamed(partitions->children[2].get(), QStringLiteral("dword sectorCount"));
    QVERIFY2(emptySectorCount, qPrintable(childNames(partitions->children[2].get())));
    QVERIFY(!findChildNamed(emptySectorCount, QStringLiteral("BYTE PartitionData[]")));
}

void StructViewDiskImageTests::builderRendersIsoVolumeDescriptors()
{
    // Scenario: ISO 9660 images anchor volume descriptors at sector 16 and the
    // primary descriptor points at path-table and root-directory extents.
    // Expected: the standard ISO definition renders the PVD, stops before the
    // terminator descriptor, and exposes the pointed-to byte ranges as dynamic rows.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("iso.strata")), "iso.strata failed to parse");
    TypeDecl *isoRoot = exportedNamed(&library, QStringLiteral("ISO_IMAGE"));
    QVERIFY(isoRoot);

    constexpr qsizetype sectorSize = 2048;
    constexpr qsizetype pvd = 16 * sectorSize;
    constexpr qsizetype terminator = 17 * sectorSize;
    constexpr qsizetype rootDirectorySector = 18;
    constexpr qsizetype pathTableSector = 19;
    QByteArray iso(20 * sectorSize, '\0');

    iso[pvd + 0] = char(1);
    writeAscii(&iso, pvd + 1, "CD001");
    iso[pvd + 6] = char(1);
    writeAscii(&iso, pvd + 8, "LINUX");
    writeAscii(&iso, pvd + 40, "TEST_ISO");
    writeLe32(&iso, pvd + 80, 20);
    writeBe32(&iso, pvd + 84, 20);
    writeLe16(&iso, pvd + 120, 1);
    writeBe16(&iso, pvd + 122, 1);
    writeLe16(&iso, pvd + 124, 1);
    writeBe16(&iso, pvd + 126, 1);
    writeLe16(&iso, pvd + 128, sectorSize);
    writeBe16(&iso, pvd + 130, sectorSize);
    writeLe32(&iso, pvd + 132, 10);
    writeBe32(&iso, pvd + 136, 10);
    writeLe32(&iso, pvd + 140, pathTableSector);
    writeBe32(&iso, pvd + 148, pathTableSector);

    constexpr qsizetype rootRecord = pvd + 156;
    iso[rootRecord + 0] = char(34);
    writeLe32(&iso, rootRecord + 2, rootDirectorySector);
    writeBe32(&iso, rootRecord + 6, rootDirectorySector);
    writeLe32(&iso, rootRecord + 10, sectorSize);
    writeBe32(&iso, rootRecord + 14, sectorSize);
    iso[rootRecord + 25] = char(0x02);
    writeLe16(&iso, rootRecord + 28, 1);
    writeBe16(&iso, rootRecord + 30, 1);
    iso[rootRecord + 32] = char(1);
    iso[rootRecord + 33] = char(0);
    iso[rootDirectorySector * sectorSize] = char(34);
    iso[rootDirectorySector * sectorSize + 25] = char(0x02);

    writeAscii(&iso, pathTableSector * sectorSize, "\x01");
    iso[pathTableSector * sectorSize + 1] = char(0);
    writeLe32(&iso, pathTableSector * sectorSize + 2, rootDirectorySector);
    writeLe16(&iso, pathTableSector * sectorSize + 6, 1);
    iso[pathTableSector * sectorSize + 8] = char(0);

    iso[terminator + 0] = char(255);
    writeAscii(&iso, terminator + 1, "CD001");
    iso[terminator + 6] = char(1);

    auto rows = buildRows(&library, isoRoot, iso);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("ISO_IMAGE"));

    StructureRow *descriptors = findChildNamed(rows[0].get(), QStringLiteral("ISO_VOLUME_DESCRIPTOR volumeDescriptors[]"));
    QVERIFY2(descriptors, qPrintable(childNames(rows[0].get())));
    QCOMPARE(descriptors->children.size(), size_t(2));
    QCOMPARE(descriptors->children[0]->name, QStringLiteral("[0]ISO_VOLUME_PRIMARY"));
    QCOMPARE(descriptors->children[1]->name, QStringLiteral("[1]ISO_VOLUME_TERMINATOR"));

    StructureRow *primary = findChildNamed(descriptors->children[0].get(),
                                           QStringLiteral("ISO_PRIMARY_VOLUME_DESCRIPTOR_PAYLOAD primary"));
    QVERIFY2(primary, qPrintable(childNames(descriptors->children[0].get())));
    QCOMPARE(findChildNamed(primary, QStringLiteral("char systemIdentifier[]"))->value, QStringLiteral("\"LINUX\""));
    QCOMPARE(findChildNamed(primary, QStringLiteral("char volumeIdentifier[]"))->value, QStringLiteral("\"TEST_ISO\""));

    StructureRow *typeLPathTableLocation = findChildNamed(primary, QStringLiteral("dword typeLPathTableLocation"));
    QVERIFY2(typeLPathTableLocation, qPrintable(childNames(primary)));
    StructureRow *pathTable = findChildNamed(typeLPathTableLocation, QStringLiteral("BYTE TypeLPathTable[]"));
    QVERIFY2(pathTable, qPrintable(childNames(typeLPathTableLocation)));
    QCOMPARE(pathTable->offset, QStringLiteral("00009800"));

    StructureRow *rootDirectoryRecord = findChildNamed(primary, QStringLiteral("ISO_DIRECTORY_RECORD rootDirectoryRecord"));
    QVERIFY2(rootDirectoryRecord, qPrintable(childNames(primary)));
    StructureRow *fileFlags = findChildNamed(rootDirectoryRecord, QStringLiteral("byte fileFlags"));
    QVERIFY2(fileFlags, qPrintable(childNames(rootDirectoryRecord)));
    QCOMPARE(fileFlags->value, QStringLiteral("ISO_FILE_FLAG_DIRECTORY"));
    StructureRow *directoryEntries = findChildNamed(rootDirectoryRecord,
                                                     QStringLiteral("ISO_DIRECTORY_ITEM DirectoryEntries[]"));
    QVERIFY2(directoryEntries, qPrintable(childNames(rootDirectoryRecord)));
    QCOMPARE(directoryEntries->offset, QStringLiteral("00009000"));
    QVERIFY(directoryEntries->lazyChildLoader);
    QVERIFY(directoryEntries->children.empty());

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("ISO Summary"));
    QVERIFY2(summary, "ISO Summary semantic child row not found");
    StructureRow *volumes = findChildNamed(summary, QStringLiteral("Volumes"));
    QVERIFY2(volumes, qPrintable(childNames(summary)));
    StructureRow *volume = findChildNamed(volumes, QStringLiteral("TEST_ISO"));
    QVERIFY2(volume, qPrintable(childNames(volumes)));
    QCOMPARE(findChildNamed(volume, QStringLiteral("SystemIdentifier"))->value, QStringLiteral("LINUX"));
    QCOMPARE(findChildNamed(volume, QStringLiteral("VolumeIdentifier"))->value, QStringLiteral("TEST_ISO"));
    QCOMPARE(findChildNamed(volume, QStringLiteral("LogicalBlockSize"))->value, QStringLiteral("2048"));
    QCOMPARE(findChildNamed(volume, QStringLiteral("VolumeSpaceSize"))->value, QStringLiteral("20"));
    QCOMPARE(findChildNamed(volume, QStringLiteral("RootExtent"))->value, QStringLiteral("18"));
    QCOMPARE(findChildNamed(volume, QStringLiteral("RootSize"))->value, QStringLiteral("2048"));
}

void StructViewDiskImageTests::builderRendersIsoDirectoryTreesAcrossSectorPadding()
{
    // Scenario: ISO directory extents are byte-bounded variable-record streams.
    // A zero record length pads to the next 2048-byte sector, after which more
    // records may follow; directory records recursively point at another such
    // stream. Expected: parsing resumes in sector two, a subdirectory expands
    // lazily, and its dot entries do not create recursive cycles.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("iso.strata")), "iso.strata failed to parse");
    TypeDecl *isoRoot = exportedNamed(&library, QStringLiteral("ISO_IMAGE"));
    QVERIFY(isoRoot);

    constexpr qsizetype sectorSize = 2048;
    constexpr qsizetype pvd = 16 * sectorSize;
    constexpr qsizetype terminator = 17 * sectorSize;
    constexpr quint32 rootSector = 18;
    constexpr quint32 secondRootSector = 19;
    constexpr quint32 childSector = 21;
    QByteArray iso(24 * sectorSize, '\0');

    iso[pvd] = char(1);
    writeAscii(&iso, pvd + 1, "CD001");
    iso[pvd + 6] = char(1);
    writeLe16(&iso, pvd + 128, sectorSize);
    writeBe16(&iso, pvd + 130, sectorSize);
    writeIsoDirectoryRecord(&iso,
                            pvd + 156,
                            QByteArray(1, '\0'),
                            rootSector,
                            2 * sectorSize,
                            0x02);

    iso[terminator] = char(255);
    writeAscii(&iso, terminator + 1, "CD001");
    iso[terminator + 6] = char(1);

    qsizetype rootOffset = rootSector * sectorSize;
    rootOffset += writeIsoDirectoryRecord(&iso,
                                          rootOffset,
                                          QByteArray(1, '\0'),
                                          rootSector,
                                          2 * sectorSize,
                                          0x02);
    rootOffset += writeIsoDirectoryRecord(&iso,
                                          rootOffset,
                                          QByteArrayLiteral("ROOT.TXT;1"),
                                          22,
                                          1,
                                          0);
    QCOMPARE(rootOffset < secondRootSector * sectorSize, true);

    writeIsoDirectoryRecord(&iso,
                            secondRootSector * sectorSize,
                            QByteArrayLiteral("BOOT"),
                            childSector,
                            sectorSize,
                            0x02);

    qsizetype childOffset = childSector * sectorSize;
    childOffset += writeIsoDirectoryRecord(&iso,
                                           childOffset,
                                           QByteArray(1, '\0'),
                                           childSector,
                                           sectorSize,
                                           0x02);
    childOffset += writeIsoDirectoryRecord(&iso,
                                           childOffset,
                                           QByteArray(1, '\1'),
                                           rootSector,
                                           2 * sectorSize,
                                           0x02);
    writeIsoDirectoryRecord(&iso,
                            childOffset,
                            QByteArrayLiteral("NEST.BIN;1"),
                            23,
                            1,
                            0);

    auto rows = buildRows(&library, isoRoot, iso);
    QCOMPARE(rows.size(), size_t(1));
    StructureRow *rootRecord = findDescendantNamed(rows[0].get(),
                                                    QStringLiteral("ISO_DIRECTORY_RECORD rootDirectoryRecord"));
    QVERIFY(rootRecord);
    StructureRow *rootEntries = findChildNamed(rootRecord,
                                               QStringLiteral("ISO_DIRECTORY_ITEM DirectoryEntries[]"));
    QVERIFY2(rootEntries, qPrintable(childNames(rootRecord)));
    QVERIFY(rootEntries->lazyChildLoader);
    QVERIFY(rootEntries->children.empty());
    loadLazyChildren(rootEntries);
    QCOMPARE(rootEntries->byteLength, uint64_t(2 * sectorSize));
    QCOMPARE(rootEntries->children.size(), size_t(5));
    QCOMPARE(rootEntries->children[0]->absoluteOffset, uint64_t(rootSector * sectorSize));
    QCOMPARE(rootEntries->children[2]->absoluteOffset, uint64_t(rootOffset));
    QCOMPARE(rootEntries->children[2]->byteLength,
             uint64_t(secondRootSector * sectorSize - rootOffset));
    QCOMPARE(rootEntries->children[3]->absoluteOffset, uint64_t(secondRootSector * sectorSize));
    QVERIFY2(rootEntries->children[1]->name.contains(QStringLiteral("ROOT.TXT;1")),
             qPrintable(rootEntries->children[1]->name));
    QVERIFY2(rootEntries->children[3]->name.contains(QStringLiteral("BOOT")),
             qPrintable(rootEntries->children[3]->name));
    QVERIFY(!rootEntries->children[1]->hasOpenAsTarget);
    QVERIFY(rootEntries->children[1]->lazyChildLoader);
    loadLazyChildren(rootEntries->children[1].get());
    StructureRow *rootFileData = findChildNamed(rootEntries->children[1].get(), QStringLiteral("BYTE FileData[]"));
    QVERIFY2(rootFileData, qPrintable(childNames(rootEntries->children[1].get())));
    QVERIFY(rootFileData->hasOpenAsTarget);
    QVERIFY(!rootFileData->branchIconPath.isEmpty());
    QVERIFY(!rootEntries->children[3]->hasOpenAsTarget);
    QVERIFY(rootEntries->children[3]->branchIconPath.isEmpty());

    StructureRow *subdirectoryRecord = findDescendantNamed(rootEntries->children[3].get(),
                                                            QStringLiteral("ISO_DIRECTORY_RECORD record"));
    QVERIFY2(subdirectoryRecord, qPrintable(childNames(rootEntries->children[3].get())));
    StructureRow *subdirectoryFlags = findChildNamed(subdirectoryRecord, QStringLiteral("byte fileFlags"));
    QVERIFY2(subdirectoryFlags, qPrintable(childNames(subdirectoryRecord)));
    QVERIFY(rootEntries->children[3]->lazyChildLoader);
    loadLazyChildren(rootEntries->children[3].get());

    StructureRow *childEntries = findChildNamed(rootEntries->children[3].get(),
                                                QStringLiteral("ISO_DIRECTORY_ITEM DirectoryEntries[]"));
    QVERIFY2(childEntries, qPrintable(childNames(rootEntries->children[3].get())));
    QVERIFY(childEntries->lazyChildLoader);
    QVERIFY(childEntries->children.empty());
    loadLazyChildren(childEntries);
    QCOMPARE(childEntries->byteLength, uint64_t(sectorSize));
    QCOMPARE(childEntries->children.size(), size_t(4));

    for (size_t dotIndex = 0; dotIndex < 2; ++dotIndex)
    {
        StructureRow *dotRecord = findDescendantNamed(childEntries->children[dotIndex].get(),
                                                       QStringLiteral("ISO_DIRECTORY_RECORD record"));
        QVERIFY(dotRecord);
        StructureRow *dotFlags = findChildNamed(dotRecord, QStringLiteral("byte fileFlags"));
        QVERIFY(dotFlags);
        QVERIFY(!childEntries->children[dotIndex]->lazyChildLoader);
    }

    StructureRow *nestedFile = findDescendantNamed(childEntries->children[2].get(),
                                                    QStringLiteral("ISO_DIRECTORY_RECORD record"));
    QVERIFY(nestedFile);
    StructureRow *nestedIdentifier = findChildNamed(nestedFile, QStringLiteral("byte fileIdentifier[]"));
    QVERIFY2(nestedIdentifier, qPrintable(childNames(nestedFile)));
    QCOMPARE(nestedIdentifier->value, QStringLiteral("\"NEST.BIN;1\""));
    QVERIFY2(childEntries->children[2]->name.contains(QStringLiteral("NEST.BIN;1")),
             qPrintable(childEntries->children[2]->name));
    QVERIFY(!childEntries->children[2]->hasOpenAsTarget);
    QVERIFY(childEntries->children[2]->lazyChildLoader);
    loadLazyChildren(childEntries->children[2].get());
    StructureRow *nestedFileData = findChildNamed(childEntries->children[2].get(), QStringLiteral("BYTE FileData[]"));
    QVERIFY2(nestedFileData, qPrintable(childNames(childEntries->children[2].get())));
    QVERIFY(nestedFileData->hasOpenAsTarget);
    QVERIFY(!nestedFileData->branchIconPath.isEmpty());
    QVERIFY(!childEntries->children[0]->hasOpenAsTarget);
    QVERIFY(childEntries->children[0]->branchIconPath.isEmpty());
    QVERIFY(!childEntries->children[1]->hasOpenAsTarget);
    QVERIFY(childEntries->children[1]->branchIconPath.isEmpty());
    QCOMPARE(nestedFileData->openAsOffset, uint64_t(23 * sectorSize));
    QCOMPARE(nestedFileData->openAsByteLength, uint64_t(1));

    StructureRow *summary = findSemanticRootChildNamed(rows, QStringLiteral("ISO Summary"));
    QVERIFY2(summary, "ISO Summary semantic child row not found");
    QVERIFY2(findChildNamed(summary, QStringLiteral("Volumes")), qPrintable(childNames(summary)));
}

REGISTER_STRUCTVIEW_TEST(StructViewDiskImageTests)
#include "disk_image_tests.moc"
