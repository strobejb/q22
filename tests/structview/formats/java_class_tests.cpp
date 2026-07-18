#include "../structview_testsupport.h"

class StructViewJavaClassTests : public QObject
{
    Q_OBJECT

private slots:
    void builderRendersJavaClassConstantPool();
};

void StructViewJavaClassTests::builderRendersJavaClassConstantPool()
{
    // Scenario: Java class files are big-endian and start with a constant pool
    // whose entries are tagged variants.
    // Expected: the standard Java definition renders the class header and
    // selects a UTF-8 constant pool payload by tag.
    StrataLibrary library;
    QVERIFY2(parseStandardDefinition(&library, QStringLiteral("java.strata")), "java.strata failed to parse");
    TypeDecl *javaRoot = exportedNamed(&library, QStringLiteral("JAVA_CLASS"));
    QVERIFY(javaRoot);

    QByteArray klass;
    const auto appendBe16 = [&klass](quint16 value) {
        klass.append(char((value >> 8) & 0xff));
        klass.append(char(value & 0xff));
    };
    const auto appendBe32 = [&klass](quint32 value) {
        klass.append(char((value >> 24) & 0xff));
        klass.append(char((value >> 16) & 0xff));
        klass.append(char((value >> 8) & 0xff));
        klass.append(char(value & 0xff));
    };
    const auto appendBe64 = [&klass, &appendBe32](quint64 value) {
        appendBe32(quint32((value >> 32) & 0xffffffff));
        appendBe32(quint32(value & 0xffffffff));
    };

    appendBe32(0xcafebabe);
    appendBe16(0);  // minor
    appendBe16(52); // Java 8 major
    appendBe16(4);  // long entry, its reserved slot, then one UTF-8 entry
    klass.append(char(5));
    appendBe64(0x0102030405060708);
    klass.append(char(1));
    appendBe16(4);
    klass.append("Test", 4);
    appendBe16(0x0021); // public | super
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);
    appendBe16(0);

    auto rows = buildRows(&library, javaRoot, klass);
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows[0]->name, QStringLiteral("JAVA_CLASS"));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("dword magic"))->value,
             QStringLiteral("JAVA_MAGIC_CLASS"));
    QCOMPARE(findChildNamed(rows[0].get(), QStringLiteral("word majorVersion"))->value,
             QStringLiteral("52"));

    StructureRow *constantPool = findChildNamed(rows[0].get(), QStringLiteral("JAVA_CP_INFO constantPool[]"));
    QVERIFY2(constantPool, qPrintable(childNames(rows[0].get())));
    QCOMPARE(constantPool->children.size(), size_t(2));
    QCOMPARE(constantPool->children[0]->name, QStringLiteral("[0]JAVA_CONSTANT_Long"));
    QCOMPARE(constantPool->children[1]->name, QStringLiteral("[2]JAVA_CONSTANT_Utf8"));

    StructureRow *utf8 = findDescendantNamed(constantPool->children[1].get(), QStringLiteral("JAVA_CONSTANT_Utf8_INFO utf8"));
    QVERIFY2(utf8, qPrintable(childNames(constantPool->children[1].get())));
    StructureRow *bytes = findChildNamed(utf8, QStringLiteral("byte bytes[]"));
    QVERIFY2(bytes, qPrintable(childNames(utf8)));
    QCOMPARE(bytes->value, QStringLiteral("\"Test\""));

    StructureRow *accessFlags = findChildNamed(rows[0].get(), QStringLiteral("word accessFlags"));
    QVERIFY2(accessFlags, qPrintable(childNames(rows[0].get())));
    QCOMPARE(accessFlags->value, QStringLiteral("JAVA_ACC_PUBLIC | JAVA_ACC_SUPER"));

    QByteArray largeKlass;
    const auto appendLargeBe16 = [&largeKlass](quint16 value) {
        largeKlass.append(char((value >> 8) & 0xff));
        largeKlass.append(char(value & 0xff));
    };
    const auto appendLargeBe32 = [&largeKlass](quint32 value) {
        largeKlass.append(char((value >> 24) & 0xff));
        largeKlass.append(char((value >> 16) & 0xff));
        largeKlass.append(char((value >> 8) & 0xff));
        largeKlass.append(char(value & 0xff));
    };

    appendLargeBe32(0xcafebabe);
    appendLargeBe16(0);
    appendLargeBe16(52);
    appendLargeBe16(102); // 101 physical UTF-8 constants, beyond the display cap.
    for (int i = 0; i < 101; ++i)
    {
        largeKlass.append(char(1));
        appendLargeBe16(1);
        largeKlass.append(char('A' + (i % 26)));
    }
    appendLargeBe16(0x0021);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);
    appendLargeBe16(0);

    rows = buildRows(&library, javaRoot, largeKlass);
    QCOMPARE(rows.size(), size_t(1));
    constantPool = findChildNamed(rows[0].get(), QStringLiteral("JAVA_CP_INFO constantPool[]"));
    QVERIFY2(constantPool, qPrintable(childNames(rows[0].get())));
    QCOMPARE(constantPool->children.size(), size_t(100));
    QCOMPARE(constantPool->children.back()->name, QStringLiteral("[99]JAVA_CONSTANT_Utf8"));
    accessFlags = findChildNamed(rows[0].get(), QStringLiteral("word accessFlags"));
    QVERIFY2(accessFlags, qPrintable(childNames(rows[0].get())));
    QCOMPARE(accessFlags->absoluteOffset, uint64_t(414));
    QCOMPARE(accessFlags->value, QStringLiteral("JAVA_ACC_PUBLIC | JAVA_ACC_SUPER"));
}

REGISTER_STRUCTVIEW_TEST(StructViewJavaClassTests)
#include "java_class_tests.moc"
