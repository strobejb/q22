#include <QtTest/QtTest>

#include "parser.h"

class TypeLibTests : public QObject
{
	Q_OBJECT

private slots:
	void defaultParsersDoNotShareTypes();
	void sharedTypeLibraryPoolsTypesAcrossParsers();
	void includeParserUsesTheSameTypeLibrary();
	void fileRefsRemainValidForSharedLibraryAfterParserDies();
	void cleanupBelongsToTheTypeLibrary();
	void dynamicPlacementTagsParse();
	void viewTagsParse();
	void endianExpressionTagsParse();
	void lengthIsIsReserved();
	void unsizedArraysRequireSizeIs();
	void elfRootIsExportedAndAssociated();
	void standardTypelibFilesParse();
};

static bool parseBuffer(Parser &parser, const char *text)
{
	parser.Init(text, strlen(text));
	return parser.Parse() != 0;
}

void TypeLibTests::defaultParsersDoNotShareTypes()
{
	// Scenario: two callers create ordinary Parser instances in the same process.
	// Expected: a typedef parsed by the first parser is invisible to the second.
	// Regression guard: the old process-global identifier table leaked types
	// between unrelated parser uses.
	Parser first;
	QVERIFY(parseBuffer(first, "typedef dword LocalOnly;\n"));
	QCOMPARE(first.GetTypeLibrary()->globalTypeDeclList.size(), size_t(1));

	Parser second;
	QVERIFY(!parseBuffer(second, "LocalOnly value;\n"));
	QCOMPARE(second.GetTypeLibrary()->globalTypeDeclList.size(), size_t(0));
}

void TypeLibTests::sharedTypeLibraryPoolsTypesAcrossParsers()
{
	// Scenario: a caller deliberately parses multiple buffers into one library.
	// Expected: the second parser can use a typedef produced by the first parser.
	// Regression guard: moving globals into Parser must not remove the original
	// ability to pool type declarations across related parses.
	TypeLibrary library;

	Parser first(&library);
	QVERIFY(parseBuffer(first, "typedef dword SharedType;\n"));

	Parser second(&library);
	QVERIFY(parseBuffer(second, "SharedType value;\n"));

	QCOMPARE(library.globalTypeDeclList.size(), size_t(2));
	QCOMPARE(library.globalFileHistory.size(), size_t(2));
}

