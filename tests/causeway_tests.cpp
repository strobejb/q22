#include <QtTest/QtTest>

#include "parser.h"

#include <cstdio>
#include <vector>

class CausewayTests : public QObject
{
	Q_OBJECT

private slots:
	void defaultParsersDoNotShareTypes();
	void sharedStrataLibraryPoolsTypesAcrossParsers();
	void includeParserUsesTheSameStrataLibrary();
	void includeParserSearchesLibraryPathsAfterLocalDirectory();
	void fileRefsRemainValidForSharedLibraryAfterParserDies();
	void cleanupBelongsToTheStrataLibrary();
	void tagsetsParseAndExpand();
	void tagsetErrorsArePrecise();
	void includeDefinedTagsetsCanBeUsedByParent();
	void tagsetsDumpInSourceOrder();
	void bitfieldsParseAndValidate();
	void dynamicPlacementTagsParse();
	void semanticSchemaAndEmitTagsParse();
	void positionalSemanticDestinationTagsParse();
	void emitRoleWrappersAreScoped();
	void unknownSemanticSchemaReferencesFail();
	void viewTagsParse();
	void formatAndTreeTagsParse();
	void diagnosticTagsParse();
	void fourccBareTagIsRejected();
	void descriptionTagsParseAndDisplayRemainsSeparate();
	void magicTagsParse();
	void magicTagRequiresByteSequenceBeforeOptionalOffset();
	void findSearchExpressionsParse();
	void alignAndEntrypointTagsParse();
	void openAsTagsParse();
	void extentTagsAndScalarSizeofParse();
	void endianExpressionTagsParse();
	void ternaryExpressionTagsParse();
	void legacyCountAndSelectAliasesUseCanonicalTokens();
	void lengthIsIsReserved();
	void unsizedArraysRequireCount();
	void maxCountAndByteSequenceTerminatorsParse();
	void namedOffsetMapsAndValueAtParse();
	void scopePrefixesParse();
	void multiDimensionalFlexibleArraysParse();
	void elfRootIsExportedAndAssociated();
	void standardTypelibFilesParse();
};

static bool parseBuffer(Parser &parser, const char *text)
{
	parser.Init(text, strlen(text));
	return parser.Parse() != 0;
}

static int countTags(Tag *tag, TOKEN tok)
{
	int count = 0;
	for(; tag; tag = tag->link)
		if(tag->tok == tok)
			count++;
	return count;
}

static ExprNode *findByteSequenceExpr(ExprNode *expr)
{
	if(!expr)
		return nullptr;
	if(expr->type == EXPR_BYTESEQ)
		return expr;
	if(ExprNode *found = findByteSequenceExpr(expr->left))
		return found;
	if(ExprNode *found = findByteSequenceExpr(expr->right))
		return found;
	return findByteSequenceExpr(expr->cond);
}

static ExprNode *findTagWrapExpr(ExprNode *expr, TOKEN tok)
{
	if(!expr)
		return nullptr;
	if(expr->type == EXPR_TAGWRAP && expr->tok == tok)
		return expr;
	if(ExprNode *found = findTagWrapExpr(expr->left, tok))
		return found;
	if(ExprNode *found = findTagWrapExpr(expr->right, tok))
		return found;
	return findTagWrapExpr(expr->cond, tok);
}

static void collectCommaArgs(ExprNode *expr, std::vector<ExprNode *> *args)
{
	if(!expr || !args)
		return;
	if(expr->type == EXPR_COMMA)
	{
		collectCommaArgs(expr->left, args);
		collectCommaArgs(expr->right, args);
		return;
	}
	args->push_back(expr);
}

void CausewayTests::defaultParsersDoNotShareTypes()
{
	// Scenario: two callers create ordinary Parser instances in the same process.
	// Expected: a typedef parsed by the first parser is invisible to the second.
	// Regression guard: the old process-global identifier table leaked types
	// between unrelated parser uses.
	Parser first;
	QVERIFY(parseBuffer(first, "typedef dword LocalOnly;\n"));
	QCOMPARE(first.GetStrataLibrary()->globalTypeDeclList.size(), size_t(1));

	Parser second;
	QVERIFY(!parseBuffer(second, "LocalOnly value;\n"));
	QCOMPARE(second.GetStrataLibrary()->globalTypeDeclList.size(), size_t(0));
}

void CausewayTests::sharedStrataLibraryPoolsTypesAcrossParsers()
{
	// Scenario: a caller deliberately parses multiple buffers into one library.
	// Expected: the second parser can use a typedef produced by the first parser.
	// Regression guard: moving globals into Parser must not remove the original
	// ability to pool type declarations across related parses.
	StrataLibrary library;

	Parser first(&library);
	QVERIFY(parseBuffer(first, "typedef dword SharedType;\n"));

	Parser second(&library);
	QVERIFY(parseBuffer(second, "SharedType value;\n"));

	QCOMPARE(library.globalTypeDeclList.size(), size_t(2));
	QCOMPARE(library.globalFileHistory.size(), size_t(2));
}

void CausewayTests::includeParserUsesTheSameStrataLibrary()
{
	// Scenario: a source file includes another source file which defines a type.
	// Expected: the include-child parser contributes to the same StrataLibrary, and
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

	StrataLibrary library;
	Parser parser(&library);
	QVERIFY(parser.Ooof(qPrintable(mainFile.fileName())));

	QCOMPARE(library.globalFileHistory.size(), size_t(2));
	QCOMPARE(library.globalTypeDeclList.size(), size_t(2));
}

