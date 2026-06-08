#ifndef FINDPATTERN_H
#define FINDPATTERN_H

#include <QByteArray>
#include <QString>

enum SearchDataType {
    SearchHex,
    SearchUTF8,
    SearchUTF16,
    SearchUTF32,
    SearchByte,
    SearchWord,
    SearchDword,
};

QByteArray buildFindPattern(const QString &text,
                            SearchDataType type,
                            bool textBigEndian,
                            bool integerBigEndian,
                            bool signedIntegers);

#endif // FINDPATTERN_H
