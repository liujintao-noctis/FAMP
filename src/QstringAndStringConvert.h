#pragma once

#include<qstring.h>
using namespace std;

static QString str2qstr(const string str)		// string转QString 乱码问题
{
	return QString::fromLocal8Bit(str.data());
}

static string qstr2str(const QString qstr)	//QString转string 乱码问题
{
	QByteArray cdata = qstr.toLocal8Bit();
	return string(cdata);
}