void CausewayTests::includeParserSearchesLibraryPathsAfterLocalDirectory()
{
	// Scenario: a user override definition includes a shared file that exists
	// in a Strata library directory rather than beside the override.
	// Expected: include resolution checks the including file's directory first,
	// then parser-supplied library paths.
	QTemporaryDir userDir;
	QTemporaryDir libraryDir;
	QVERIFY(userDir.isValid());
	QVERIFY(libraryDir.isValid());

	QFile includeFile(libraryDir.filePath("common.tl"));
	QVERIFY(includeFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(includeFile.write("typedef dword LibraryType;\n") > 0);
	includeFile.close();

	QFile mainFile(userDir.filePath("main.tl"));
	QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(mainFile.write("include \"common.tl\";\nLibraryType value;\n") > 0);
	mainFile.close();

	StrataLibrary library;
	Parser parser(&library);
	parser.AddIncludePath(qPrintable(libraryDir.path()));
	QVERIFY2(parser.Ooof(qPrintable(mainFile.fileName())), parser.LastErrStr());

	QCOMPARE(library.globalFileHistory.size(), size_t(2));
	QCOMPARE(library.globalTypeDeclList.size(), size_t(2));
	QCOMPARE(QString::fromLocal8Bit(library.globalFileHistory[1]->filePath),
	         QFileInfo(includeFile).canonicalFilePath());
}

void CausewayTests::fileRefsRemainValidForSharedLibraryAfterParserDies()
{
	// Scenario: qexed keeps parsed declarations after the parser object is gone.
	// Expected: FILEREF still points into FILE_DESC storage owned by StrataLibrary,
	// so diagnostics and round-tripping can inspect the original source buffer.
	// Regression guard: FILE_DESC lifetime used to be implicit process-global
	// state; it must not become tied to a temporary Parser cursor.
	StrataLibrary library;

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

void CausewayTests::cleanupBelongsToTheStrataLibrary()
{
	// Scenario: callers own a shared StrataLibrary and decide when to discard it.
	// Expected: cleanup empties declarations and file buffers once at the library
	// boundary; non-owning parsers do not delete shared state on destruction.
	// Regression guard: moving lexer cleanup out of Parser must not leave stale
	// FILE_DESC entries or double-delete include/parser state.
	StrataLibrary library;

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

void CausewayTests::tagsetsParseAndExpand()
{
	// Scenario: a Strata file declares a reusable annotation block for repeated
	// structure members.
	// Expected: the tagset is stored on the StrataLibrary, while tags(NAME) expands
	// to ordinary cloned tags on the target declaration.
	// Regression guard: PE data-directory rules should be declared once without
	// teaching Structure View about a separate runtime tagset concept.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"enum Dir { ExportEntry = 0, ImportEntry = 1 };\n"
						"typedef struct _DataDir { dword VirtualAddress; dword Size; } DataDir;\n"
						"typedef struct _Bucket { } Bucket;\n"
						"typedef struct _Export { dword value; } ExportDesc;\n"
						"typedef struct _Import { dword value; } ImportDesc;\n"
						"tagset DIRECTORY_TAGS\n"
						"[\n"
						"  name(Dir),\n"
						"  dynamic_struct(case(ExportEntry), type(ExportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0)),\n"
						"  dynamic_struct(case(ImportEntry), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0))\n"
						"];\n"
						"[export]\n"
						"struct Root {\n"
						"  [tags(DIRECTORY_TAGS)] DataDir dirs[2];\n"
						"} root;\n"));

	QCOMPARE(parser.GetStrataLibrary()->globalTagSetList.size(), size_t(1));
	QCOMPARE(QString::fromLocal8Bit(parser.GetStrataLibrary()->globalTagSetList[0]->name), QStringLiteral("DIRECTORY_TAGS"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(1));

	Tag *expanded = root->baseType->sptr->typeDeclList[0]->tagList;
	QVERIFY(FindTag(expanded, TOK_NAME, nullptr));
	QCOMPARE(countTags(expanded, TOK_DYNAMICSTRUCT), 2);
	QVERIFY(!FindTag(expanded, TOK_TAGS, nullptr));
}

void CausewayTests::tagsetErrorsArePrecise()
{
	// Scenario: tagsets are a simple alias feature, not a macro language with
	// forward references or composition.
	// Expected: invalid uses fail at parse time with dedicated diagnostics.
	// Regression guard: missing or recursive aliases should not become generic
	// syntax errors that leave definition authors guessing.
	Parser unknown;
	QVERIFY(!parseBuffer(unknown,
						 "struct Root {\n"
						 "  [tags(MISSING)] byte value;\n"
						 "} root;\n"));
	QCOMPARE(unknown.LastErr(), ERROR_UNKNOWN_TAGSET);

	Parser duplicate;
	QVERIFY(!parseBuffer(duplicate,
						 "tagset COMMON [offset(1)];\n"
						 "tagset COMMON [offset(2)];\n"));
	QCOMPARE(duplicate.LastErr(), ERROR_TAGSET_REDEFINITION);

	Parser nested;
	QVERIFY(!parseBuffer(nested,
						 "tagset BASE [offset(1)];\n"
						 "tagset WRAPPED [tags(BASE)];\n"));
	QCOMPARE(nested.LastErr(), ERROR_TAGS_NOT_ALLOWED_IN_TAGSET);
}

void CausewayTests::includeDefinedTagsetsCanBeUsedByParent()
{
	// Scenario: a shared StrataLibrary parses an included file that defines a
	// reusable tagset, then the parent file consumes it.
	// Expected: tagsets live in the same library-level result state as types, so
	// includes can provide common annotation blocks.
	// Regression guard: tagsets must not be parser-local cursor state.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());

	QFile includeFile(dir.filePath("common.tl"));
	QVERIFY(includeFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(includeFile.write("tagset COMMON [offset(4)];\n") > 0);
	includeFile.close();

	QFile mainFile(dir.filePath("main.tl"));
	QVERIFY(mainFile.open(QIODevice::WriteOnly | QIODevice::Text));
	QVERIFY(mainFile.write("include \"common.tl\";\nstruct Root { [tags(COMMON)] byte value; } root;\n") > 0);
	mainFile.close();

	StrataLibrary library;
	Parser parser(&library);
	QVERIFY(parser.Ooof(qPrintable(mainFile.fileName())));
	QCOMPARE(library.globalTagSetList.size(), size_t(1));

	TypeDecl *root = library.globalTypeDeclList.back();
	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_OFFSET, nullptr));
}

void CausewayTests::tagsetsDumpInSourceOrder()
{
	// Scenario: Strata round-tripping emits the durable parse result back to a
	// text file.
	// Expected: tagset declarations remain ordinary source-order statements,
	// while tagset uses have already expanded to normal tags.
	// Regression guard: adding semantic TagSet objects must not make Dump()
	// reorder or drop source-level alias declarations.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"tagset COMMON [offset(4)];\n"
						"[tags(COMMON)] byte value;\n"));

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString dumpPath = dir.filePath(QStringLiteral("dump.txt"));
	FILE *fp = fopen(qPrintable(dumpPath), "wb");
	QVERIFY(fp != nullptr);
	parser.Dump(fp);
	fclose(fp);

	QFile dumpFile(dumpPath);
	QVERIFY(dumpFile.open(QIODevice::ReadOnly));
	const QByteArray dumped = dumpFile.readAll();
	const int tagsetPos = dumped.indexOf("tagset COMMON");
	const int valuePos = dumped.indexOf("byte value");
	QVERIFY(tagsetPos >= 0);
	QVERIFY(valuePos > tagsetPos);
	QVERIFY(dumped.contains("[offset(4)]"));
	QVERIFY(!dumped.contains("tags(COMMON)"));
}

