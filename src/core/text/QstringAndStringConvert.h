/*****************************************************************
 * Copyright (C) 2023 Nanjing Normal University. All Rights Reserved.
 * Name: 田野考古制图系统(FAMS)
 * Author: liujintao
 * Version: defined in cmake/FampVersion.cmake
 * Description: QString 与 std::string UTF-8 转换工具
 *****************************************************************/

#pragma once

#include <QString>
#include <string>

inline QString str2qstr(const std::string& str)      // string转QString 乱码问题
{
    return QString::fromUtf8(str.c_str());
}

inline std::string qstr2str(const QString& qstr) //QString转string 乱码问题
{
    return qstr.toUtf8().toStdString();
}
