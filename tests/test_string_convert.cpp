#include <gtest/gtest.h>

#include <QString>
#include <string>

#include "QstringAndStringConvert.h"

TEST(StringConvertTest, QStringToStdStringRoundTrip)
{
    QString original = QString::fromUtf8("田野考古制图系统 FAMP");
    std::string converted = qstr2str(original);
    QString back = str2qstr(converted);

    // 往返转换应保持可读性
    EXPECT_FALSE(converted.empty());
    EXPECT_FALSE(back.isEmpty());
}

TEST(StringConvertTest, StdStringToQStringRoundTrip)
{
    std::string original = "Hello FAMP 123";
    QString converted = str2qstr(original);
    std::string back = qstr2str(converted);

    EXPECT_EQ(original, back);
}

TEST(StringConvertTest, EmptyStringConversion)
{
    std::string empty;
    QString qempty = str2qstr(empty);
    EXPECT_TRUE(qempty.isEmpty());

    std::string back = qstr2str(qempty);
    EXPECT_TRUE(back.empty());
}

TEST(StringConvertTest, SpecialCharacters)
{
    // 测试特殊字符
    std::string special = "！@#￥%……&*（）——+";
    QString qstr = str2qstr(special);
    EXPECT_FALSE(qstr.isEmpty());
}