void CausewayTests::bitfieldsParseAndValidate()
{
	StrataLibrary library;
	Parser parser(&library);
	QVERIFY(parseBuffer(parser,
						"enum Masks { Flag = 0x80, Width = 0x70, Mode = 0x84 };\n"
						"enum Values { Off = 0, On = 1 };\n"
						"enum Patterns { ModePattern = 0x84 };\n"
						"bitfield Packed {\n"
						"  match(Flag);\n"
						"  match(\"Mode pattern\", Mode) = ModePattern;\n"
						"  field(\"Width bits\", Width);\n"
						"  field(\"Mode\", Mode, enum(Values));\n"
						"  field(Size, 0x07);\n"
						"};\n"
						"[export]\n"
						"struct Root { [bitfield(Packed)] byte packed; } root;\n"));

	QCOMPARE(library.globalBitfieldList.size(), size_t(1));
	QVERIFY(!library.globalFileHistory.empty());
	bool sawBitfieldStatement = false;
	for(Statement *stmt : library.globalFileHistory[0]->stmtList)
		if(stmt && stmt->stmtType == stmtBITFIELD)
			sawBitfieldStatement = true;
	QVERIFY(sawBitfieldStatement);

	Bitfield *bitfield = library.globalBitfieldList[0];
	QVERIFY(bitfield);
	QCOMPARE(QString::fromLocal8Bit(bitfield->name), QStringLiteral("Packed"));
	QCOMPARE(bitfield->entries.size(), size_t(5));
	QCOMPARE(bitfield->entries[0]->kind, bitfieldMATCH);
	QCOMPARE(bitfield->entries[0]->maskValue, INUMTYPE(0x80));
	QCOMPARE(bitfield->entries[0]->matchValue, INUMTYPE(0x80));
	QCOMPARE(QString::fromLocal8Bit(bitfield->entries[0]->inferredName), QStringLiteral("Flag"));
	QCOMPARE(QString::fromLocal8Bit(bitfield->entries[1]->displayName), QStringLiteral("Mode pattern"));
	QCOMPARE(bitfield->entries[1]->matchValue, INUMTYPE(0x84));
	QCOMPARE(bitfield->entries[2]->kind, bitfieldFIELD);
	QCOMPARE(QString::fromLocal8Bit(bitfield->entries[2]->displayName), QStringLiteral("Width bits"));
	QCOMPARE(bitfield->entries[4]->maskValue, INUMTYPE(0x07));
	QCOMPARE(QString::fromLocal8Bit(bitfield->entries[4]->displayName), QStringLiteral("Size"));
	QCOMPARE(QString::fromLocal8Bit(bitfield->entries[3]->valueEnumName), QStringLiteral("Values"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : library.globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;
	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	TypeDecl *packed = root->baseType->sptr->typeDeclList[0];
	QVERIFY(FindTag(packed->tagList, TOK_BITFIELD, nullptr));

	Parser duplicate;
	QVERIFY(!parseBuffer(duplicate,
						 "enum Masks { Flag = 0x80 };\n"
						 "bitfield Packed { match(Flag); };\n"
						 "bitfield Packed { match(Flag); };\n"));
	QCOMPARE(duplicate.LastErr(), ERROR_BITFIELD_REDEFINITION);

	Parser namelessMatch;
	QVERIFY(!parseBuffer(namelessMatch,
						 "bitfield Packed { match(0x80); };\n"));
	QCOMPARE(namelessMatch.LastErr(), ERROR_BITFIELD_ENTRY_SYNTAX);

	Parser unknown;
	QVERIFY(!parseBuffer(unknown,
						 "struct Root { [bitfield(Missing)] byte packed; } root;\n"));
	QCOMPARE(unknown.LastErr(), ERROR_UNKNOWN_BITFIELD);
}

void CausewayTests::dynamicPlacementTagsParse()
{
	// Scenario: a structure definition uses the dynamic placement tags consumed
	// by Structure View, including repeated dynamic_struct entries.
	// Expected: Strata treats them as ordinary parsed tags with expression
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
						"  [dynamic_struct(case(ImportEntry), type(ImportDesc), offset(VirtualAddress), mapper(offset_map), optional(Size != 0)), dynamic_array(case(ImportEntry), type(ImportDesc), offset(VirtualAddress), count(Size / sizeof(ImportDesc)), mapper(offset_map), terminated_by(value == 0), terminator(\"hidden\"))] DataDir dirs[2];\n"
						"  [dynamic_container(type(SectionBucket)), offset_map(VirtualAddress, SizeOfRawData, PointerToRawData)] Section sections[2];\n"
						"  [size_is(16), terminated_by(0)] char name[];\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
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
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(3));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_DYNAMICSTRUCT, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_DYNAMICARRAY, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_DYNAMICCONTAINER, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_OFFSETMAP, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[2]->tagList, TOK_TERMINATEDBY, nullptr));
}

