#ifndef TESTS_STRUCTVIEW_TESTSUPPORT_H
#define TESTS_STRUCTVIEW_TESTSUPPORT_H

#include "structview/structuredefinitionmanager.h"
#include "structview/structurerenderengine.h"
#include "structview/structuresemanticview.h"
#include "structview/structuretreemodel.h"
#include "structview/structurevaluebuilder.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

struct StructViewTestRegistration
{
    const char *name = nullptr;
    std::function<std::unique_ptr<QObject>()> create;
};

inline std::vector<StructViewTestRegistration> &structViewTestRegistry()
{
    static std::vector<StructViewTestRegistration> tests;
    return tests;
}

class StructViewTestRegistrar
{
public:
    StructViewTestRegistrar(const char *name, std::function<std::unique_ptr<QObject>()> create)
    {
        structViewTestRegistry().push_back({ name, std::move(create) });
    }
};

inline int runStructViewRegisteredTests(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int status = 0;
    for (const StructViewTestRegistration &registration : structViewTestRegistry()) {
        std::unique_ptr<QObject> test = registration.create();
        status |= QTest::qExec(test.get(), argc, argv);
    }
    return status;
}

#define STRUCTVIEW_TEST_CONCAT_IMPL(a, b) a##b
#define STRUCTVIEW_TEST_CONCAT(a, b) STRUCTVIEW_TEST_CONCAT_IMPL(a, b)
#define REGISTER_STRUCTVIEW_TEST(TestClass) \
    namespace { \
    const StructViewTestRegistrar STRUCTVIEW_TEST_CONCAT(g_registerStructViewTest_, __LINE__)( \
        #TestClass, []() { return std::make_unique<TestClass>(); }); \
    }

class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name)
        , m_hadValue(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_hadValue)
            qputenv(m_name, m_previous);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name = nullptr;
    bool m_hadValue = false;
    QByteArray m_previous;
};

inline void writeTextFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(text), qint64(text.size()));
}

inline bool parseBuffer(Parser &parser, const char *text)
{
    parser.Init(text, strlen(text));
    return parser.Parse() != 0;
}

inline TypeDecl *firstExported(StrataLibrary *library)
{
    if (!library)
        return nullptr;

    for (TypeDecl *decl : library->globalTypeDeclList)
        if (decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
            return decl;

    return nullptr;
}

inline QString exportedName(TypeDecl *decl)
{
    if (!decl)
        return {};

    for (Type *type : decl->declList)
        if (type && type->sym)
            return QString::fromLocal8Bit(type->sym->name);

    if (decl->baseType && decl->baseType->ty == typeSTRUCT && decl->baseType->sptr && decl->baseType->sptr->symbol)
        return QString::fromLocal8Bit(decl->baseType->sptr->symbol->name);

    return {};
}

inline TypeDecl *exportedNamed(StrataLibrary *library, const QString &name)
{
    if (!library)
        return nullptr;

    for (TypeDecl *decl : library->globalTypeDeclList)
        if (decl && FindTag(decl->tagList, TOK_EXPORT, nullptr) && exportedName(decl) == name)
            return decl;

    return nullptr;
}

inline TypeDecl *typeNamed(StrataLibrary *library, const QString &name)
{
    if (!library)
        return nullptr;

    const QByteArray nameBytes = name.toLocal8Bit();
    for (TypeDecl *decl : library->globalTypeDeclList)
    {
        if (!decl)
            continue;

        for (Type *type : decl->declList)
        {
            if ((type->ty == typeTYPEDEF || type->ty == typeIDENTIFIER) && type->sym
                && nameBytes == type->sym->name)
            {
                return decl;
            }
        }

        Type *base = BaseNode(decl->baseType);
        if (base && (base->ty == typeSTRUCT || base->ty == typeUNION) && base->sptr
            && base->sptr->symbol && nameBytes == base->sptr->symbol->name)
        {
            return decl;
        }
    }

    return nullptr;
}

inline std::vector<std::unique_ptr<StructureRow>> buildRows(StrataLibrary *library,
                                                            TypeDecl *root,
                                                            const QByteArray &bytes,
                                                            uint64_t baseOffset = 0,
                                                            const StructureDisplayOptions &options = StructureDisplayOptions())
{
    StructureValueBuilder builder;
    return builder.build(library,
                         root,
                         baseOffset,
                         [&bytes](uint64_t offset, uint8_t *buffer, size_t length) -> size_t {
                             if (offset >= static_cast<uint64_t>(bytes.size()))
                                 return 0;

                             const size_t available = static_cast<size_t>(bytes.size() - static_cast<int>(offset));
                             const size_t copied = qMin(length, available);
                             memcpy(buffer, bytes.constData() + offset, copied);
                             return copied;
                         },
                         options);
}

inline bool parseStandardElfDefinition(StrataLibrary *library)
{
    if (!library)
        return false;

    Parser parser(library);
    const QString path = QDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR)).filePath(QStringLiteral("elf.strata"));
    return parser.Ooof(qPrintable(path));
}