void TypeLibTests::includeParserUsesTheSameTypeLibrary()
{
	// Scenario: a source file includes another source file which defines a type.
	// Expected: the include-child parser contributes to the same TypeLibrary, and
	// the parent file can use the included typedef immediately afterwards.
	// Regression guard: include parsers must not get an isolated library or clean
	// up file buffers owned by the parent parse.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());

	QFile includeFile(dir.filePath("inc.tl"));
	QVERIFY(includeFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(includeFile.write("typedef dword IncludedType;\n") > 0);
	includeFile.close();

	QFile mainFile(dir.filePath("main.tl"));
	QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(mainFile.write("include \"inc.tl\";\nIncludedType value;\n") > 0);
	mainFile.close();

	TypeLibrary library;
	Parser parser(&library);
	QVERIFY(parser.Ooof(qPrintable(mainFile.fileName())));

	QCOMPARE(library.globalFileHistory.size(), size_t(2));
	QCOMPARE(library.globalTypeDeclList.size(), size_t(2));
}

void TypeLibTests::fileRefsRemainValidForSharedLibraryAfterParserDies()
{
	// Scenario: qexed keeps parsed declarations after the parser object is gone.
	// Expected: FILEREF still points into FILE_DESC storage owned by TypeLibrary,
	// so diagnostics and round-tripping can inspect the original source buffer.
	// Regression guard: FILE_DESC lifetime used to be implicit process-global
	// state; it must not become tied to a temporary Parser cursor.
	TypeLibrary library;

	{
		Parser parser(&library);
		QVERIFY(parseBuffer(parser, "typedef dword PersistedType;\n"));
	}

	QCOMPARE(library.globalTypeDeclList.size(), size_t(1));
	TypeDecl *decl = library.globalTypeDeclList[0];
	QVERIFY(decl->fileRef.fileDesc != nullptr);
	QVERIFY(decl->fileRef.fileDesc->buf != nullptr);
	QVERIFY(QByteArray(decl->fileRef.fileDesc->buf).contains("PersistedType"));
}

void TypeLibTests::cleanupBelongsToTheTypeLibrary()
{
	// Scenario: callers own a shared TypeLibrary and decide when to discard it.
	// Expected: cleanup empties declarations and file buffers once at the library
	// boundary; non-owning parsers do not delete shared state on destruction.
	// Regression guard: moving lexer cleanup out of Parser must not leave stale
	// FILE_DESC entries or double-delete include/parser state.
	TypeLibrary library;

	{
		Parser parser(&library);
		QVERIFY(parseBuffer(parser, "typedef dword CleanupType;\n"));
	}

	QVERIFY(!library.globalTypeDeclList.empty());
	QVERIFY(!library.globalFileHistory.empty());

	library.Cleanup();

	QVERIFY(library.globalTypeDeclList.empty());
	QVERIFY(library.globalFileHistory.empty());
}

void TypeLibTests::dynamicPlacementTagsParse()
{
	// Scenario: a structure definition uses the dynamic placement tags consumed
	// by Structure View, including repeated dynamic_struct entries.
	// Expected: TypeLib treats them as ordinary parsed tags with expression
	// payloads, so they can be round-tripped and interpreted by the renderer.
	// Regression guard: dynamic PE placement must live in definition files, not
	// in hard-coded C++ parser branches.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"enum Dir { ExportEntry = 0, ImportEntry = 1 };\n"
						"typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
						"typedef struct _Section { dword VirtualAddress; dword SizeOfRawData; dword PointerToRawData; } Section;\n"
						"typedef struct _SectionBucket { } SectionBucket;\n"
						"typedef struct _Import { dword value; } ImportDesc;\n"
						"[export]\n"
						"struct Root {\n"
						"  [dynamic_struct(ImportEntry, ImportDesc, VirtualAddress, Size != 0)] DataDir dirs[2];\n"
						"  [dynamic_container(SectionBucket), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[2];\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetTypeLibrary()->globalTypeDeclList)
	{
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
		{
			root = decl;
			break;
		}
	}

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(2));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_DYNAMICSTRUCT, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_DYNAMICCONTAINER, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_OFFSETMAP, nullptr));
}

void TypeLibTests::viewTagsParse()
{
	// Scenario: a TypeLib declaration opts into a C++ semantic interpreter with
	// a compact view("id") hook.
	// Expected: the parser preserves the string expression on the TypeDecl, so
	// Structure View can run optional interpreters after raw rendering.
	// Regression guard: semantic interpretation must remain declarative TypeLib
	// metadata, not a hard-coded type-name check in the renderer.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[view(\"pe.imports\")]\n"
						"typedef struct _Import { dword thunk; } ImportDesc;\n"));

	QCOMPARE(parser.GetTypeLibrary()->globalTypeDeclList.size(), size_t(1));
	ExprNode *expr = nullptr;
	QVERIFY(FindTag(parser.GetTypeLibrary()->globalTypeDeclList[0]->tagList, TOK_VIEW, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(expr->str), QStringLiteral("pe.imports"));
}