void CausewayTests::semanticSchemaAndEmitTagsParse()
{
	// Scenario: a definition declares a semantic-only schema, attaches it to a
	// raw root, and emits byte-backed rows into a schema destination.
	// Expected: [semantic] allows unsized destination arrays, [semantic(View)]
	// is preserved on the root, and emit(...) keeps its role-wrapped arguments.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"typedef byte PayloadByte;\n"
						"[semantic(\"Root Data\")]\n"
						"typedef struct _ROOT_VIEW {\n"
						"  PayloadByte Payloads[];\n"
						"  [name(concat(\"item \", Key))]\n"
						"  struct { byte Key; char Label[]; } Items[];\n"
						"} ROOT_VIEW;\n"
						"[export, semantic(ROOT_VIEW)]\n"
						"typedef struct _Root {\n"
						"  dword payloadOffset;\n"
						"  dword payloadSize;\n"
						"  [emit_row(dest(Payloads, key(cstr(\"payloads\", payloadOffset), payloadSize + array_index() + element_value()), name(cstr(\"payloads\", payloadOffset))), offset(payloadOffset)),\n"
						"   emit_node(dest(Payloads, key(payloadOffset), name(fmt(\"payload {0}\", payloadOffset))), name(concat(\"payload \", payloadOffset)), offset(payloadOffset), extent(payloadSize), field(Size, root::value_at(0, dword)), attr(Note, cstr_from(root::value_at(4, dword), field_at(Items, 0, Key)))),\n"
						"   emit(dest(Payloads), label(payloadOffset), type(PayloadByte), offset(payloadOffset), count(payloadSize))] byte marker;\n"
						"} Root;\n"));

	QCOMPARE(parser.GetStrataLibrary()->globalTypeDeclList.size(), size_t(3));
	TypeDecl *schema = parser.GetStrataLibrary()->globalTypeDeclList[1];
	QVERIFY(FindTag(schema->tagList, TOK_SEMANTIC, nullptr));
	QVERIFY(schema->baseType && schema->baseType->sptr);
	QVERIFY(schema->baseType->sptr->semanticSchema);
	QCOMPARE(schema->baseType->sptr->typeDeclList.size(), size_t(2));
	TypeDecl *items = schema->baseType->sptr->typeDeclList[1];
	QVERIFY(items);
	QVERIFY(FindTag(items->tagList, TOK_NAME, nullptr));
	QVERIFY(!items->declList.empty());
	Type *itemsElement = BaseNode(items->declList[0]);
	QVERIFY(itemsElement && itemsElement->ty == typeSTRUCT && itemsElement->sptr);
	QVERIFY(itemsElement->sptr->semanticSchema);
	QCOMPARE(itemsElement->sptr->typeDeclList.size(), size_t(2));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[2];
	ExprNode *semanticExpr = nullptr;
	QVERIFY(FindTag(schema->tagList, TOK_SEMANTIC, &semanticExpr));
	QVERIFY(semanticExpr);
	QCOMPARE(semanticExpr->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(semanticExpr->str), QStringLiteral("Root Data"));
	semanticExpr = nullptr;
	QVERIFY(FindTag(root->tagList, TOK_SEMANTIC, &semanticExpr));
	QVERIFY(semanticExpr);
	QCOMPARE(semanticExpr->type, EXPR_IDENTIFIER);
	QCOMPARE(QString::fromLocal8Bit(semanticExpr->str), QStringLiteral("ROOT_VIEW"));

	TypeDecl *marker = root->baseType->sptr->typeDeclList[2];
	ExprNode *emitExpr = nullptr;
	QVERIFY(FindTag(marker->tagList, TOK_EMITROW, &emitExpr));
	QVERIFY(emitExpr);
	QCOMPARE(emitExpr->type, EXPR_COMMA);
	emitExpr = nullptr;
	QVERIFY(FindTag(marker->tagList, TOK_EMIT, &emitExpr));
	QVERIFY(emitExpr);
	QCOMPARE(emitExpr->type, EXPR_COMMA);
	emitExpr = nullptr;
	QVERIFY(FindTag(marker->tagList, TOK_EMITNODE, &emitExpr));
	QVERIFY(emitExpr);
	QCOMPARE(emitExpr->type, EXPR_COMMA);
}

void CausewayTests::positionalSemanticDestinationTagsParse()
{
	// Scenario: emit_node destinations use append(...) plus absolute and
	// sequence-relative item(...) addressing.
	// Expected: the parser owns each nested address expression beneath the
	// destination wrapper and preserves its keyword token.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[semantic] typedef struct _View { struct { byte Value; } Items[]; } View;\n"
						"[export, semantic(View)] typedef struct _Root {\n"
						"  [emit_node(dest(Items, append(\"defined\")), field(Value, a))] byte a;\n"
						"  [emit_node(dest(Items, item(\"defined\", b)), field(Value, b))] byte b;\n"
						"  [emit_node(dest(Items, item(c)), field(Value, c))] byte c;\n"
						"} Root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[1];
	QVERIFY(root && root->baseType && root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(3));

	ExprNode *appendEmit = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_EMITNODE, &appendEmit));
	ExprNode *appendWrap = findTagWrapExpr(appendEmit, TOK_APPEND);
	QVERIFY(appendWrap && appendWrap->left);
	QCOMPARE(appendWrap->left->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromUtf8(appendWrap->left->str), QStringLiteral("defined"));

	ExprNode *relativeEmit = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_EMITNODE, &relativeEmit));
	ExprNode *relativeItem = findTagWrapExpr(relativeEmit, TOK_ITEM);
	QVERIFY(relativeItem && relativeItem->left);
	QCOMPARE(relativeItem->left->type, EXPR_COMMA);

	ExprNode *absoluteEmit = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[2]->tagList, TOK_EMITNODE, &absoluteEmit));
	ExprNode *absoluteItem = findTagWrapExpr(absoluteEmit, TOK_ITEM);
	QVERIFY(absoluteItem && absoluteItem->left);
	QCOMPARE(absoluteItem->left->type, EXPR_COMMA);
	QVERIFY(absoluteItem->left->left);
	QCOMPARE(absoluteItem->left->left->type, EXPR_IDENTIFIER);

	Parser malformed;
	QVERIFY(!parseBuffer(malformed,
						 "struct Root { [emit_node(dest(Items, item(\"defined\",)), attr(Value, 1))] byte marker; } root;\n"));
}