inline bool parseStandardDefinition(StrataLibrary *library, const QString &fileName)
{
    if (!library)
        return false;

    Parser parser(library);
    const QString path = QDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR)).filePath(fileName);
    return parser.Ooof(qPrintable(path));
}

inline StructureRow *findChildNamed(StructureRow *parent, const QString &name)
{
    if (!parent)
        return nullptr;

    for (const auto &child : parent->children)
        if (child->name == name)
            return child.get();

    return nullptr;
}

inline StructureRow *findTopLevelNamed(const std::vector<std::unique_ptr<StructureRow>> &rows, const QString &name)
{
    for (const auto &row : rows)
    {
        if (row && row->name == name)
            return row.get();
    }

    return nullptr;
}

inline StructureRow *findDescendantNamed(StructureRow *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->name == name)
        return parent;

    for (const auto &child : parent->children)
        if (StructureRow *found = findDescendantNamed(child.get(), name))
            return found;

    return nullptr;
}

inline QString childNames(StructureRow *parent)
{
    if (!parent)
        return QStringLiteral("<null>");

    QStringList names;
    for (const auto &child : parent->children)
        names.push_back(child->name);
    return names.join(QStringLiteral(", "));
}

inline void verifyBranchIconsPresent(const StructureRow *row)
{
    QVERIFY(row != nullptr);
    QVERIFY(!row->branchIconPath.isEmpty());
    QVERIFY(!row->branchOpenIconPath.isEmpty());
    QVERIFY(!row->branchEmptyIconPath.isEmpty());
}

inline void writeLe32(QByteArray *bytes, qsizetype offset, quint32 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 4 <= bytes->size());
    (*bytes)[offset + 0] = char(value & 0xff);
    (*bytes)[offset + 1] = char((value >> 8) & 0xff);
    (*bytes)[offset + 2] = char((value >> 16) & 0xff);
    (*bytes)[offset + 3] = char((value >> 24) & 0xff);
}

inline void writeLe16(QByteArray *bytes, qsizetype offset, quint16 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 2 <= bytes->size());
    (*bytes)[offset + 0] = char(value & 0xff);
    (*bytes)[offset + 1] = char((value >> 8) & 0xff);
}

inline void writeLe64(QByteArray *bytes, qsizetype offset, quint64 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 8 <= bytes->size());
    for (int i = 0; i < 8; ++i)
        (*bytes)[offset + i] = char((value >> (i * 8)) & 0xff);
}

inline void writeBe16(QByteArray *bytes, qsizetype offset, quint16 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 2 <= bytes->size());
    (*bytes)[offset + 0] = char((value >> 8) & 0xff);
    (*bytes)[offset + 1] = char(value & 0xff);
}

inline void writeBe32(QByteArray *bytes, qsizetype offset, quint32 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 4 <= bytes->size());
    (*bytes)[offset + 0] = char((value >> 24) & 0xff);
    (*bytes)[offset + 1] = char((value >> 16) & 0xff);
    (*bytes)[offset + 2] = char((value >> 8) & 0xff);
    (*bytes)[offset + 3] = char(value & 0xff);
}

inline void writeBe64(QByteArray *bytes, qsizetype offset, quint64 value)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(offset >= 0);
    QVERIFY(offset + 8 <= bytes->size());
    for (int i = 0; i < 8; ++i)
        (*bytes)[offset + i] = char((value >> ((7 - i) * 8)) & 0xff);
}

inline void writeAscii(QByteArray *bytes, qsizetype offset, const char *text)
{
    QVERIFY(bytes != nullptr);
    QVERIFY(text != nullptr);
    const qsizetype length = qsizetype(strlen(text)) + 1;
    QVERIFY(offset >= 0);
    QVERIFY(offset + length <= bytes->size());
    memcpy(bytes->data() + offset, text, size_t(length));
}

#endif // TESTS_STRUCTVIEW_TESTSUPPORT_H