void TypeLibTests::endianExpressionTagsParse()
{
	// Scenario: a binary format declares its byte order inside an already
	// addressable field rather than as a fixed string in the definition file.
	// Expected: endian(expr) is preserved as an expression tag, letting Structure
	// View evaluate it against file data while rendering.
	// Regression guard: ELF needs endian(header.e_ident[EI_DATA] == ELFDATA2MSB)
	// instead of only the older endian("big") spelling.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"enum Data { Big = 2 };\n"
						"[export, endian(header[0] == Big)]\n"
						"struct Root { byte header[1]; word value; } root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetTypeLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	ExprNode *expr = nullptr;
	QVERIFY(FindTag(root->tagList, TOK_ENDIAN, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_BINARY);
}

void TypeLibTests::lengthIsIsReserved()
{
	// Scenario: TypeLib knows the IDL-flavoured length_is keyword, but qexed
	// does not implement its rendering semantics.
	// Expected: the parser rejects it with a deliberate reserved-keyword error
	// instead of accepting a tag that Structure View will ignore later.
	// Regression guard: unsupported IDL syntax should fail near the definition
	// author, not become a surprising empty array or layout bug in the UI.
	Parser parser;
	QVERIFY(!parseBuffer(parser,
						 "struct Root {\n"
						 "  [length_is(count)] byte data[];\n"
						 "  byte count;\n"
						 "} root;\n"));
	QCOMPARE(parser.LastErr(), ERROR_RESERVED_KEYWORD);
}

void TypeLibTests::unsizedArraysRequireSizeIs()
{
	// Scenario: a definition declares a flexible array member with [] but gives
	// no TypeLib count tag.
	// Expected: the parser rejects the declaration because Structure View cannot
	// safely infer how many elements to render.
	// Regression guard: empty arrays should not silently render as zero elements
	// or depend on later UI code to diagnose definition mistakes.
	Parser missingSize;
	QVERIFY(!parseBuffer(missingSize,
						 "struct Root {\n"
						 "  byte data[];\n"
						 "} root;\n"));
	QCOMPARE(missingSize.LastErr(), ERROR_UNSIZED_ARRAY_REQUIRES_SIZEIS);

	Parser withSize;
	QVERIFY(parseBuffer(withSize,
						"struct Root {\n"
						"  byte count;\n"
						"  [size_is(count)] byte data[];\n"
						"} root;\n"));
}

void TypeLibTests::elfRootIsExportedAndAssociated()
{
	// Scenario: qexed ships ELF as a real Structure View root definition.
	// Expected: elf.txt parses to an exported root with common ELF suffix
	// associations, so the UI can list and auto-select it like PE.
	// Regression guard: ELF support should not remain a stale standalone header
	// typedef that never appears in the Structure View picker.
	const QDir typeLibDir(QStringLiteral(TYPELIB_TEST_DATA_DIR));
	const QString path = typeLibDir.filePath(QStringLiteral("elf.txt"));
	Parser parser;
	QVERIFY2(parser.Ooof(qPrintable(path)), qPrintable(parser.LastErrStr()));

	TypeDecl *elf = nullptr;
	for(TypeDecl *decl : parser.GetTypeLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			elf = decl;

	QVERIFY(elf);
	QVERIFY(FindTag(elf->tagList, TOK_ENDIAN, nullptr));
	QVERIFY(FindTag(elf->tagList, TOK_VIEW, nullptr));

	ExprNode *assoc = nullptr;
	QVERIFY(FindTag(elf->tagList, TOK_ASSOC, &assoc));
	QVERIFY(assoc);
	auto containsAssoc = [](auto &&self, ExprNode *expr, const QString &needle) -> bool {
		if(!expr)
			return false;
		if(expr->type == EXPR_STRINGBUF && expr->str
			&& QString::fromLocal8Bit(expr->str) == needle)
		{
			return true;
		}
		return self(self, expr->left, needle) || self(self, expr->right, needle);
	};
	QVERIFY(containsAssoc(containsAssoc, assoc, QStringLiteral(".elf")));
	QVERIFY(containsAssoc(containsAssoc, assoc, QStringLiteral(".so")));
	QVERIFY(containsAssoc(containsAssoc, assoc, QStringLiteral(".o")));
}

void TypeLibTests::standardTypelibFilesParse()
{
	// Scenario: qexed ships real TypeLib definition files for users and tests.
	// Expected: every shipped example parses from the runtime data directory, and
	// relative includes such as elf.txt -> basetypes.txt resolve naturally.
	// Regression guard: the examples must not drift into stale, untested app data.
	const QDir typeLibDir(QStringLiteral(TYPELIB_TEST_DATA_DIR));
	QVERIFY2(typeLibDir.exists(), qPrintable(typeLibDir.absolutePath()));

	const QStringList files = {
		QStringLiteral("basetypes.txt"),
		QStringLiteral("elf.txt"),
		QStringLiteral("pe.txt"),
	};

	for(const QString &file : files)
	{
		const QString path = typeLibDir.filePath(file);
		QVERIFY2(QFileInfo::exists(path), qPrintable(path));

		Parser parser;
		QVERIFY2(parser.Ooof(qPrintable(path)), qPrintable(parser.LastErrStr()));
		QVERIFY2(!parser.GetTypeLibrary()->globalTypeDeclList.empty(), qPrintable(file));
	}
}

QTEST_MAIN(TypeLibTests)
#include "typelib_tests.moc"