void CausewayTests::emitRoleWrappersAreScoped()
{
	// Scenario: dest(...), label(...), key(...), attr(...), and field(...) are only role wrappers inside
	// semantic emit tags,
	// not public standalone field tags.
	// Expected: using either as a normal tag fails with the regular illegal-tag
	// diagnostic.
	Parser destParser;
	QVERIFY(!parseBuffer(destParser,
						 "struct Root { [dest(Payloads)] byte marker; } root;\n"));
	QCOMPARE(destParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser labelParser;
	QVERIFY(!parseBuffer(labelParser,
						 "struct Root { [label(marker)] byte marker; } root;\n"));
	QCOMPARE(labelParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser keyParser;
	QVERIFY(!parseBuffer(keyParser,
						 "struct Root { [key(marker)] byte marker; } root;\n"));
	QCOMPARE(keyParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser attrParser;
	QVERIFY(!parseBuffer(attrParser,
						 "struct Root { [attr(Name, marker)] byte marker; } root;\n"));
	QCOMPARE(attrParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser fieldParser;
	QVERIFY(!parseBuffer(fieldParser,
						 "struct Root { [field(Name, marker)] byte marker; } root;\n"));
	QCOMPARE(fieldParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser appendParser;
	QVERIFY(!parseBuffer(appendParser,
						 "struct Root { [append(\"items\")] byte marker; } root;\n"));
	QCOMPARE(appendParser.LastErr(), ERROR_ILLEGAL_TAG);

	Parser itemParser;
	QVERIFY(!parseBuffer(itemParser,
						 "struct Root { [item(0)] byte marker; } root;\n"));
	QCOMPARE(itemParser.LastErr(), ERROR_ILLEGAL_TAG);
}

void CausewayTests::unknownSemanticSchemaReferencesFail()
{
	// Scenario: a raw root names a semantic schema that has not been declared.
	// Expected: parsing fails at the declaration instead of silently dropping
	// all later emit(...) rows.
	Parser parser;
	QVERIFY(!parseBuffer(parser,
						 "[export, semantic(MissingView)]\n"
						 "typedef struct _Root { byte marker; } Root;\n"));
	QCOMPARE(parser.LastErr(), ERROR_UNKNOWN_SEMANTIC_SCHEMA);
}

void CausewayTests::viewTagsParse()
{
	// Scenario: a Strata declaration opts into a C++ semantic interpreter with
	// a compact view("id") hook.
	// Expected: the parser preserves the string expression on the TypeDecl, so
	// Structure View can run optional interpreters after raw rendering.
	// Regression guard: semantic interpretation must remain declarative Strata
	// metadata, not a hard-coded type-name check in the renderer.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[view(\"pe.imports\")]\n"
						"typedef struct _Import { dword thunk; } ImportDesc;\n"));

	QCOMPARE(parser.GetStrataLibrary()->globalTypeDeclList.size(), size_t(1));
	ExprNode *expr = nullptr;
	QVERIFY(FindTag(parser.GetStrataLibrary()->globalTypeDeclList[0]->tagList, TOK_VIEW, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(expr->str), QStringLiteral("pe.imports"));
}

void CausewayTests::formatAndTreeTagsParse()
{
	// Scenario: Strata has generic display and tree-presentation tags.
	// Expected: both parse as ordinary string-valued tags and remain available
	// on the declaration for Structure View to interpret.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						 "[export]\n"
						 "struct Root {\n"
						 "  [format(\"fourcc\")] dword tag;\n"
						 "  [format(\"hex\", width(8))] dword offset;\n"
						 "  [format(\"timestamp\", \"filetime\")] qword created;\n"
						 "  [tree(\"collapsed\")] byte payload;\n"
						 "  [case(fourcc(\"RIFF\"))] byte selected;\n"
						 "} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(5));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[0]->tagList, TOK_FORMAT, nullptr));
	ExprNode *formatExpr = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_FORMAT, &formatExpr));
	QVERIFY(formatExpr);
	QCOMPARE(formatExpr->type, EXPR_COMMA);
	QVERIFY(formatExpr->right);
	QVERIFY(formatExpr->right->left);
	QCOMPARE(formatExpr->right->left->type, EXPR_TAGWRAP);
	QCOMPARE(formatExpr->right->left->tok, TOK_WIDTH);
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[2]->tagList, TOK_FORMAT, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[3]->tagList, TOK_TREE, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[4]->tagList, TOK_CASE, nullptr));
}

void CausewayTests::diagnosticTagsParse()
{
	StrataLibrary library;
	Parser parser(&library);
	QVERIFY(parseBuffer(parser,
						"[export]\n"
						"struct Root {\n"
						"  [assert(element_value() == fourcc(\"RIFF\")), warn(Size > file_size(), \"oversized\")]\n"
						"  dword Magic;\n"
						"  dword Size;\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : library.globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	TypeDecl *magic = root->baseType->sptr->typeDeclList[0];
	ExprNode *assertExpr = nullptr;
	ExprNode *warnExpr = nullptr;
	QVERIFY(FindTag(magic->tagList, TOK_ASSERT, &assertExpr));
	QVERIFY(assertExpr);
	QVERIFY(FindTag(magic->tagList, TOK_WARN, &warnExpr));
	QVERIFY(warnExpr);
	std::vector<ExprNode *> warnArgs;
	collectCommaArgs(warnExpr, &warnArgs);
	QCOMPARE(warnArgs.size(), size_t(2));
	QCOMPARE(warnArgs[1]->type, EXPR_STRINGBUF);
}

void CausewayTests::fourccBareTagIsRejected()
{
	// Scenario: fourcc remains an expression helper, but no longer a public
	// bare display tag.
	// Expected: [fourcc] fails during tag parsing instead of being accepted and
	// interpreted specially by Structure View.
	Parser parser;
	QVERIFY(!parseBuffer(parser,
						 "struct Root {\n"
						 "  [fourcc] dword tag;\n"
						 "} root;\n"));
	QCOMPARE(parser.LastErr(), ERROR_ILLEGAL_TAG);
}

void CausewayTests::descriptionTagsParseAndDisplayRemainsSeparate()
{
	// Scenario: export("name") carries the description as the export tag's
	// own expression. display() is unrelated legacy metadata and stays separate.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[export(\"Portable Executable (PE)\"), display(\"legacy\")]\n"
						"struct Root { dword magic; } root;\n"));

	QCOMPARE(parser.GetStrataLibrary()->globalTypeDeclList.size(), size_t(1));
	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];

	ExprNode *description = nullptr;
	QVERIFY(FindTag(root->tagList, TOK_EXPORT, &description));
	QVERIFY(description);
	QCOMPARE(description->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(description->str), QStringLiteral("Portable Executable (PE)"));

	ExprNode *display = nullptr;
	QVERIFY(FindTag(root->tagList, TOK_DISPLAY, &display));
	QVERIFY(display);
	QCOMPARE(display->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(display->str), QStringLiteral("legacy"));
}

void CausewayTests::magicTagsParse()
{
	// Scenario: an exported root declares byte signatures for extensionless
	// binary files, including a non-printable leading byte.
	// Expected: magic(...) stores a byte sequence plus optional integer offset, so
	// Structure View can extract the offset and signature bytes later.
	// Regression guard: file association should not be limited to filename
	// suffixes, and ELF must not require fragile escaped string literals.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[export, magic({ 'M', 'Z' }), magic({ 0x7F, 'E', 'L', 'F' }, 4)]\n"
						"struct Root { byte value; } root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];
	QCOMPARE(countTags(root->tagList, TOK_MAGIC), 2);

	Tag *magic = FindTag(root->tagList, TOK_MAGIC, nullptr);
	QVERIFY(magic);
	ExprNode *expr = magic->expr;
	QVERIFY(expr);
	QCOMPARE(Evaluate(expr), INUMTYPE(4));
	QCOMPARE(magic->byteSequence.size(), size_t(4));
	QCOMPARE(magic->byteSequence[0], uint8_t(0x7f));
	QCOMPARE(magic->byteSequence[1], uint8_t('E'));
	QCOMPARE(magic->byteSequence[2], uint8_t('L'));
	QCOMPARE(magic->byteSequence[3], uint8_t('F'));

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString dumpPath = dir.filePath(QStringLiteral("dump.txt"));
	FILE *fp = fopen(qPrintable(dumpPath), "wb");
	QVERIFY(fp != nullptr);
	parser.Dump(fp);
	fclose(fp);

	QFile dumpFile(dumpPath);
	QVERIFY(dumpFile.open(QIODevice::ReadOnly));
	const QByteArray dumped = dumpFile.readAll();
	QVERIFY(dumped.contains("magic({ 0x7F, 'E', 'L', 'F' }, 4)"));
}

void CausewayTests::magicTagRequiresByteSequenceBeforeOptionalOffset()
{
	// Scenario: magic(...) is intentionally byte-sequence first, with a simple
	// optional integer offset after it.
	// Expected: the ZIP-style shorthand parses, while the old offset-first form
	// is rejected with a tag-specific message.
	// Regression guard: magic({'P', 'K'}) should be accepted and documented as
	// the obvious spelling for signatures at offset zero.
	Parser ok;
	QVERIFY(parseBuffer(ok,
						"[magic({ 'P', 'K', 0x03, 0x04 })]\n"
						"struct Root { byte value; } root;\n"));

	Parser parser;
	QVERIFY(!parseBuffer(parser,
						 "[magic(0, { 'P', 'K', 0x03, 0x04 })]\n"
						 "struct Root { byte value; } root;\n"));

	QCOMPARE(parser.LastErr(), ERROR_MAGIC_SYNTAX);
	QVERIFY(QByteArray(parser.LastErrStr()).contains("{ bytes... }"));
}

void CausewayTests::findSearchExpressionsParse()
{
	// Scenario: byte-pattern search is an expression built-in, not a tag.
	// Expected: reserved find_first/find_last calls parse in ordinary tag
	// expressions, while arbitrary identifier calls remain unsupported.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"struct Root {\n"
						"  [offset(find_last({ 'P', 'K', 0x05, 0x06 }, 65557))] dword eocd;\n"
						"  [offset(find_first({ 'M', 'Z' }))] word mz;\n"
						"} root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];
	TypeDecl *eocdDecl = root->baseType->sptr->typeDeclList[0];
	ExprNode *offset = nullptr;
	QVERIFY(FindTag(eocdDecl->tagList, TOK_OFFSET, &offset));
	QVERIFY(offset);
	QCOMPARE(offset->type, EXPR_FUNCTION);
	QCOMPARE(offset->tok, TOK_FINDLAST);
	QVERIFY(offset->left);
	QCOMPARE(offset->left->type, EXPR_COMMA);
	QVERIFY(offset->left->left);
	QCOMPARE(offset->left->left->type, EXPR_BYTESEQ);
	QCOMPARE(offset->left->left->byteSequence.size(), size_t(4));
	QCOMPARE(offset->left->left->byteSequence[0], uint8_t('P'));
	QCOMPARE(offset->left->left->byteSequence[1], uint8_t('K'));
	QCOMPARE(offset->left->left->byteSequence[2], uint8_t(0x05));
	QCOMPARE(offset->left->left->byteSequence[3], uint8_t(0x06));
	QVERIFY(offset->left->right);
	QCOMPARE(offset->left->right->type, EXPR_NUMBER);
	QCOMPARE(offset->left->right->val, INUMTYPE(65557));

	Parser invalidByte;
	QVERIFY(!parseBuffer(invalidByte,
						 "struct Root { [offset(find_last({ 0x100 }))] dword value; } root;\n"));

	Parser arbitraryCall;
	QVERIFY(!parseBuffer(arbitraryCall,
						 "struct Root { [offset(foo(1))] dword value; } root;\n"));
}

void CausewayTests::alignAndEntrypointTagsParse()
{
	// Scenario: Structure View definitions annotate both layout and a field that
	// identifies executable code.
	// Expected: align works on the exported compound declaration and on an
	// individual field, while entrypoint -- a parameterless tag, since it only
	// ever marks the field it is attached to -- survives alongside it.
	// Regression guard: align existed as a keyword long before the renderer used
	// it, so adding entrypoint must not disturb existing tag parsing.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[export, align(4)]\n"
						"struct Root {\n"
						"  byte magic;\n"
						"  [align(8), entrypoint] dword codeRva;\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(FindTag(root->tagList, TOK_ALIGN, nullptr));
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(2));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_ALIGN, nullptr));
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_ENTRYPOINT, nullptr));
}

void CausewayTests::openAsTagsParse()
{
	// Scenario: a row can declare that it opens a bounded byte range as another
	// Strata root. Expected: nested is accepted as the declarative spelling,
	// open_as remains a compatibility alias, and arguments are role-wrapped so
	// argument order stays explicit and future transform-style arguments can be
	// added without positional ambiguity.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"typedef struct _Child { byte magic; } Child;\n"
						"[open_as(type(Child), offset(dataOffset), extent(dataSize), name(fmt(\"slice {0}\", dataOffset)))]\n"
						"typedef struct _EntryOpenAs {\n"
						"  dword dataOffset;\n"
						"  dword dataSize;\n"
						"} EntryOpenAs;\n"
						"[nested(type(Child), offset(dataOffset), extent(dataSize), name(fmt(\"nested {0}\", dataOffset)))]\n"
						"typedef struct _EntryNested {\n"
						"  dword dataOffset;\n"
						"  dword dataSize;\n"
						"} EntryNested;\n"));

	std::vector<TypeDecl *> entries;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_OPENAS, nullptr))
			entries.push_back(decl);

	QCOMPARE(entries.size(), size_t(2));
	for(TypeDecl *entry : entries)
	{
		ExprNode *openAs = nullptr;
		QVERIFY(FindTag(entry->tagList, TOK_OPENAS, &openAs));
		QVERIFY(openAs);
		QVERIFY(findTagWrapExpr(openAs, TOK_TYPE));
		QVERIFY(findTagWrapExpr(openAs, TOK_OFFSET));
		QVERIFY(findTagWrapExpr(openAs, TOK_EXTENT));
		QVERIFY(findTagWrapExpr(openAs, TOK_NAME));
	}
}

void CausewayTests::extentTagsAndScalarSizeofParse()
{
	// Scenario: a binary format has a declaration whose file span is stored in
	// the data rather than implied by the selected Strata branch, and expressions
	// occasionally need small static scalar sizes.
	// Expected: extent(expr) is preserved as a normal tag, while sizeof(name) is
	// accepted only for a single scalar type name or scalar typedef alias.
	// Regression guard: PE section headers should not need hand-written
	// signature/header byte arithmetic once the optional header has an extent.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"typedef dword DWORD;\n"
						"[export]\n"
						"struct Root {\n"
						"  byte count;\n"
						"  [optional(count != 0), extent(count + sizeof(byte) + sizeof(DWORD))]\n"
						"  union { byte tiny; };\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(2));

	ExprNode *expr = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_EXTENT, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_BINARY);
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_OPTIONAL, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_BINARY);

	Parser fieldSizeof;
	QVERIFY(!parseBuffer(fieldSizeof,
						 "struct Root {\n"
						 "  byte value;\n"
						 "  [offset(sizeof(root.value))] byte other;\n"
						 "} root;\n"));
	QCOMPARE(fieldSizeof.LastErr(), ERROR_SIZEOF_SCALAR_ONLY);
}

void CausewayTests::endianExpressionTagsParse()
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
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	ExprNode *expr = nullptr;
	QVERIFY(FindTag(root->tagList, TOK_ENDIAN, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_BINARY);
}

void CausewayTests::ternaryExpressionTagsParse()
{
	// Scenario: a definition chooses a render-time count from file data using
	// C-style conditional syntax.
	// Expected: Strata preserves the ternary expression as the count payload,
	// so the renderer can evaluate the selected branch later.
	// Regression guard: conditional expressions should remain part of the small
	// expression language instead of forcing format-specific C++ for simple
	// branchy counts.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"[export]\n"
						"struct Root {\n"
						"  byte flag;\n"
						"  [size_is(flag ? 3 : 1)] byte values[];\n"
						"} root;\n"));

	TypeDecl *root = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			root = decl;

	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	QCOMPARE(root->baseType->sptr->typeDeclList.size(), size_t(2));

	ExprNode *expr = nullptr;
	QVERIFY(FindTag(root->baseType->sptr->typeDeclList[1]->tagList, TOK_COUNT, &expr));
	QVERIFY(expr);
	QCOMPARE(expr->type, EXPR_COMMA);
	QVERIFY(expr->left);
	QCOMPARE(expr->left->type, EXPR_TERTIARY);
}

void CausewayTests::legacyCountAndSelectAliasesUseCanonicalTokens()
{
	// Scenario: existing definitions use the historical IDL spellings.
	// Expected: the lexer accepts them but normalizes the parsed tags to the
	// canonical count/select token identities used by the renderer.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"struct Root {\n"
						"  byte discriminator;\n"
						"  [size_is(discriminator)] byte values[];\n"
						"  [switch_is(discriminator)] union { [case(0)] byte zero; } choice;\n"
						"} root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];
	QVERIFY(root);
	QVERIFY(root->baseType);
	QVERIFY(root->baseType->sptr);
	const auto &fields = root->baseType->sptr->typeDeclList;
	QCOMPARE(fields.size(), size_t(3));
	QVERIFY(FindTag(fields[1]->tagList, TOK_COUNT, nullptr));
	QVERIFY(FindTag(fields[2]->tagList, TOK_SELECT, nullptr));
}

void CausewayTests::lengthIsIsReserved()
{
	// Scenario: Strata knows the IDL-flavoured length_is keyword, but qexed
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

void CausewayTests::unsizedArraysRequireCount()
{
	// Scenario: a definition declares a flexible array member with [] but gives
	// no Strata count tag.
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

	Parser withMaxCount;
	QVERIFY(parseBuffer(withMaxCount,
						"struct Root {\n"
						"  [max_count(16), terminated_by(0)] byte data[];\n"
						"} root;\n"));
}

void CausewayTests::maxCountAndByteSequenceTerminatorsParse()
{
	// Scenario: terminated arrays can use a named maximum count and a byte
	// sequence terminator.
	// Expected: max_count(...) is accepted as an array bound tag, and
	// terminated_by({ ... }) preserves the byte-sequence expression for the
	// renderer.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"struct Root {\n"
						"  [max_count(1024), terminated_by({ 0x00, 0x00, 0x01 }), terminator(\"hidden\")] byte payload[];\n"
						"} root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];
	TypeDecl *field = root->baseType->sptr->typeDeclList[0];
	QVERIFY(FindTag(field->tagList, TOK_MAXCOUNT, nullptr));
	QVERIFY(FindTag(field->tagList, TOK_TERMINATOR, nullptr));

	ExprNode *terminatorExpr = nullptr;
	QVERIFY(FindTag(field->tagList, TOK_TERMINATEDBY, &terminatorExpr));
	ExprNode *byteSeq = findByteSequenceExpr(terminatorExpr);
	QVERIFY(byteSeq);
	QCOMPARE(byteSeq->byteSequence.size(), size_t(3));
	QCOMPARE(byteSeq->byteSequence[0], uint8_t(0));
	QCOMPARE(byteSeq->byteSequence[1], uint8_t(0));
	QCOMPARE(byteSeq->byteSequence[2], uint8_t(1));
}

void CausewayTests::namedOffsetMapsAndValueAtParse()
{
	// Scenario: Strata definitions can define named offset spaces and use
	// one-off scalar reads in expressions.
	// Expected: both the new named offset(...) form and value_at(...) parse
	// without disturbing existing anonymous offset_map(...) syntax.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"typedef struct _Section { dword va; dword size; dword raw; } Section;\n"
						"[export, offset_map(\"strings\", stringBase)]\n"
						"struct Root {\n"
						"  dword stringBase;\n"
						"  dword nameOffset;\n"
						"  dword targetRva;\n"
						"  dword probeRva;\n"
						"  [offset_map(\"rva\", va, size, raw), offset_map(va, size, raw), count(1)] Section sections[];\n"
						"  [offset(\"strings\", nameOffset), string, max_count(16), terminated_by(0)] char name[];\n"
						"  [offset(\"rva\", targetRva)] dword mappedValue;\n"
						"  [optional(value_at(\"rva\", probeRva, word) == 0x1234)] byte mappedProbe;\n"
						"  [optional(value_at(2, word) == 0x4433)] byte localProbe;\n"
						"} root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[1];
	QVERIFY(FindTag(root->tagList, TOK_OFFSETMAP, nullptr));

	TypeDecl *sections = root->baseType->sptr->typeDeclList[4];
	QVERIFY(FindTag(sections->tagList, TOK_OFFSETMAP, nullptr));

	TypeDecl *name = root->baseType->sptr->typeDeclList[5];
	QVERIFY(FindTag(name->tagList, TOK_OFFSET, nullptr));

	TypeDecl *mappedProbe = root->baseType->sptr->typeDeclList[7];
	ExprNode *optionalExpr = nullptr;
	QVERIFY(FindTag(mappedProbe->tagList, TOK_OPTIONAL, &optionalExpr));
	QVERIFY(optionalExpr);
}

void CausewayTests::scopePrefixesParse()
{
	// Scenario: the expression grammar recognizes scope prefixes as a distinct
	// token instead of collapsing them into ordinary ':' punctuation.
	// Expected: root:: and parent:: parse as scope-qualified expressions, with
	// the right-hand side still parsed by the ordinary expression grammar.
	Parser rootParser;
	rootParser.Init("root::value_at(4, dword)", strlen("root::value_at(4, dword)"));
	ExprNode *rootExpr = rootParser.ParseExpression();
	QVERIFY(rootExpr);
	QCOMPARE(rootExpr->type, EXPR_SCOPE);
	QVERIFY(rootExpr->left);
	QCOMPARE(rootExpr->left->type, EXPR_IDENTIFIER);
	QCOMPARE(QString::fromLocal8Bit(rootExpr->left->str), QStringLiteral("root"));
	QVERIFY(rootExpr->right);
	QCOMPARE(rootExpr->right->type, EXPR_VALUEAT);
	QCOMPARE(rootExpr->right->tok, TOK_VALUEAT);
	delete rootExpr;

	Parser parentParser;
	parentParser.Init("parent::innerValue", strlen("parent::innerValue"));
	ExprNode *parentExpr = parentParser.ParseExpression();
	QVERIFY(parentExpr);
	QCOMPARE(parentExpr->type, EXPR_SCOPE);
	QVERIFY(parentExpr->left);
	QCOMPARE(QString::fromLocal8Bit(parentExpr->left->str), QStringLiteral("parent"));
	QVERIFY(parentExpr->right);
	QCOMPARE(parentExpr->right->type, EXPR_IDENTIFIER);
	QCOMPARE(QString::fromLocal8Bit(parentExpr->right->str), QStringLiteral("innerValue"));
	delete parentExpr;
}

void CausewayTests::multiDimensionalFlexibleArraysParse()
{
	// Scenario: a variable-width string table is modeled as nested flexible
	// arrays, with comma-separated tag arguments applying to successive
	// dimensions.
	// Expected: the parser accepts both dimensions and preserves the comma
	// argument lists for the renderer.
	Parser parser;
	QVERIFY(parseBuffer(parser,
						"struct Root {\n"
						"  [count(4, 16), terminated_by(_, 0)] char strings[][];\n"
						"} root;\n"));

	TypeDecl *root = parser.GetStrataLibrary()->globalTypeDeclList[0];
	TypeDecl *field = root->baseType->sptr->typeDeclList[0];
	QVERIFY(field);
	QVERIFY(field->declList[0]->link->ty == typeARRAY);
	QVERIFY(field->declList[0]->link->link->ty == typeARRAY);

	ExprNode *countExpr = nullptr;
	QVERIFY(FindTag(field->tagList, TOK_COUNT, &countExpr));
	QVERIFY(countExpr);
	QCOMPARE(countExpr->type, EXPR_COMMA);

	ExprNode *terminatorExpr = nullptr;
	QVERIFY(FindTag(field->tagList, TOK_TERMINATEDBY, &terminatorExpr));
	QVERIFY(terminatorExpr);
	QCOMPARE(terminatorExpr->type, EXPR_COMMA);
}

void CausewayTests::elfRootIsExportedAndAssociated()
{
	// Scenario: qexed ships ELF as a real Structure View root definition.
	// Expected: elf.strata parses to an exported root with common ELF suffix
	// associations, so the UI can list and auto-select it like PE.
	// Regression guard: ELF support should not remain a stale standalone header
	// typedef that never appears in the Structure View picker.
	const QDir typeLibDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR));
	const QString path = typeLibDir.filePath(QStringLiteral("elf.strata"));
	Parser parser;
	QVERIFY2(parser.Ooof(qPrintable(path)), qPrintable(parser.LastErrStr()));

	TypeDecl *elf = nullptr;
	for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
		if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
			elf = decl;

	QVERIFY(elf);
	QVERIFY(FindTag(elf->tagList, TOK_ENDIAN, nullptr));
	QVERIFY(FindTag(elf->tagList, TOK_VIEW, nullptr));

	ExprNode *description = nullptr;
	QVERIFY(FindTag(elf->tagList, TOK_EXPORT, &description));
	QVERIFY(description);
	QCOMPARE(description->type, EXPR_STRINGBUF);
	QCOMPARE(QString::fromLocal8Bit(description->str), QStringLiteral("Executable and Linkable Format (ELF)"));

	ExprNode *assoc = nullptr;
	QVERIFY(FindTag(elf->tagList, TOK_ASSOC, &assoc));
	QVERIFY(assoc);
	QVERIFY(FindTag(elf->tagList, TOK_MAGIC, nullptr));
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

void CausewayTests::standardTypelibFilesParse()
{
	// Scenario: qexed ships real Strata definition files for users and tests.
	// Expected: every shipped example parses from the runtime data directory, and
	// relative includes such as elf.strata -> basetypes.strata resolve naturally.
	// Regression guard: the examples must not drift into stale, untested app data.
	const QDir typeLibDir(QStringLiteral(CAUSEWAY_TEST_DATA_DIR));
	QVERIFY2(typeLibDir.exists(), qPrintable(typeLibDir.absolutePath()));

	const QStringList files = {
		QStringLiteral("basetypes.strata"),
		QStringLiteral("dex.strata"),
		QStringLiteral("dtb.strata"),
		QStringLiteral("elf.strata"),
		QStringLiteral("pe.strata"),
		QStringLiteral("zip.strata"),
	};

	for(const QString &file : files)
	{
		const QString path = typeLibDir.filePath(file);
		QVERIFY2(QFileInfo::exists(path), qPrintable(path));
		QFile source(path);
		QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));
		const QString header = QString::fromUtf8(source.readLine()).trimmed();
		QCOMPARE(header, QStringLiteral("// q22-strata-v1"));

		Parser parser;
		QVERIFY2(parser.Ooof(qPrintable(path)), qPrintable(parser.LastErrStr()));
		QVERIFY2(!parser.GetStrataLibrary()->globalTypeDeclList.empty(), qPrintable(file));

		if(file == QStringLiteral("pe.strata"))
		{
			TypeDecl *pe = nullptr;
			for(TypeDecl *decl : parser.GetStrataLibrary()->globalTypeDeclList)
				if(decl && FindTag(decl->tagList, TOK_EXPORT, nullptr))
					pe = decl;

			QVERIFY(pe);
			QVERIFY(FindTag(pe->tagList, TOK_MAGIC, nullptr));
			ExprNode *description = nullptr;
			QVERIFY(FindTag(pe->tagList, TOK_EXPORT, &description));
			QVERIFY(description);
			QCOMPARE(description->type, EXPR_STRINGBUF);
			QCOMPARE(QString::fromLocal8Bit(description->str), QStringLiteral("Portable Executable (PE)"));
		}
	}
}

QTEST_MAIN(CausewayTests)
#include "causeway_tests.moc"
